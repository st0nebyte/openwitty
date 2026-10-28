#include "DeviceSession.hpp"

#include <algorithm>
#include <cstdio>
#include <thread>

namespace lewitt {

namespace {
bool indicatesUnresponsiveSettings(Errc code) {
    return code == Errc::Timeout || code == Errc::Io ||
           code == Errc::ShortTransfer || code == Errc::Malformed;
}
std::vector<std::size_t> diffOffsets(const SettingsMessage& a, const SettingsMessage& b) {
    std::vector<std::size_t> out;
    auto ab = a.bytes(), bb = b.bytes();
    for (std::size_t i = 0; i < ab.size(); ++i)
        if (ab[i] != bb[i]) out.push_back(i);
    return out;
}
// Offsets outside `expected` that differ between `baseline` and `now` - i.e. something changed
// that this write didn't intend to change (a concurrent device-side edit, or something unexpected).
std::vector<std::size_t> unexpectedOffsets(const SettingsMessage& baseline, const SettingsMessage& now,
                                            const std::vector<std::size_t>& expected) {
    std::vector<std::size_t> out;
    auto bb = baseline.bytes(), nb = now.bytes();
    for (std::size_t i = 0; i < bb.size(); ++i) {
        if (bb[i] == nb[i]) continue;
        if (std::find(expected.begin(), expected.end(), i) != expected.end()) continue;
        out.push_back(i);
    }
    return out;
}
}  // namespace

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
    failures_ = settingsFailures_ = 0;
    status_.reset();  // an earlier status frame is not evidence of this connection's readiness
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
    failures_ = settingsFailures_ = 0;
    status_.reset();  // do not publish pre-disconnect meter/status data as current
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
    current_.reset(); pending_.reset(); status_.reset(); api_.reset(); caps_ = {};
    failures_ = settingsFailures_ = 0;
}

Status DeviceSession::refreshSettings(SettingsRefreshTiming* timing) {
    if (timing) { *timing = {}; timing->started = std::chrono::steady_clock::now(); }
    auto r = transport_->read(MessageId::Settings);
    if (timing) timing->transportDone = std::chrono::steady_clock::now();
    if (!r) {
        auto st = fail(r.error());
        noteResult(st);
        settingsFailures_ = indicatesUnresponsiveSettings(st.error().code) ? settingsFailures_ + 1 : 0;
        if (state_ == ConnectionState::Connected && settingsFailures_ >= kMaxConsecutiveFailures) {
            state_ = ConnectionState::Error;
            lastError_ = makeError(st.error().code, "settings not refreshing after " +
                std::to_string(settingsFailures_) + " consecutive attempts; reconnect required");
            transport_->close();
        }
        return st;
    }
    auto m = SettingsMessage::fromBytes(r.value());
    if (timing) timing->decoded = std::chrono::steady_clock::now();
    if (!m) {
        auto st = fail(m.error());
        noteResult(st);
        settingsFailures_ = indicatesUnresponsiveSettings(st.error().code) ? settingsFailures_ + 1 : 0;
        if (state_ == ConnectionState::Connected && settingsFailures_ >= kMaxConsecutiveFailures) {
            state_ = ConnectionState::Error;
            lastError_ = makeError(st.error().code, "settings not decoding after " +
                std::to_string(settingsFailures_) + " consecutive attempts; reconnect required");
            transport_->close();
        }
        return st;
    }
    failures_ = 0;
    settingsFailures_ = 0;
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
    if (timing) timing->merged = std::chrono::steady_clock::now();
    return okStatus();
}

Status DeviceSession::refreshStatus(StatusRefreshTiming* timing) {
    if (timing) { *timing = {}; timing->started = std::chrono::steady_clock::now(); }
    auto r = transport_->read(MessageId::Status);
    if (timing) timing->transportDone = std::chrono::steady_clock::now();
    if (!r) { auto st = fail(r.error()); noteResult(st); return st; }
    auto m = StatusMessage::fromBytes(r.value());
    if (timing) timing->decoded = std::chrono::steady_clock::now();
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
    // Real hardware: only offsets explicitly authorized for this experiment may differ.
    if (!transport_->isSimulated()) {
        for (std::size_t off : diffOffsets(*current_, *pending_)) {
            if (std::find(policy_.hardwareWriteOffsets.begin(), policy_.hardwareWriteOffsets.end(), off) ==
                policy_.hardwareWriteOffsets.end())
                return makeError(Errc::WritesDisabled,
                                 "settings byte " + std::to_string(off) +
                                     " is not authorized for hardware writes in this build/session");
        }
    }
    ++settingsWriteAttempts_;
    if (auto s = transport_->write(MessageId::Settings, bytes); !s) { auto st = fail(s.error()); noteResult(st); return st; }
    failures_ = 0;
    return refreshSettings();
}

