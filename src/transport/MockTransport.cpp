#include "MockTransport.hpp"

#include <algorithm>

namespace lewitt {

MockTransport::MockTransport() : settings_(0x80, 0), status_(0x40, 0), version_(0x40, 0), api_(0x40, 0) {
    api_[0] = 1;  // simulated device speaks API v1.0
    // Plausible defaults (not device data): input1 25 dB, volumes 0 dB, preamp clean.
    settings_[0] = 0x00; settings_[1] = 25;      // 25 dB Q8.8
    settings_[10] = 0x00; settings_[11] = 25;
    settings_[20] = 0; settings_[21] = 0;
    settings_[27] = 0; settings_[28] = 0;
    settings_[34] = 1;  // simulated default: microphone monitoring
    settings_[37] = 80;  // LED brightness
    settings_[41] = 0xFF;  // mute LED red (guide: factory red)
    settings_[53] = 0x0E;  // custom button: toggle brightness (as in the guide screenshot)
    settings_[56] = 1;     // physical focus: microphone (confirmed mapping; simulation only)
}

Status MockTransport::open() {
    if (!present_) return makeError(Errc::NotFound, "mock device unplugged");
    open_ = true;
    return okStatus();
}

DeviceInfo MockTransport::info() const {
    if (pidOverride_) return DeviceInfo{kLewittVendorId, pidOverride_, "CONNECT 2 (simulated)", "MOCK0001", "mock"};
    return DeviceInfo{kLewittVendorId, std::uint16_t(mode_ == DeviceMode::Normal ? 0x0004 : 0x0002),
                      "CONNECT 2 (simulated)", "MOCK0001", "mock"};
}

Result<Bytes> MockTransport::maybeFault() {
    if (!present_ || !open_) return makeError(Errc::Disconnected, "device not open");
    if (fault_ != Fault::None && faultCount_ > 0) {
        --faultCount_;
        Fault f = fault_;
        if (faultCount_ == 0) fault_ = Fault::None;
        switch (f) {
            case Fault::Timeout: return makeError(Errc::Timeout, "simulated timeout");
            case Fault::Disconnect: open_ = false; present_ = false;
                return makeError(Errc::Disconnected, "simulated disconnect");
            case Fault::Stall: return makeError(Errc::Stall, "simulated stall");
            case Fault::AccessDenied: return makeError(Errc::AccessDenied, "simulated EACCES");
            case Fault::Busy: return makeError(Errc::Busy, "simulated EBUSY");
            case Fault::Internal: return makeError(Errc::Internal, "simulated internal failure");
            case Fault::LongRead: return Bytes(0x1000, 0xEE);
            case Fault::ShortRead: return Bytes{1, 2, 3};
            default: break;
        }
    }
    return Bytes{};
}

Result<Bytes> MockTransport::read(MessageId id) {
    if (cancel_.load()) return makeError(Errc::Cancelled, "request cancelled");
    if (readHook_) readHook_();
    if (cancel_.load()) return makeError(Errc::Cancelled, "request cancelled");
    auto f = maybeFault();
    if (!f) return f;
    if (!f.value().empty()) {
        // a response of the wrong length is classified by size, never trusted
        if (f.value().size() > 0x100) return makeError(Errc::Malformed, "simulated oversized response");
        return makeError(Errc::ShortTransfer, "simulated short transfer");
    }
    // Settling-delay / external-change simulation applies to any read call (settings or status),
    // same as the real device, which answers both kinds of GET regardless of write staleness.
    if (externalChangeAfterReads_ == 0) {
        if (externalChangeOffset_ < settings_.size()) settings_[externalChangeOffset_] = externalChangeValue_;
        externalChangeAfterReads_ = -1;
    } else if (externalChangeAfterReads_ > 0) {
        --externalChangeAfterReads_;
    }
    switch (id) {
        case MessageId::Settings:
            if (staleReadsRemaining_ > 0) { --staleReadsRemaining_; return preWriteSettings_; }
            return settings_;
        case MessageId::Status: {
            Bytes st = status_;
            if (agProgress_ >= 0) {  // simulated measurement
                st[0x24] = static_cast<std::uint8_t>(agProgress_);
                if (agProgress_ >= 100) {
                    st[0x23] = agOutcome_;
                    status_[0x23] = agOutcome_;   // outcome persists after the measurement
                    status_[0x24] = 100;
                    agProgress_ = -1;
                }
                else agProgress_ += 10;
            }
            return st;
        }
        case MessageId::Version: return version_;
        case MessageId::ApiVersion: return api_;
        default: return makeError(Errc::NotSupported, "message is write-only");
    }
}

Status MockTransport::write(MessageId id, std::span<const std::uint8_t> data) {
    if (writeHook_) writeHook_();
    auto f = maybeFault();
    if (!f) return f.error();
    if (!f.value().empty()) return makeError(Errc::ShortTransfer, "simulated short transfer");
    const MessageInfo* mi = messageInfo(id);
    if (!mi || !mi->writable) return makeError(Errc::NotSupported, "message is read-only");
    if (data.size() != mi->size) return makeError(Errc::InvalidArgument, "wrong size");
    if (id == MessageId::Settings) {
        if (staleReadsBudget_ > 0) {
            preWriteSettings_ = settings_;   // snapshot BEFORE applying the new value
            staleReadsRemaining_ = staleReadsBudget_;
        }
        settings_.assign(data.begin(), data.end());
    }
    if (id == MessageId::Triggers) {
        auto cmd = static_cast<std::uint16_t>(data[0] | (data[1] << 8));
        triggerLog_.push_back(cmd);
        if (cmd == 1 || cmd == 2 || cmd == 3) { agProgress_ = 0; status_[0x23] = 0x40; }
        if (cmd == 0x104) mode_ = DeviceMode::MacOSStreaming;
        if (cmd == 0x105) mode_ = DeviceMode::Normal;
    }
    log_.push_back(id);
    return okStatus();
}

}  // namespace lewitt
