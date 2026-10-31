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
#include <chrono>
#include <functional>
#include <optional>
#include <string>

#include "core/DeviceSession.hpp"
#include "core/VolumeSync.hpp"
#include "desktop/DesktopMixer.hpp"

namespace lewitt {

// Who applies a desktop (KDE/PipeWire) volume change to the device.
enum class DeviceWriteMode {
    None,      // dry run: decisions are only logged
    Native,    // default: the kernel/ALSA path is authoritative (it reaches the device when Feature Unit 2
               // honours SET_CUR, verified on an ordinary boot); the link never writes the device and
               // only reports a desktop change the device did not follow
    Fallback,  // additionally apply such a change (once its follow grace has passed) through the vendor settings message (for the verified
               // exceptional state in which the device ignores Feature Unit 2)
};

struct LinkOptions {
    DeviceWriteMode deviceWrites{DeviceWriteMode::None};
    bool desktopWrites{true};     // mirror the device level to the ALSA control (knob changes); off in dry run
    PollPolicy verify{};
    // How long the device may take to report a desktop change the kernel forwarded before it counts as
    // "did not follow" (Native: notice; Fallback: only then the vendor write). The CONNECT 2 reports the
    // new level 0.4-0.7 s after the ALSA control changes; judging a single immediate read produced false
    // notices (Native) and needless vendor writes (Fallback) during fast volume-key presses.
    std::chrono::milliseconds followGrace{1500};
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
    // Phase 12: a fresh settings snapshot; the link follows the output byte 59 maps the native volume to (speakers or
    // headphones; "none" = idle) and re-targets when that changes.
    void onSettingsSample(const SettingsMessage& s, Clock::time_point now);
    // The output the native volume is mapped to now (nullopt: "none", or not read yet).
    std::optional<Output> target() const { return target_; }
    void reset() { sync_.reset(); }

    const VolumeSync& sync() const { return sync_; }
    int hardwareWrites() const { return writes_; }
    int desktopWrites() const { return desktopWrites_; }
    // False once the device session failed (the caller reconnects and calls start() again).
    bool healthy() const { return healthy_; }
    // A user-facing explanation when the desktop and the device do not agree for a known reason
    // (empty when they agree or nothing needs explaining).
    const std::string& notice() const { return notice_; }

private:
    std::optional<OutputLevel> readHardware();
    // Byte 59 -> output; nullopt for "none" (and unknown values).
    static std::optional<Output> mappedOutput(const SettingsMessage& s);
    // Adopt a new target: KDE first shows that output's level AND mute (Phase 12B finding), then a fresh baseline.
    bool noteTarget(const SettingsMessage& s);
    void retarget(const OutputLevel& hw, Clock::time_point now);
    void apply(const SyncAction& a, Clock::time_point now);
    void writeDevice(const OutputLevel& target, Clock::time_point now);
    void alignDesktop(const DesktopLevel& target, Clock::time_point now);
    void beginStep();
    void endStep();
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
    bool rejectedForPending_{false};
    bool notFollowed_{false};          // Native mode: a desktop change the device did not follow
    struct FollowCheck {
        OutputLevel target;
        Clock::time_point deadline;
    };
    std::optional<FollowCheck> followCheck_;   // waiting for the device to report a desktop change
    void checkFollow(Clock::time_point now);
    int alignDepth_{0};
    std::optional<Output> target_{Output::Speaker};
    bool targetChanged_{false};
    bool started_{false};
    bool idleLogged_{false};
    std::string notice_;
};

}  // namespace lewitt