WriteResult DeviceSession::commitAndVerify(PollPolicy pp) {
    WriteResult r;
    if (!pending_) { r.state = WriteState::Verified; return r; }   // nothing to do
    if (!writesEnabled()) {
        r.state = WriteState::Failed;
        r.error = makeError(Errc::WritesDisabled, "hardware writes are disabled until the protocol is verified");
        return r;
    }
    // Fresh snapshot immediately before writing, reusing the existing device-wins merge algorithm:
    // any field that changed externally since the last refresh is adopted from the device; the
    // pending edit survives for every field that didn't. If this happens to leave nothing pending
    // (e.g. the device already has the value we wanted), there's nothing to write - report Verified.
    if (auto rs = refreshSettings(); !rs) { r.state = WriteState::Failed; r.error = rs.error(); return r; }
    if (!pending_) { r.state = WriteState::Verified; return r; }
    if (!current_) {
        r.state = WriteState::Failed;
        r.error = makeError(Errc::InvalidArgument, "no device settings read yet; refusing to write");
        return r;
    }
    if (pp.requireStableSnapshot) {
        // Two consecutive reads must agree; otherwise the device is changing under us (physical
        // control, other software) and a full-struct SET could revert that change. Nothing is sent.
        const SettingsMessage first = *current_;
        if (auto rs = refreshSettings(); !rs) { r.state = WriteState::Failed; r.error = rs.error(); return r; }
        if (!current_ || !(first == *current_)) {
            r.state = WriteState::Failed;
            r.error = makeError(Errc::Io, "device settings changed while preparing the write; "
                                            "nothing was sent - retry when the device is idle");
            return r;
        }
        if (!pending_) { r.state = WriteState::Verified; return r; }
    }
    const SettingsMessage before = *current_;
    const SettingsMessage target = *pending_;
    auto expected = diffOffsets(before, target);
    // Bytes the device updates itself in response to this write (e.g. byte 25 after Dim): accepted as changed,
    // and not required to match the request.
    std::vector<std::size_t> followers;
    for (auto o : expected)
        for (auto f : deviceFollowerOffsets(o)) followers.push_back(f);
    expected.insert(expected.end(), followers.begin(), followers.end());
    auto converged = [&] {
        for (auto o : diffOffsets(*current_, target))
            if (std::find(followers.begin(), followers.end(), o) == followers.end()) return false;
        return true;
    };

    r.state = WriteState::Submitted;
    const std::uint64_t attemptsBefore = settingsWriteAttempts_;
    Status s = commit();   // existing, tested: guard checks, write, one re-read
    r.submitted = settingsWriteAttempts_ != attemptsBefore;
    if (!s) {
        r.state = WriteState::Failed;
        r.error = s.error();
        return r;
    }
    r.state = WriteState::TransferCompleted;
    if (!current_) {   // shouldn't happen; commit() always refreshes on success
        r.state = WriteState::Failed;
        r.error = makeError(Errc::Internal, "commit succeeded but no settings snapshot afterward");
        return r;
    }
    auto bad = unexpectedOffsets(before, *current_, expected);
    if (!bad.empty()) { r.state = WriteState::Ambiguous; r.unexpectedOffsets = bad; return r; }
    if (converged()) { r.state = WriteState::Verified; return r; }

    // Not yet converged: bounded, read-only polling. The SET is never resent.
    r.state = WriteState::AwaitingConvergence;
    auto sleep = pp.sleeper ? pp.sleeper
                            : std::function<void(std::chrono::milliseconds)>(
                                  [](std::chrono::milliseconds d) { std::this_thread::sleep_for(d); });
    for (auto d : pp.delays) {
        sleep(d);
        ++r.pollAttempts;
        Status rs = refreshSettings();
        if (!rs) { r.state = WriteState::Failed; r.error = rs.error(); return r; }
        if (!current_) {
            r.state = WriteState::Failed;
            r.error = makeError(Errc::Disconnected, "lost settings snapshot during verification");
            return r;
        }
        bad = unexpectedOffsets(before, *current_, expected);
        if (!bad.empty()) { r.state = WriteState::Ambiguous; r.unexpectedOffsets = bad; return r; }
        if (converged()) { r.state = WriteState::Verified; return r; }
    }
    r.state = WriteState::VerificationTimedOut;
    return r;
}

Status DeviceSession::sendTrigger(Trigger t) {
    if (!writesEnabled())
        return makeError(Errc::WritesDisabled, "hardware writes are disabled until the protocol is verified");
    auto b = TriggerMessage(t).bytes();
    // The guard first (factory reset never, mode switches never on hardware, LED reset only as a persistent
    // change), then the per-trigger hardware authorization.
    if (auto g = checkWriteAllowed(MessageId::Triggers, b, WriteGuardContext{transport_->isSimulated(),
                                                                              policy_.allowPersistentChanges}); !g)
        return g;
    if (!transport_->isSimulated() &&
        std::find(policy_.hardwareTriggers.begin(), policy_.hardwareTriggers.end(), t) == policy_.hardwareTriggers.end())
        return makeError(Errc::WritesDisabled, "this device action is not authorized on real hardware");
    if (auto s = transport_->write(MessageId::Triggers, b); !s) { auto st = fail(s.error()); noteResult(st); return st; }
    failures_ = 0;
    return okStatus();
}

}  // namespace lewitt
