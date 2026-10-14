#include "DeviceSession.hpp"

#include <cstdio>

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

// Failure bookkeeping. Only evidence of an unresponsive link counts; a stall (device
// answered "no"), busy/permission problems and cancellations never escalate.
void DeviceSession::noteResult(const Status& s) {
    if (s) { failures_ = 0; return; }
    switch (s.error().code) {
        case Errc::Timeout: case Errc::Io: case Errc::ShortTransfer: case Errc::Malformed:
            if (++failures_ >= kMaxConsecutiveFailures && state_ == ConnectionState::Connected) {
                state_ = ConnectionState::Error;
                lastError_ = makeError(Errc::Timeout, "device not responding (" + std::to_string(failures_) +
                                                      " consecutive failures); reconnect required");
                transport_->close();
            }
            break;
        default: break;
    }
}

Status DeviceSession::handshake() {
    if (auto s = transport_->open(); !s) return s;
    // Version handshake first, as LewittDevice::open does: message 0x0A, major must be 1.
    auto r = transport_->read(MessageId::ApiVersion);
    if (!r) { transport_->close(); return r.error(); }
    auto m = ApiVersionMessage::fromBytes(r.value());
    if (!m) { transport_->close(); return m.error(); }
    api_ = m.value();
    if (api_->major() != kSupportedApiMajor) {
        transport_->close();
        return makeError(Errc::NotSupported,
                         "Message API version not supported by the device (device major " +
                         std::to_string(api_->major()) + ", supported " + std::to_string(kSupportedApiMajor) + ")");
    }
    DeviceInfo di = transport_->info();
    caps_ = DeviceCapabilities{api_->major(), api_->minor(), di.pid, deviceModeFromPid(di.pid),
                               transport_->isSimulated()};
    // The original refuses PIDs that are neither of its two modes ("Device mode not recognized").
    if (di.pid != 0 && caps_.mode == DeviceMode::Unknown && !transport_->isSimulated()) {
        transport_->close();
        char buf[96];
        std::snprintf(buf, sizeof buf, "unsupported device mode (USB product id 0x%04x is not a CONNECT 2 APP mode)", di.pid);
        return makeError(Errc::NotSupported, buf);
    }
    return okStatus();
}

Status DeviceSession::connect() {
    state_ = ConnectionState::Connecting;
    failures_ = 0;
    if (auto s = handshake(); !s) {
        api_.reset();
        auto e = s.error();
        fail(e);
        state_ = (e.code == Errc::Disconnected || e.code == Errc::NotFound) ? ConnectionState::Disconnected
                                                                              : ConnectionState::Error;
        return e;
    }
    state_ = ConnectionState::Connected;
    if (auto s = refreshSettings(); !s) { state_ = ConnectionState::Error; transport_->close(); return s; }
    (void)refreshStatus();
    return okStatus();
}

Status DeviceSession::reconnect() {
    // keep what the user typed; everything device-side is re-read
    auto savedPending = pending_;
    auto oldCurrent = current_;
    transport_->close();
    state_ = ConnectionState::Connecting;
    failures_ = 0;
    if (auto s = handshake(); !s) {
        auto e = s.error();
        state_ = (e.code == Errc::Disconnected || e.code == Errc::NotFound) ? ConnectionState::Disconnected
                                                                              : ConnectionState::Error;
        lastError_ = e;
        return e;
    }
    state_ = ConnectionState::Connected;
    pending_ = savedPending;
    current_ = oldCurrent;            // so refreshSettings() can merge device-side changes
    auto s = refreshSettings();
    if (!s) { state_ = ConnectionState::Error; transport_->close(); return s; }
    (void)refreshStatus();
    return okStatus();
}

void DeviceSession::disconnect() {
    transport_->close();
    state_ = ConnectionState::Disconnected;
    current_.reset(); pending_.reset(); status_.reset(); api_.reset(); caps_ = {}; failures_ = 0;
}

Status DeviceSession::refreshSettings() {
    auto r = transport_->read(MessageId::Settings);
    if (!r) { auto st = fail(r.error()); noteResult(st); return st; }
    auto m = SettingsMessage::fromBytes(r.value());
    if (!m) { auto st = fail(m.error()); noteResult(st); return st; }
    failures_ = 0;
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
    if (!r) { auto st = fail(r.error()); noteResult(st); return st; }
    auto m = StatusMessage::fromBytes(r.value());
    if (!m) { auto st = fail(m.error()); noteResult(st); return st; }
    failures_ = 0;
    status_ = m.value();
    return okStatus();
}

Status DeviceSession::commit() {
    if (!writesEnabled())
        return makeError(Errc::WritesDisabled, "hardware writes are disabled until the protocol is verified");
    if (!pending_) return okStatus();
    if (!current_) return makeError(Errc::InvalidArgument, "no device settings read yet; refusing to write");
    if (*pending_ == *current_) {   // nothing differs: no duplicate command
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
    // Backend-level safety (does not depend on GUI state): size/permission guard and
    // "only bytes with a verified setter may differ from what the device reported".
    auto bytes = pending_->bytes();
    if (auto g = checkWriteAllowed(MessageId::Settings, bytes, WriteGuardContext{transport_->isSimulated(),
                                                                                 policy_.allowPersistentChanges}); !g)
        return g;
    if (auto g = verifyOnlyKnownFieldsChanged(*current_, *pending_); !g) return g;
    if (auto s = transport_->write(MessageId::Settings, bytes); !s) { auto st = fail(s.error()); noteResult(st); return st; }
    failures_ = 0;
    return refreshSettings();
}

Status DeviceSession::sendTrigger(Trigger t) {
    if (!writesEnabled())
        return makeError(Errc::WritesDisabled, "hardware writes are disabled until the protocol is verified");
    auto b = TriggerMessage(t).bytes();
    if (auto g = checkWriteAllowed(MessageId::Triggers, b, WriteGuardContext{transport_->isSimulated(),
                                                                              policy_.allowPersistentChanges}); !g)
        return g;
    if (auto s = transport_->write(MessageId::Triggers, b); !s) { auto st = fail(s.error()); noteResult(st); return st; }
    failures_ = 0;
    return okStatus();
}

}  // namespace lewitt
