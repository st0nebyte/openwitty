// Phase 8D: VolumeSync authority policy (pure; simulated clock).
#include <gtest/gtest.h>

#include <limits>

#include "core/VolumeSync.hpp"

using namespace lewitt;
using namespace std::chrono_literals;

namespace {
using TP = VolumeSync::TimePoint;
const TP t0 = TP{} + 1000s;
OutputLevel hw(double db, bool muted = false) { return {db, muted}; }
DesktopLevel desk(double db, bool muted = false) { return {db, muted}; }

// A sync that has finished its startup hold, baseline -30 dB unmuted on both sides.
VolumeSync synced(VolumeSyncConfig c = {}) {
    VolumeSync s(c);
    s.start(hw(-30), desk(-30), t0);
    return s;
}
const TP afterHold = t0 + 6s;
}  // namespace

TEST(VolumeSync, StartNeverWritesTheDeviceAndAlignsTheDesktop) {
    for (auto d : {desk(0), desk(-60), desk(-30, true), desk(-10)}) {
        VolumeSync s;
        auto a = s.start(hw(-30), d, t0);
        EXPECT_FALSE(a.writeSpeaker.has_value());
        ASSERT_TRUE(a.setDesktop.has_value());
        EXPECT_DOUBLE_EQ(a.setDesktop->db, -30);
        EXPECT_FALSE(a.setDesktop->muted);
        EXPECT_EQ(s.phase(), SyncPhase::Synced);
    }
    VolumeSync s;
    auto a = s.start(hw(-30, true), std::nullopt, t0);           // desktop unreadable: still read-only
    EXPECT_FALSE(a.writeSpeaker.has_value());
}

TEST(VolumeSync, LoginRouteRestoreDuringStartupHoldDoesNotRaiseTheDevice) {
    VolumeSync s;
    s.start(hw(-40), desk(-40), t0);
    auto a = s.onDesktop(desk(0), t0 + 1500ms);                 // WirePlumber restores an old 100 % route volume
    EXPECT_FALSE(a.writeSpeaker.has_value());
    ASSERT_TRUE(a.setDesktop.has_value());
    EXPECT_DOUBLE_EQ(a.setDesktop->db, -40);
    EXPECT_DOUBLE_EQ(s.hardware()->db, -40);
}

TEST(VolumeSync, DesktopDecreaseIsAppliedUnchanged) {
    auto s = synced();
    auto a = s.onDesktop(desk(-45), afterHold);
    ASSERT_TRUE(a.writeSpeaker.has_value());
    EXPECT_DOUBLE_EQ(a.writeSpeaker->db, -45);
    EXPECT_FALSE(a.setDesktop.has_value());
    EXPECT_EQ(s.phase(), SyncPhase::Writing);
}

TEST(VolumeSync, LargeIncreaseIsLimitedAndTheDesktopShowsWhatWasApplied) {
    auto s = synced();
    auto a = s.onDesktop(desk(0), afterHold);
    ASSERT_TRUE(a.writeSpeaker.has_value());
    EXPECT_DOUBLE_EQ(a.writeSpeaker->db, -20);                  // -30 + 10 dB maximum step
    ASSERT_TRUE(a.setDesktop.has_value());
    EXPECT_DOUBLE_EQ(a.setDesktop->db, -20);
    s.onWriteResult(WriteState::Verified, hw(-20), afterHold + 10ms);
    // the echo of our desktop correction is not a new request
    auto e = s.onDesktop(desk(-20), afterHold + 30ms);
    EXPECT_TRUE(e.empty());
    // a second, deliberate move continues in bounded steps
    auto b = s.onDesktop(desk(0), afterHold + 500ms);
    ASSERT_TRUE(b.writeSpeaker.has_value());
    EXPECT_DOUBLE_EQ(b.writeSpeaker->db, -10);
}

TEST(VolumeSync, MaxIncreaseZeroNeverRaises) {
    VolumeSyncConfig c;
    c.maxIncreaseDb = 0;
    auto s = synced(c);
    auto a = s.onDesktop(desk(-10), afterHold);
    EXPECT_FALSE(a.writeSpeaker.has_value());
    ASSERT_TRUE(a.setDesktop.has_value());                      // desktop snapped back to the real level
    EXPECT_DOUBLE_EQ(a.setDesktop->db, -30);
}

TEST(VolumeSync, MuteIsAppliedImmediatelyAndUnmuteRestoresTheSameLevel) {
    auto s = synced();
    auto m = s.onDesktop(desk(-30, true), afterHold);
    ASSERT_TRUE(m.writeSpeaker.has_value());
    EXPECT_TRUE(m.writeSpeaker->muted);
    EXPECT_DOUBLE_EQ(m.writeSpeaker->db, -30);
    s.onWriteResult(WriteState::Verified, hw(-30, true), afterHold + 5ms);
    auto u = s.onDesktop(desk(-30, false), afterHold + 400ms);
    ASSERT_TRUE(u.writeSpeaker.has_value());
    EXPECT_FALSE(u.writeSpeaker->muted);
    EXPECT_DOUBLE_EQ(u.writeSpeaker->db, -30);
}

