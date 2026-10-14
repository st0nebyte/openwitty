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
}

Status MockTransport::open() {
    if (!present_) return makeError(Errc::NotFound, "mock device unplugged");
    open_ = true;
    return okStatus();
}

DeviceInfo MockTransport::info() const {
    return DeviceInfo{kLewittVendorId, 0x0004, "CONNECT 2 (simulated)", "MOCK0001", "mock"};
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
            case Fault::ShortRead: return Bytes{1, 2, 3};
            default: break;
        }
    }
    return Bytes{};
}

Result<Bytes> MockTransport::read(MessageId id) {
    auto f = maybeFault();
    if (!f) return f;
    if (!f.value().empty()) return makeError(Errc::ShortTransfer, "simulated short transfer");
    switch (id) {
        case MessageId::Settings: return settings_;
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
    auto f = maybeFault();
    if (!f) return f.error();
    if (!f.value().empty()) return makeError(Errc::ShortTransfer, "simulated short transfer");
    const MessageInfo* mi = messageInfo(id);
    if (!mi || !mi->writable) return makeError(Errc::NotSupported, "message is read-only");
    if (data.size() != mi->size) return makeError(Errc::InvalidArgument, "wrong size");
    if (id == MessageId::Settings) settings_.assign(data.begin(), data.end());
    if (id == MessageId::Triggers) {
        auto cmd = static_cast<std::uint16_t>(data[0] | (data[1] << 8));
        if (cmd == 1 || cmd == 2 || cmd == 3) { agProgress_ = 0; status_[0x23] = 0x40; }
    }
    log_.push_back(id);
    return okStatus();
}

}  // namespace lewitt
