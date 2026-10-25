// Speaker-volume synchronization policy between the desktop (ALSA "PCM Playback" control that
// PipeWire/KDE move) and the physical CONNECT 2 speaker output. Pure decision logic: no I/O, no
// clock of its own, so every transition is unit-testable.
//
// Authority policy (who may change what):
//   start / reconnect / service restart  READ-ONLY. The hardware level is the truth; the desktop
//                                        control may be re-aligned to it, the device is never
//                                        written. A startup hold window additionally treats early
//                                        desktop events (WirePlumber's route restore at login) as
//                                        non-authoritative: they re-align the desktop, not the device.
//   desktop change (KDE slider / mute)   authoritative after the hold window: written to the device,
//                                        a volume increase is limited to maxIncreaseDb above the
//                                        current hardware level per step (the desktop is re-aligned
//                                        to what was actually applied); mute is applied at once.
//   hardware change (knob, another tool) authoritative: adopted as the new truth, mirrored to the
//                                        desktop, and any older queued desktop request is dropped.
//   write outcome unknown (timeout /     no retry. The state becomes Uncertain; nothing is written
//   ambiguous / failed)                  until the next hardware read re-establishes the truth.
//   echo of our own desktop update       ignored (prevents feedback loops).
#pragma once
#include <chrono>
#include <optional>
#include <string>

#include "core/OutputControl.hpp"
#include "core/WriteVerification.hpp"

namespace lewitt {

// The desktop side: ALSA "PCM Playback Volume" in dB and "PCM Playback Switch" (muted = switch off).
struct DesktopLevel {
    double db{-60.0};
    bool muted{true};
};

struct VolumeSyncConfig {
    double maxIncreaseDb{kMaxOutputIncreaseDb};         // per desktop-originated step
    double toleranceDb{0.3};                            // levels closer than this are "equal" (0.5 dB grid)
    std::chrono::milliseconds startupHold{5000};        // after start(): desktop is not authoritative
    std::chrono::milliseconds minWriteInterval{150};    // rate limit; newer requests replace queued ones
    std::chrono::milliseconds echoWindow{2000};         // how long our own desktop update may echo back
    bool mirrorHardwareToDesktop{true};                 // show knob changes in KDE
};

enum class SyncPhase { Unknown, Synced, Writing, Uncertain };

struct SyncAction {
    std::optional<OutputLevel> writeSpeaker;   // the complete target (level + mute) for the device
    std::optional<DesktopLevel> setDesktop;    // re-align the desktop control to this
    std::string note;                          // why (for logs); empty = nothing happened
    bool empty() const { return !writeSpeaker && !setDesktop; }
};

class VolumeSync {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit VolumeSync(VolumeSyncConfig c = {}) : cfg_(c) {}

    // Baseline after (re)connect / service start. Never produces a hardware write.
    SyncAction start(OutputLevel hardware, std::optional<DesktopLevel> desktop, TimePoint now);
    SyncAction onDesktop(DesktopLevel d, TimePoint now);
    SyncAction onHardware(OutputLevel hw, TimePoint now);
    // Result of executing a previously returned writeSpeaker. `hwAfter` is the device state read
    // back after the write (if any read succeeded).
    SyncAction onWriteResult(WriteState st, std::optional<OutputLevel> hwAfter, TimePoint now);
    // A returned write was intentionally not executed (dry-run): return to Synced, hardware unchanged.
    void abandonWrite();
    // Flush a rate-limited desktop request once the interval has passed.
    SyncAction tick(TimePoint now);
    // Device or desktop control lost: forget everything; the next start() re-baselines.
    void reset();
    // Ask for the desktop to be re-aligned to the hardware level (e.g. a desktop change that could
    // not be applied). Empty when there is nothing to align.
    SyncAction realignDesktop(TimePoint now, const char* why);
    // The executor reports what the desktop control actually shows after (or instead of) a returned
    // setDesktop: written possibly with safety adjustments (rounded down, left muted), or skipped.
    void noteDesktopWritten(DesktopLevel actual, TimePoint now);
    void noteDesktopUnchanged(std::optional<DesktopLevel> actual);

    SyncPhase phase() const { return phase_; }
    const std::optional<OutputLevel>& hardware() const { return hw_; }
    const std::optional<DesktopLevel>& desktop() const { return desk_; }
    bool hasQueuedRequest() const { return queued_.has_value(); }
    const VolumeSyncConfig& config() const { return cfg_; }

private:
    bool sameLevel(double a, double b) const;
    bool desktopMatchesHardware(const DesktopLevel& d, const OutputLevel& h) const;
    bool isEcho(const DesktopLevel& d, TimePoint now) const;
    SyncAction alignDesktop(TimePoint now, std::string why);
    SyncAction evaluate(const DesktopLevel& d, TimePoint now);

    VolumeSyncConfig cfg_;
    SyncPhase phase_{SyncPhase::Unknown};
    std::optional<OutputLevel> hw_;
    std::optional<DesktopLevel> desk_;
    std::optional<OutputLevel> inFlight_;
    std::optional<DesktopLevel> queued_;
    std::optional<DesktopLevel> echo_;
    TimePoint echoUntil_{};
    TimePoint holdUntil_{};
    std::optional<TimePoint> lastWrite_;
};

}  // namespace lewitt
