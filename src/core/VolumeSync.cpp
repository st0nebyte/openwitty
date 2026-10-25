#include "core/VolumeSync.hpp"

#include <cmath>
#include <cstdio>

namespace lewitt {

bool VolumeSync::sameLevel(double a, double b) const { return std::fabs(a - b) <= cfg_.toleranceDb; }

bool VolumeSync::desktopMatchesHardware(const DesktopLevel& d, const OutputLevel& h) const {
    return d.muted == h.muted && sameLevel(d.db, h.db);
}

bool VolumeSync::isEcho(const DesktopLevel& d, TimePoint now) const {
    return echo_ && now <= echoUntil_ && echo_->muted == d.muted && sameLevel(echo_->db, d.db);
}

SyncAction VolumeSync::alignDesktop(TimePoint now, std::string why) {
    SyncAction a;
    if (!hw_ || !cfg_.mirrorHardwareToDesktop) return a;
    if (desk_ && desktopMatchesHardware(*desk_, *hw_)) return a;
    DesktopLevel target{hw_->db, hw_->muted};
    a.setDesktop = target;
    a.note = std::move(why);
    echo_ = target;
    echoUntil_ = now + cfg_.echoWindow;
    desk_ = target;
    return a;
}

SyncAction VolumeSync::start(OutputLevel hardware, std::optional<DesktopLevel> desktop, TimePoint now) {
    phase_ = SyncPhase::Synced;
    hw_ = hardware;
    desk_ = desktop;
    inFlight_.reset();
    queued_.reset();
    echo_.reset();
    lastWrite_.reset();
    holdUntil_ = now + cfg_.startupHold;
    auto a = alignDesktop(now, "start: desktop re-aligned to the hardware level (read-only start)");
    if (a.empty()) a.note = "start: hardware and desktop agree (read-only start)";
    return a;
}

SyncAction VolumeSync::evaluate(const DesktopLevel& d, TimePoint now) {
    SyncAction a;
    const OutputLevel& h = *hw_;
    OutputLevel target = h;
    bool clamped = false;
    if (d.muted != h.muted) target.muted = d.muted;
    if (!sameLevel(d.db, h.db)) {
        auto requested = normalizeOutputDb(d.db);
        if (!requested) {
            a.note = "desktop: non-finite level ignored";
            return a;
        }
        double limited = limitIncrease(h.db, *requested, cfg_.maxIncreaseDb);
        clamped = limited < *requested;
        target.db = *normalizeOutputDb(limited);
    }
    if (target.muted == h.muted && sameLevel(target.db, h.db)) {
        if (clamped) return alignDesktop(now, "desktop: increase not applicable, desktop re-aligned");
        return a;
    }
    a.writeSpeaker = target;
    a.note = clamped ? "desktop: increase limited to the per-step maximum" : "desktop: applying desktop change";
    if (clamped) {
        DesktopLevel shown{target.db, d.muted};
        a.setDesktop = shown;
        echo_ = shown;
        echoUntil_ = now + cfg_.echoWindow;
        desk_ = shown;
    }
    inFlight_ = target;
    phase_ = SyncPhase::Writing;
    lastWrite_ = now;
    return a;
}

SyncAction VolumeSync::onDesktop(DesktopLevel d, TimePoint now) {
    SyncAction a;
    if (phase_ == SyncPhase::Unknown || !hw_) {
        a.note = "desktop: ignored, no hardware baseline yet";
        return a;
    }
    if (isEcho(d, now)) {
        desk_ = d;
        a.note = "desktop: echo of our own update ignored";
        return a;
    }
    desk_ = d;
    if (phase_ == SyncPhase::Uncertain) {
        a.note = "desktop: ignored, hardware state uncertain until the next read";
        return a;
    }
    if (now < holdUntil_) {
        auto r = alignDesktop(now, "desktop: startup hold, desktop re-aligned to the hardware level");
        if (r.empty()) r.note = "desktop: startup hold, already aligned";
        return r;
    }
    if (desktopMatchesHardware(d, *hw_)) {
        queued_.reset();
        a.note = "desktop: already matches hardware";
        return a;
    }
    if (phase_ == SyncPhase::Writing ||
        (lastWrite_ && now < *lastWrite_ + cfg_.minWriteInterval)) {
        queued_ = d;                                   // newest request replaces an older one
        a.note = "desktop: queued (write in flight or rate limit)";
        return a;
    }
    return evaluate(d, now);
}

SyncAction VolumeSync::tick(TimePoint now) {
    SyncAction a;
    if (!queued_ || phase_ != SyncPhase::Synced || !hw_) return a;
    if (lastWrite_ && now < *lastWrite_ + cfg_.minWriteInterval) return a;
    DesktopLevel d = *queued_;
    queued_.reset();
    if (desktopMatchesHardware(d, *hw_)) return a;
    return evaluate(d, now);
}

SyncAction VolumeSync::onHardware(OutputLevel hw, TimePoint now) {
    SyncAction a;
    if (phase_ == SyncPhase::Unknown) {
        a.note = "hardware: ignored, not started";
        return a;
    }
    if (phase_ == SyncPhase::Writing) {
        a.note = "hardware: ignored while our write is in flight";
        return a;
    }
    if (phase_ == SyncPhase::Uncertain) {
        phase_ = SyncPhase::Synced;
        hw_ = hw;
        queued_.reset();
        auto r = alignDesktop(now, "hardware: re-established after an uncertain write, desktop re-aligned");
        if (r.empty()) r.note = "hardware: re-established after an uncertain write";
        return r;
    }
    if (hw_ && hw.muted == hw_->muted && sameLevel(hw.db, hw_->db)) return a;
    hw_ = hw;
    queued_.reset();                                   // the device changed after the queued request
    // The source cannot be told apart from the device side: the speaker knob or Mute button, another
    // tool, or a desktop change that the kernel applied to the device.
    char lvl[64];
    std::snprintf(lvl, sizeof lvl, "%.2f dB %s", hw.db, hw.muted ? "muted" : "unmuted");
    auto r = alignDesktop(now, std::string("hardware: device speaker now ") + lvl + ", mirrored to the desktop");
    if (r.empty()) r.note = std::string("hardware: device speaker now ") + lvl + ", desktop already matches";
    return r;
}

SyncAction VolumeSync::onWriteResult(WriteState st, std::optional<OutputLevel> hwAfter, TimePoint now) {
    SyncAction a;
    if (phase_ != SyncPhase::Writing || !inFlight_) {
        a.note = "write result: no write in flight, ignored";
        return a;
    }
    const OutputLevel target = *inFlight_;
    inFlight_.reset();
    if (st == WriteState::Verified) {
        phase_ = SyncPhase::Synced;
        hw_ = hwAfter ? *hwAfter : target;
        a.note = "write verified";
        if (queued_) {
            auto next = tick(now);
            if (!next.empty()) return next;
        }
        return a;
    }
    phase_ = SyncPhase::Uncertain;                    // never retried: re-read before anything else
    queued_.reset();
    a.note = "write outcome not verified: no retry, waiting for a fresh hardware read";
    return a;
}

void VolumeSync::abandonWrite() {
    if (phase_ == SyncPhase::Writing) phase_ = SyncPhase::Synced;
    inFlight_.reset();
}

SyncAction VolumeSync::realignDesktop(TimePoint now, const char* why) {
    if (phase_ == SyncPhase::Unknown) return {};
    return alignDesktop(now, why);
}

void VolumeSync::noteDesktopWritten(DesktopLevel actual, TimePoint now) {
    desk_ = actual;
    echo_ = actual;
    echoUntil_ = now + cfg_.echoWindow;
}

void VolumeSync::noteDesktopUnchanged(std::optional<DesktopLevel> actual) {
    desk_ = actual;
    echo_.reset();
}

void VolumeSync::reset() {
    phase_ = SyncPhase::Unknown;
    hw_.reset();
    desk_.reset();
    inFlight_.reset();
    queued_.reset();
    echo_.reset();
    lastWrite_.reset();
}

}  // namespace lewitt
