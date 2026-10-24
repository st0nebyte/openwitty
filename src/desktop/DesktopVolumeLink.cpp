#include "desktop/DesktopVolumeLink.hpp"

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
    return true;
}

void DesktopVolumeLink::onDesktopEvent(Clock::time_point now) {
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
}

void DesktopVolumeLink::onPollTick(Clock::time_point now) {
    if (sync_.phase() == SyncPhase::Unknown) return;
    if (auto hw = readHardware()) apply(sync_.onHardware(*hw, now), now);
    else return;
    apply(sync_.tick(now), now);
}

void DesktopVolumeLink::onHardwareSample(const OutputLevel& hw, Clock::time_point now) {
    if (sync_.phase() == SyncPhase::Unknown) return;
    apply(sync_.onHardware(hw, now), now);
    apply(sync_.tick(now), now);
}

void DesktopVolumeLink::apply(const SyncAction& a, Clock::time_point now) {
    if (!a.note.empty() && !a.empty()) log(a.note);
    if (a.setDesktop) {
        if (opt_.desktopWrites) {
            if (auto s = mixer_.write(*a.setDesktop); s) ++desktopWrites_;
            else log("desktop update failed: " + s.error().message);
        } else {
            log(fmt("dry run: would set desktop to %.2f dB %s", a.setDesktop->db, a.setDesktop->muted));
        }
    }
    if (!a.writeSpeaker) return;
    const OutputLevel target = *a.writeSpeaker;
    if (!opt_.hardwareWrites) {
        log(fmt("dry run: would write speaker %.2f dB %s", target.db, target.muted));
        sync_.abandonWrite();
        return;
    }
    if (session_.pendingSettings()) {        // another client's unconfirmed edit: never piggyback on it
        log("speaker write deferred: another unconfirmed edit is pending in this session");
        sync_.abandonWrite();
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