TEST(VolumeSync, KnobChangeIsMirroredAndEchoDoesNotLoop) {
    auto s = synced();
    auto a = s.onHardware(hw(-18), afterHold);
    EXPECT_FALSE(a.writeSpeaker.has_value());
    ASSERT_TRUE(a.setDesktop.has_value());
    EXPECT_DOUBLE_EQ(a.setDesktop->db, -18);
    // PipeWire/ALSA echo our update back, possibly rounded to the 0.5 dB grid
    EXPECT_TRUE(s.onDesktop(desk(-18.0), afterHold + 20ms).empty());
    EXPECT_TRUE(s.onDesktop(desk(-18.2), afterHold + 40ms).empty());
    EXPECT_TRUE(s.onHardware(hw(-18), afterHold + 1s).empty());
}

TEST(VolumeSync, KnobChangeDropsAnOlderQueuedDesktopRequest) {
    auto s = synced();
    s.onDesktop(desk(-35), afterHold);                          // write #1 in flight
    s.onDesktop(desk(-36), afterHold + 10ms);                   // queued behind it
    EXPECT_TRUE(s.hasQueuedRequest());
    s.onWriteResult(WriteState::Verified, hw(-35), afterHold + 20ms);
    s.onHardware(hw(-25), afterHold + 60ms);                    // knob turned afterwards
    EXPECT_FALSE(s.hasQueuedRequest());
    EXPECT_TRUE(s.tick(afterHold + 1s).writeSpeaker == std::nullopt);
    EXPECT_DOUBLE_EQ(s.hardware()->db, -25);
}

TEST(VolumeSync, DesktopMoveAfterKnobWins) {
    auto s = synced();
    s.onHardware(hw(-25), afterHold);
    auto a = s.onDesktop(desk(-28), afterHold + 3s);            // after the echo window: a real move
    ASSERT_TRUE(a.writeSpeaker.has_value());
    EXPECT_DOUBLE_EQ(a.writeSpeaker->db, -28);
}

TEST(VolumeSync, SliderDragIsCoalescedToTheNewestValue) {
    auto s = synced();
    auto first = s.onDesktop(desk(-29), afterHold);
    ASSERT_TRUE(first.writeSpeaker.has_value());
    for (double v : {-28.5, -28.0, -27.5, -27.0}) EXPECT_FALSE(s.onDesktop(desk(v), afterHold + 20ms).writeSpeaker);
    s.onWriteResult(WriteState::Verified, hw(-29), afterHold + 50ms);
    EXPECT_FALSE(s.tick(afterHold + 100ms).writeSpeaker);       // still inside the 150 ms rate limit
    auto next = s.tick(afterHold + 200ms);
    ASSERT_TRUE(next.writeSpeaker.has_value());
    EXPECT_DOUBLE_EQ(next.writeSpeaker->db, -27.0);             // only the newest value is written
}

TEST(VolumeSync, UnverifiedWriteIsNeverRetriedAndHardwareBecomesTheTruthAgain) {
    for (auto st : {WriteState::VerificationTimedOut, WriteState::Ambiguous, WriteState::Failed}) {
        auto s = synced();
        s.onDesktop(desk(-25), afterHold);
        auto r = s.onWriteResult(st, std::nullopt, afterHold + 600ms);
        EXPECT_FALSE(r.writeSpeaker.has_value());
        EXPECT_EQ(s.phase(), SyncPhase::Uncertain);
        EXPECT_FALSE(s.onDesktop(desk(-20), afterHold + 700ms).writeSpeaker);   // no writes while uncertain
        EXPECT_FALSE(s.tick(afterHold + 2s).writeSpeaker);
        auto h = s.onHardware(hw(-30), afterHold + 2s);                         // device did not follow
        EXPECT_EQ(s.phase(), SyncPhase::Synced);
        EXPECT_FALSE(h.writeSpeaker.has_value());
        ASSERT_TRUE(h.setDesktop.has_value());
        EXPECT_DOUBLE_EQ(h.setDesktop->db, -30);                                // desktop shows reality
    }
}

TEST(VolumeSync, NoBaselineMeansNoAction) {
    VolumeSync s;
    EXPECT_TRUE(s.onDesktop(desk(-10), t0).empty());
    EXPECT_TRUE(s.onHardware(hw(-10), t0).empty());
    EXPECT_TRUE(s.tick(t0).empty());
    auto ok = synced();
    ok.reset();                                                  // device or card lost
    EXPECT_TRUE(ok.onDesktop(desk(-10), afterHold).empty());
    auto again = ok.start(hw(-30), desk(-10), afterHold);        // reconnect: read-only again
    EXPECT_FALSE(again.writeSpeaker.has_value());
}

TEST(VolumeSync, NonFiniteDesktopValueIsIgnored) {
    auto s = synced();
    auto a = s.onDesktop(desk(std::numeric_limits<double>::quiet_NaN()), afterHold);
    EXPECT_FALSE(a.writeSpeaker.has_value());
    EXPECT_EQ(s.phase(), SyncPhase::Synced);
}

TEST(VolumeSync, DryRunAbandonKeepsTheBaseline) {
    auto s = synced();
    s.onDesktop(desk(-25), afterHold);
    s.abandonWrite();
    EXPECT_EQ(s.phase(), SyncPhase::Synced);
    EXPECT_DOUBLE_EQ(s.hardware()->db, -30);
}
