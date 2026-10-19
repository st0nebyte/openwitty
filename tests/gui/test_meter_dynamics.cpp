#include <gtest/gtest.h>

#include "MeterDynamics.hpp"
#include "MeterPresentation.hpp"

#include <array>
#include <chrono>
#include <limits>

using lewitt::MeterDynamics;
using State = MeterDynamics::SourceState;
using namespace std::chrono_literals;

namespace {
MeterDynamics::TimePoint at(int ms) { return MeterDynamics::TimePoint{} + std::chrono::milliseconds(ms); }
std::array<double, 8> all(double d) { std::array<double, 8> a{}; a.fill(d); return a; }
}

TEST(MeterDynamics, AttackReleaseAndNoOvershoot) {
    MeterDynamics m;
    m.setSourceState(State::Fresh, at(0));
    m.observe(all(-60), at(0));
    m.observe(all(-6), at(100));
    m.advance(at(120));
    const double attacked = m.channel(0).displayDb;
    EXPECT_GT(attacked, -60);
    EXPECT_LT(attacked, -6);
    EXPECT_DOUBLE_EQ(m.channel(0).peakDb, -6);
    m.advance(at(200));
    EXPECT_LE(m.channel(0).displayDb, -6);
    m.observe(all(-50), at(210));
    m.advance(at(260));
    EXPECT_GT(m.channel(0).displayDb, -50);
    EXPECT_LT(m.channel(0).displayDb, -6);
    EXPECT_DOUBLE_EQ(m.channel(0).observedDb, -50);
}

TEST(MeterDynamics, PeakHoldBoundaryAndDecay) {
    MeterDynamics m;
    m.setSourceState(State::Fresh, at(0));
    m.observe(all(-3), at(0));
    m.observe(all(-40), at(100));
    m.advance(at(749));
    EXPECT_DOUBLE_EQ(m.channel(0).peakDb, -3);
    m.advance(at(750));
    EXPECT_DOUBLE_EQ(m.channel(0).peakDb, -3);
    m.advance(at(1000));
    EXPECT_NEAR(m.channel(0).peakDb, -9, 0.001); // 24 dB/s after 750 ms
    EXPECT_LE(m.channel(0).peakDb, -3);
}

TEST(MeterDynamics, ElapsedTimeNotFrameCount) {
    MeterDynamics a, b;
    for (auto* m : {&a, &b}) {
        m->setSourceState(State::Fresh, at(0));
        m->observe(all(-60), at(0));
        m->observe(all(-12), at(100));
    }
    a.advance(at(150));
    a.advance(at(200));
    b.advance(at(200));
    EXPECT_NEAR(a.channel(0).displayDb, b.channel(0).displayDb, 0.001);
    a.observe(all(-48), at(210));
    b.observe(all(-48), at(210));
    a.advance(at(260));
    a.advance(at(310));
    b.advance(at(310));
    EXPECT_NEAR(a.channel(0).displayDb, b.channel(0).displayDb, 0.001);
}

TEST(MeterDynamics, LongPauseAndMonotonicRegression) {
    MeterDynamics m;
    m.setSourceState(State::Fresh, at(0));
    m.observe(all(-60), at(0));
    m.observe(all(-9), at(100));
    m.advance(at(50)); // older frame cannot move the state
    EXPECT_DOUBLE_EQ(m.channel(0).displayDb, -60);
    m.advance(at(2100));
    EXPECT_DOUBLE_EQ(m.channel(0).displayDb, -9);
    EXPECT_GE(m.channel(0).peakDb, m.channel(0).displayDb);
}

TEST(MeterDynamics, FloorClampingAndInvalidChannels) {
    MeterDynamics m;
    m.setSourceState(State::Fresh, at(0));
    auto sample = all(-120);
    sample[1] = 14;
    sample[2] = std::numeric_limits<double>::quiet_NaN();
    sample[3] = std::numeric_limits<double>::infinity();
    m.observe(sample, at(0));
    EXPECT_DOUBLE_EQ(m.channel(0).displayDb, -60);
    EXPECT_DOUBLE_EQ(m.channel(1).displayDb, 0);
    EXPECT_FALSE(m.channel(2).valid);
    EXPECT_FALSE(m.channel(3).valid);
    EXPECT_TRUE(m.channel(4).valid);
}

TEST(MeterDynamics, DelayedStaleRecoveryAndDisconnect) {
    MeterDynamics m;
    m.setSourceState(State::Fresh, at(0));
    m.observe(all(-10), at(0));
    m.setSourceState(State::Delayed, at(400));
    EXPECT_FALSE(m.needsFrames());
    m.observe(all(-1), at(500)); // ignored while delayed
    EXPECT_DOUBLE_EQ(m.channel(0).observedDb, -10);
    m.setSourceState(State::Stale, at(750));
    EXPECT_FALSE(m.channel(0).valid);
    EXPECT_DOUBLE_EQ(m.channel(0).displayDb, -60);
    m.setSourceState(State::Fresh, at(800));
    EXPECT_FALSE(m.channel(0).valid); // no pre-recovery sample reused
    m.observe(all(-23), at(810));
    EXPECT_DOUBLE_EQ(m.channel(0).displayDb, -23);
    m.setSourceState(State::Unavailable, at(900));
    EXPECT_FALSE(m.channel(0).valid);
}

TEST(MeterDynamics, EightChannelsStayIndependent) {
    MeterDynamics m;
    m.setSourceState(State::Fresh, at(0));
    std::array<double, 8> sample{-60, -50, -40, -30, -20, -10, -5, 0};
    m.observe(sample, at(0));
    for (std::size_t i = 0; i < sample.size(); ++i) {
        EXPECT_DOUBLE_EQ(m.channel(i).observedDb, sample[i]);
        EXPECT_DOUBLE_EQ(m.channel(i).displayDb, sample[i]);
        EXPECT_DOUBLE_EQ(m.channel(i).peakDb, sample[i]);
    }
}

TEST(MeterDynamics, FloorSamplesDoNotCreateOrRenewAStuckPeak) {
    MeterDynamics m;
    m.setSourceState(State::Fresh, at(0));
    m.observe(all(-60), at(0));
    EXPECT_DOUBLE_EQ(m.channel(0).peakDb, -60);
    EXPECT_FALSE(m.needsFrames());
    m.observe(all(-6), at(100));
    m.observe(all(-60), at(200));
    for (int t = 300; t <= 4000; t += 100) {
        m.observe(all(-60), at(t));
        m.advance(at(t));
    }
    EXPECT_DOUBLE_EQ(m.channel(0).peakDb, -60);
    EXPECT_DOUBLE_EQ(m.channel(0).displayDb, -60);
    EXPECT_FALSE(m.needsFrames());
    m.observe(all(-60), at(4100));
    EXPECT_FALSE(m.needsFrames());
}

TEST(MeterDynamics, InvalidAndReconnectClearPeakImmediately) {
    MeterDynamics m;
    m.setSourceState(State::Fresh, at(0));
    m.observe(all(-3), at(0));
    auto bad = all(-60);
    bad[0] = std::numeric_limits<double>::quiet_NaN();
    m.observe(bad, at(100));
    EXPECT_FALSE(m.channel(0).valid);
    EXPECT_DOUBLE_EQ(m.channel(0).peakDb, -60);
    m.setSourceState(State::Unavailable, at(200));
    m.setSourceState(State::Fresh, at(300));
    EXPECT_FALSE(m.channel(1).valid);
    m.observe(all(-60), at(400));
    EXPECT_DOUBLE_EQ(m.channel(1).peakDb, -60);
}
