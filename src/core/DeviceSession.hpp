// Device session: owns a transport, tracks state, enforces the write policy.
// Qt-free; synchronous (callers run it on a worker thread).
#pragma once
#include <memory>
#include <optional>

#include "protocol/Messages.hpp"
#include "transport/IDeviceTransport.hpp"

namespace lewitt {

enum class ConnectionState { Disconnected, Connecting, Connected, Error };

struct WritePolicy {
    // Real-hardware writes need BOTH: policy enabled here AND transport allowing.
    // Simulated transports are always writable.
    bool allowHardwareWrites{false};
};

class DeviceSession {
public:
    DeviceSession(std::unique_ptr<IDeviceTransport> t, WritePolicy p = {});

    ConnectionState state() const { return state_; }
    const Error& lastError() const { return lastError_; }
    DeviceInfo info() const { return transport_->info(); }
    bool isSimulated() const { return transport_->isSimulated(); }
    bool writesEnabled() const { return transport_->isSimulated() || policy_.allowHardwareWrites; }

    Status connect();          // open + initial sync (read-only)
    void disconnect();
    Status refreshSettings();  // read settings -> current & pending
    Status refreshStatus();    // read status (meters)

    const std::optional<SettingsMessage>& settings() const { return current_; }
    const std::optional<StatusMessage>& status() const { return status_; }

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
    bool dirty() const { return pending_ && !(*pending_ == *current_); }
    Status commit();           // write pending settings (policy gated), then re-read
    Status sendTrigger(Trigger t);

private:
    Status fail(Error e);
    std::unique_ptr<IDeviceTransport> transport_;
    WritePolicy policy_;
    ConnectionState state_{ConnectionState::Disconnected};
    Error lastError_;
    std::optional<SettingsMessage> current_, pending_;
    std::optional<StatusMessage> status_;
};

}  // namespace lewitt
