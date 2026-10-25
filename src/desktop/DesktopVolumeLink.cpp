#include "desktop/DesktopVolumeLink.hpp"

#include <cmath>
#include <cstdio>

namespace lewitt {

namespace {
std::string fmt(const char* f, double a, bool b) {
    char buf[96];
    std::snprintf(buf, sizeof buf, f, a, b ? "muted" : "unmuted");
    return buf;
}
}  // namespace

DesktopVolumeLink::DesktopVolumeLink(DeviceSession& session, IDesktopMixer& mixer, VolumeSyncConfig cfg,
                                     LinkOptions opt, Logger log)
    : session_(session), mixer_(mixer), sync_(cfg), opt_(std::move(opt)), log_(std::move(log)) {}

std::optional<OutputLevel> DesktopVolumeLink::readHardware() {
    if (auto s = session_.refreshSettings(); !s || !session_.settings()) {
        healthy_ = false;
        log("device read failed: " + (s ? std::string("no settings") : s.error().message));
        return std::nullopt;
    }
    return outputLevel(*session_.settings(), Output::Speaker);
}

bool DesktopVolumeLink::start(Clock::time_point now) {
    healthy_ = true;
    auto hw = readHardware();
    if (!hw) return false;
    auto desk = mixer_.read();
    auto a = sync_.start(*hw, desk, now);
    log(fmt("start: hardware speaker %.2f dB %s", hw->db, hw->muted));
    apply(a, now);
    endStep();
    return true;
}

void DesktopVolumeLink::onDesktopEvent(Clock::time_point now) {
    beginStep();
    // Fresh device state first: the device may already have followed the desktop by itself, or
    // the knob may have moved since the last poll. Both are adopted before the desktop is judged.
    if (sync_.phase() != SyncPhase::Writing) {
        auto hw = readHardware();
        if (!hw) return;
        apply(sync_.onHardware(*hw, now), now);
    }
    // Read the desktop only now: the hardware step may have re-aligned it (after an unverified
    // write, or a knob change). Judging a value read before that would re-send an old request,
    // i.e. retry a write whose outcome was unknown.
    auto desk = mixer_.read();
    if (!desk) return;
    apply(sync_.onDesktop(*desk, now), now);
    endStep();
}

void DesktopVolumeLink::onPollTick(Clock::time_point now) {
    if (sync_.phase() == SyncPhase::Unknown) return;
    beginStep();
    if (auto hw = readHardware()) apply(sync_.onHardware(*hw, now), now);
    else return;
    apply(sync_.tick(now), now);
    endStep();
}

void DesktopVolumeLink::onHardwareSample(const OutputLevel& hw, Clock::time_point now) {
    if (sync_.phase() == SyncPhase::Unknown) return;
    beginStep();
    apply(sync_.onHardware(hw, now), now);
    apply(sync_.tick(now), now);
    endStep();
}

void DesktopVolumeLink::beginStep() {
    if (!session_.pendingSettings()) rejectedForPending_ = false;
}

void DesktopVolumeLink::endStep() {
    const auto& d = sync_.desktop();
    const auto& h = sync_.hardware();
    if (notFollowed_ && d && h && d->muted == h->muted && std::fabs(d->db - h->db) <= sync_.config().toleranceDb)
        notFollowed_ = false;                 // back in step (device followed later, or desktop moved back)
    if (rejectedForPending_)
        notice_ = "A desktop volume change was not applied because another OpenWitty change is still "
                  "unconfirmed (retry or discard it). The desktop shows the speaker level again.";
    else if (notFollowed_)
        notice_ = "The CONNECT 2 did not follow the desktop volume: its standard volume control is not "
                  "responding in this state. OpenWitty's vendor fallback can apply desktop changes instead.";
    else if (d && h && d->muted && !h->muted)
        notice_ = "The desktop shows the speaker as muted, but the CONNECT 2 speaker is not muted. "
                  "Unmute in the desktop or in OpenWitty to bring them back in step.";
    else
        notice_.clear();
}

void DesktopVolumeLink::apply(const SyncAction& a, Clock::time_point now) {
    if (!a.note.empty() && !a.empty()) log(a.note);
    // The device first: when a desktop correction accompanies a write (a limited increase), the
    // correction is only written once the device really is at that level.
    if (a.writeSpeaker) writeDevice(*a.writeSpeaker, now);
    if (a.setDesktop) alignDesktop(*a.setDesktop, now);
}

// Re-align the desktop control to the device. An ALSA control write is NOT side-effect free: with a
// working Feature Unit 2 the kernel forwards it to the device. So the alignment
//   * re-reads the device right before writing and never writes a level the device no longer has
//     (a knob that just moved down must not be "restored" upwards),
//   * rounds down to the control's grid (never louder than the device),
//   * never unmutes (whether the ALSA switch and the vendor mute are the same physical mute is not
//     established), and
//   * skips the write when the control already shows the level.
void DesktopVolumeLink::alignDesktop(const DesktopLevel& target, Clock::time_point now) {
    if (!opt_.desktopWrites) {
        log(fmt("dry run: would set desktop to %.2f dB %s", target.db, target.muted));
        sync_.noteDesktopUnchanged(mixer_.read());
        return;
    }
    auto hw = readHardware();
    if (!hw) return;
    const double tol = sync_.config().toleranceDb;
    if (hw->muted != target.muted || std::fabs(hw->db - target.db) > tol) {
        log("desktop alignment skipped: the device changed meanwhile");
        sync_.noteDesktopUnchanged(mixer_.read());
        if (alignDepth_ < 1) {                // adopt the newer device state once (bounded)
            ++alignDepth_;
            apply(sync_.onHardware(*hw, now), now);
            --alignDepth_;
        }
        return;
    }
    auto cur = mixer_.read();
    DesktopLevel w = target;
    if (cur && cur->muted && !w.muted) {
        w.muted = true;
        log("desktop left muted: an alignment never unmutes");
    }
    if (cur && cur->muted == w.muted && std::fabs(cur->db - w.db) <= 0.25) {
        sync_.noteDesktopWritten(*cur, now);   // already shows it: nothing to write
        return;
    }
    if (auto s = mixer_.write(w); !s) {
        log("desktop update failed: " + s.error().message);
        sync_.noteDesktopUnchanged(mixer_.read());
        return;
    }
    ++desktopWrites_;
    auto actual = mixer_.read();
    sync_.noteDesktopWritten(actual ? *actual : w, now);
}

void DesktopVolumeLink::writeDevice(const OutputLevel& target, Clock::time_point now) {
    if (opt_.deviceWrites == DeviceWriteMode::None) {
        log(fmt("dry run: would write speaker %.2f dB %s", target.db, target.muted));
        sync_.abandonWrite();
        return;
    }
    if (opt_.deviceWrites == DeviceWriteMode::Native) {
        // The fresh read before this decision showed the device did NOT follow the kernel's request.
        // Native mode never corrects that by itself; it says so once.
        if (!notFollowed_) log(fmt("the device did not follow the desktop change (wanted %.2f dB %s); "
                                   "no vendor write in native mode", target.db, target.muted));
        notFollowed_ = true;
        sync_.abandonWrite();
        return;
    }
    if (session_.pendingSettings()) {        // another client's unconfirmed edit: never piggyback on it
        log("desktop change rejected: another unconfirmed edit is pending in this session");
        sync_.abandonWrite();
        rejectedForPending_ = true;
        apply(sync_.realignDesktop(now, "desktop re-aligned to the device after a rejected change"), now);
        return;
    }
    // Stage against the snapshot DeviceSession holds (commitAndVerify re-reads before the SET).
    auto e = session_.edit([&](SettingsMessage& m) { return stageOutput(m, Output::Speaker, target.db, target.muted); });
    if (!e) {
        log("speaker write not staged: " + e.error().message);
        apply(sync_.onWriteResult(WriteState::Failed, std::nullopt, now), now);
        return;
    }
    if (!session_.dirty()) {                 // device already at the target: nothing to send
        session_.discardPending();
        apply(sync_.onWriteResult(WriteState::Verified, target, now), now);
        return;
    }
    WriteResult r = session_.commitAndVerify(opt_.verify);
    if (r.state != WriteState::Verified) session_.discardPending();
    std::optional<OutputLevel> after;
    if (session_.settings()) after = outputLevel(*session_.settings(), Output::Speaker);
    ++writes_;
    log(fmt("speaker write %.2f dB %s: ", target.db, target.muted) + writeStateName(r.state));
    if (r.state == WriteState::Failed) healthy_ = session_.state() == ConnectionState::Connected;
    apply(sync_.onWriteResult(r.state, after, now), now);
}

}  // namespace lewitt
