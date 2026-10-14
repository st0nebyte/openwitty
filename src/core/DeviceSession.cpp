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
    // Version handshake first, as LewittDevice::open does: message 0x0A, major must be 1.
    if (auto r = transport_->read(MessageId::ApiVersion); !r) {
        transport_->close();
        return fail(r.error());
    } else if (auto m = ApiVersionMessage::fromBytes(r.value()); !m) {
        transport_->close();
        return fail(m.error());
    } else {
        api_ = m.value();
        if (api_->major() != kSupportedApiMajor) {
            transport_->close();
            state_ = ConnectionState::Error;
            lastError_ = makeError(Errc::NotSupported,
                "Message API version not supported by the device (device major " +
                std::to_string(api_->major()) + ", supported " + std::to_string(kSupportedApiMajor) + ")");
            return lastError_;
        }
    }
    state_ = ConnectionState::Connected;
    if (auto s = refreshSettings(); !s) { state_ = ConnectionState::Error; transport_->close(); return s; }
    (void)refreshStatus();
    return okStatus();
}

void DeviceSession::disconnect() {
    transport_->close();
    state_ = ConnectionState::Disconnected;
    current_.reset(); pending_.reset(); status_.reset(); api_.reset();
}

Status DeviceSession::refreshSettings() {
    auto r = transport_->read(MessageId::Settings);
    if (!r) return fail(r.error());
    auto m = SettingsMessage::fromBytes(r.value());
    if (!m) return fail(m.error());
    // Same merge as the original app: user edits survive, fields that changed on the
    // device (hardware button, other software) win. Unedited pending state is dropped.
    if (pending_ && current_) {
        SettingsMessage merged = *pending_;
        merged.mergeExternalChanges(*current_, m.value());
        pending_ = (merged == m.value()) ? std::nullopt : std::optional<SettingsMessage>(merged);
    } else {
        pending_.reset();
    }
    current_ = m.value();
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
    if (current_ && *pending_ == *current_) {   // nothing differs: no duplicate command
        pending_.reset();
        return okStatus();
    }
    if (sampleRate_ != 0 && violatesDspRestriction(*current_, *pending_, sampleRate_))
        return makeError(Errc::NotSupported,
                         "Denoise and Comp are not available at 96 kHz (documented restriction)");
    if (changesRouting(*current_, *pending_) && !(policy_.allowRoutingChanges || transport_->isSimulated()))
        return makeError(Errc::WritesDisabled,
                         "streaming-mode changes alter channel routing and may restart the device; "
                         "not enabled");
    auto bytes = pending_->bytes();
    if (auto s = transport_->write(MessageId::Settings, bytes); !s) return fail(s.error());
    return refreshSettings();
}

Status DeviceSession::sendTrigger(Trigger t) {
    if (!writesEnabled())
        return makeError(Errc::WritesDisabled, "hardware writes are disabled until the protocol is verified");
    if ((t == Trigger::SwitchToNormalMode || t == Trigger::SwitchToMacOSStreamingMode) &&
        !transport_->isSimulated())
        return makeError(Errc::NotSupported,
                         "device mode switching restarts the device and is not enabled on real hardware");
    if (t == Trigger::FactoryReset)
        return makeError(Errc::NotSupported, "factory reset is destructive and never sent by this build");
    if (t == Trigger::ResetLedColors && !(policy_.allowPersistentChanges || transport_->isSimulated()))
        return makeError(Errc::WritesDisabled, "resetting LED colours is a persistent change; not enabled");
    auto b = TriggerMessage(t).bytes();
    if (auto s = transport_->write(MessageId::Triggers, b); !s) return fail(s.error());
    return okStatus();
}

}  // namespace lewitt
