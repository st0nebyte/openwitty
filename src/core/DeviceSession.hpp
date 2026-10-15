// Device session: owns a transport, tracks state, enforces the write policy.
// Qt-free; synchronous (callers run it on a worker thread).
#pragma once
#include <memory>
#include <optional>
#include <vector>

#include "core/Policy.hpp"
#include "core/WriteVerification.hpp"
#include "protocol/WriteGuard.hpp"
#include "protocol/Messages.hpp"
#include "transport/IDeviceTransport.hpp"

namespace lewitt {

enum class ConnectionState { Disconnected, Connecting, Connected, Error };

struct WritePolicy {
    // Routing-changing edits (streaming modes) additionally require this.
    bool allowRoutingChanges{false};
    // Persistent device-side changes such as resetting LED colours.
    bool allowPersistentChanges{false};
    // Real-hardware writes need this AND a transport that allows writes.
    // Simulated transports are always writable.
    bool allowHardwareWrites{false};
    // Phase 6E: on real (non-simulated) hardware a settings write may only change bytes whose
    // offsets are listed here, in addition to every other guard. Empty (the default) means "no
    // settings offset is authorized on hardware": allowHardwareWrites alone is not enough. Each
    // experiment names exactly the offsets it was approved for (LED brightness: {37};
    // instrument mute: {12}). Simulated transports ignore this list.
    std::vector<std::size_t> hardwareWriteOffsets;
};
// What the handshake established about the device.
struct DeviceCapabilities {
    std::uint8_t apiMajor{0}, apiMinor{0};
    std::uint16_t pid{0};
    DeviceMode mode{DeviceMode::Unknown};
    bool simulated{false};
};

class DeviceSession {
public:
    // Consecutive timeouts / I/O failures before the link is declared dead (state Error,
    // transport closed, reconnect expected). Stalls never count: a stall is a rejected
    // request, not a lost device.
    static constexpr int kMaxConsecutiveFailures = 3;
    DeviceSession(std::unique_ptr<IDeviceTransport> t, WritePolicy p = {});

    ConnectionState state() const { return state_; }
    const Error& lastError() const { return lastError_; }
    DeviceInfo info() const { return transport_->info(); }
    bool isSimulated() const { return transport_->isSimulated(); }
    bool writesEnabled() const { return transport_->isSimulated() || policy_.allowHardwareWrites; }

    Status connect();          // open + initial sync (read-only)
    void disconnect();
    // Close + reopen + handshake + re-sync. Unconfirmed user edits survive (merged with
    // whatever changed on the device in the meantime; device values win).
    Status reconnect();
    // Thread-safe: abort the in-flight request (shutdown) / allow requests again.
    void cancel() { transport_->requestCancel(); }
    void clearCancel() { transport_->clearCancel(); }
    const DeviceCapabilities& capabilities() const { return caps_; }
    int consecutiveFailures() const { return failures_; }
    bool needsReconnect() const { return state_ == ConnectionState::Error || state_ == ConnectionState::Disconnected; }
    Status refreshSettings();  // read settings -> current & pending
    Status refreshStatus();    // read status (meters)

    const std::optional<SettingsMessage>& settings() const { return current_; }
    const std::optional<StatusMessage>& status() const { return status_; }
    const std::optional<ApiVersionMessage>& apiVersion() const { return api_; }

    // Stage-edit-commit. Edits apply to a copy of the last read settings so
    // unknown bytes round-trip untouched. Nothing is sent until commit().
    template <class F>
    Status edit(F&& f) {
        if (!current_) return makeError(Errc::InvalidArgument, "no settings read yet");
        SettingsMessage copy = pending_ ? *pending_ : *current_;
        if (auto s = f(copy); !s) return s;
        pending_ = copy;
        return okStatus();
    }
    // Sample rate comes from the audio stack, not the control protocol. 0 = unknown
    // (restrictions are then NOT applied and the UI must say so).
    void setSampleRateHint(std::uint32_t hz) { sampleRate_ = hz; }
    std::uint32_t sampleRate() const { return sampleRate_; }
    void discardPending() { pending_.reset(); }
    bool dirty() const { return pending_ && current_ && !(*pending_ == *current_); }
    // What the UI should show: the user's unconfirmed edits on top of the device state.
    std::optional<SettingsMessage> effectiveSettings() const { return pending_ ? pending_ : current_; }
    const std::optional<SettingsMessage>& pendingSettings() const { return pending_; }
    Status commit();           // write pending settings (policy gated), then re-read
    // Like commit(), but doesn't stop at "the USB transfer didn't error": bounded, read-only
    // polling (see WriteVerification.hpp) confirms the expected bytes actually changed before
    // reporting Verified. Never retries the SET itself. Safe to call with no pending edit (returns
    // Verified immediately, no I/O). On entry, captures the exact set of offsets the edit is
    // expected to change; any other offset changing during verification is reported as Ambiguous,
    // not silently accepted or overwritten.
    WriteResult commitAndVerify(PollPolicy pp = {});
    Status sendTrigger(Trigger t);

private:
    Status fail(Error e);
    std::unique_ptr<IDeviceTransport> transport_;
    WritePolicy policy_;
    ConnectionState state_{ConnectionState::Disconnected};
    Error lastError_;
    std::optional<SettingsMessage> current_, pending_;
    std::optional<StatusMessage> status_;
    std::optional<ApiVersionMessage> api_;
    DeviceCapabilities caps_;
    int failures_{0};
    Status handshake();       // open + api version + capability checks (no settings)
    void noteResult(const Status& s);
    std::uint32_t sampleRate_{0};
};

}  // namespace lewitt
