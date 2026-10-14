#include "DeviceSession.hpp"

namespace lewitt {

DeviceSession::DeviceSession(std::unique_ptr<IDeviceTransport> t, WritePolicy p)
    : transport_(std::move(t)), policy_(p) {}

Status DeviceSession::fail(Error e) {
    lastError_ = e;
    if (e.code == Errc::Disconnected || e.code == Errc::NotFound) {
        state_ = ConnectionState::Disconnected;
        transport_->close();
    } else if (state_ != ConnectionState::Connected) {
        state_ = ConnectionState::Error;
    }
    return e;
}

Status DeviceSession::connect() {
    state_ = ConnectionState::Connecting;
    if (auto s = transport_->open(); !s) return fail(s.error());
    state_ = ConnectionState::Connected;
    if (auto s = refreshSettings(); !s) { state_ = ConnectionState::Error; transport_->close(); return s; }
    (void)refreshStatus();
    return okStatus();
}

void DeviceSession::disconnect() {
    transport_->close();
    state_ = ConnectionState::Disconnected;
    current_.reset(); pending_.reset(); status_.reset();
}

Status DeviceSession::refreshSettings() {
    auto r = transport_->read(MessageId::Settings);
    if (!r) return fail(r.error());
    auto m = SettingsMessage::fromBytes(r.value());
    if (!m) return fail(m.error());
    current_ = m.value();
    pending_.reset();
    return okStatus();
}

Status DeviceSession::refreshStatus() {
    auto r = transport_->read(MessageId::Status);
    if (!r) return fail(r.error());
    auto m = StatusMessage::fromBytes(r.value());
    if (!m) return fail(m.error());
    status_ = m.value();
    return okStatus();
}

Status DeviceSession::commit() {
    if (!writesEnabled())
        return makeError(Errc::WritesDisabled, "hardware writes are disabled until the protocol is verified");
    if (!pending_) return okStatus();
    auto bytes = pending_->bytes();
    if (auto s = transport_->write(MessageId::Settings, bytes); !s) return fail(s.error());
    return refreshSettings();
}

Status DeviceSession::sendTrigger(Trigger t) {
    if (!writesEnabled())
        return makeError(Errc::WritesDisabled, "hardware writes are disabled until the protocol is verified");
    if (t == Trigger::Unknown0x103 || t == Trigger::Unknown0x106)
        return makeError(Errc::NotSupported, "trigger meaning unknown; refusing to send");
    auto b = TriggerMessage(t).bytes();
    if (auto s = transport_->write(MessageId::Triggers, b); !s) return fail(s.error());
    return okStatus();
}

}  // namespace lewitt
