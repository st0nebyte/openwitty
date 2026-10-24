// Glue between the desktop mixer, the device session and the VolumeSync policy. Qt-free and
// non-blocking per call; the daemon (and later the GUI worker) drives it from its own loop.
//
// The link is the only component that turns policy decisions into I/O:
//   * it reads the device (fresh settings GET) before every decision that could lead to a write,
//     so a device that already followed the desktop by itself (healthy Feature Unit 2) is not
//     written again and is never "corrected" towards an older value;
//   * hardware writes go through stageOutput() + DeviceSession::commitAndVerify(): fresh snapshot,
//     exact-byte scope (speaker bytes only), one SET, read-back verification, never retried;
//   * with writes disabled (dry run) decisions are only reported;
//   * while the session holds another client's unconfirmed edit (the GUI keeps failed edits pending
//     for the user to retry or discard) the link never stages on top of it: it writes nothing.
#pragma once
#include <functional>
#include <string>

#include "core/DeviceSession.hpp"
#include "core/VolumeSync.hpp"
#include "desktop/DesktopMixer.hpp"

namespace lewitt {

struct LinkOptions {
    bool hardwareWrites{false};   // false = dry run: nothing is written to the device
    bool desktopWrites{true};     // re-align the ALSA control (no physical change); off in dry run
    PollPolicy verify{};
};

class DesktopVolumeLink {
public:
    using Clock = VolumeSync::Clock;
    using Logger = std::function<void(const std::string&)>;

    DesktopVolumeLink(DeviceSession& session, IDesktopMixer& mixer, VolumeSyncConfig cfg, LinkOptions opt,
                      Logger log = {});

    // Read hardware and desktop, establish the baseline. Never writes the device.
    bool start(Clock::time_point now);
    // Desktop control event(s) arrived.
    void onDesktopEvent(Clock::time_point now);
    // Periodic hardware read (knob changes) plus rate-limit flush.
    void onPollTick(Clock::time_point now);
    // Same as onPollTick() for a host that has just read the settings itself (the GUI worker):
    // no additional device I/O.
    void onHardwareSample(const OutputLevel& hw, Clock::time_point now);
    void reset() { sync_.reset(); }

    const VolumeSync& sync() const { return sync_; }
    int hardwareWrites() const { return writes_; }
    int desktopWrites() const { return desktopWrites_; }
    // False once the device session failed (the caller reconnects and calls start() again).
    bool healthy() const { return healthy_; }

private:
    std::optional<OutputLevel> readHardware();
    void apply(const SyncAction& a, Clock::time_point now);
    void log(const std::string& s) const {
        if (log_) log_(s);
    }

    DeviceSession& session_;
    IDesktopMixer& mixer_;
    VolumeSync sync_;
    LinkOptions opt_;
    Logger log_;
    int writes_{0};
    int desktopWrites_{0};
    bool healthy_{true};
};

}  // namespace lewitt
