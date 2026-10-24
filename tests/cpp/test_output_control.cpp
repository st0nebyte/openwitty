// Phase 8D: output scope, value validation, bounded increases and write authorization.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "FakeHardware.hpp"
#include "core/Authorization.hpp"
#include "core/OutputControl.hpp"

using namespace lewitt;
using namespace lewitt::testing;

namespace {
const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

struct Rig {
    MockTransport mock;
    FakeHardware* hw{nullptr};
    std::unique_ptr<DeviceSession> s;
    explicit Rig(WritePolicy p) {
        auto t = std::make_unique<FakeHardware>(&mock);
        hw = t.get();
        s = std::make_unique<DeviceSession>(std::move(t), p);
        EXPECT_TRUE(s->connect().ok());
    }
};
}  // namespace

TEST(OutputControl, OffsetsAreExactlyTheVolumeAndMuteBytes) {
    EXPECT_EQ(outputWriteOffsets(Output::Speaker), (std::vector<std::size_t>{20, 21, 22}));
    EXPECT_EQ(outputWriteOffsets(Output::Headphone), (std::vector<std::size_t>{27, 28, 29}));
    auto p = outputWritePolicy(true, false);
    EXPECT_TRUE(p.allowHardwareWrites);
    EXPECT_FALSE(p.allowRoutingChanges);
    EXPECT_FALSE(p.allowPersistentChanges);
    EXPECT_EQ(p.hardwareWriteOffsets, (std::vector<std::size_t>{20, 21, 22}));
    EXPECT_EQ(outputWritePolicy(true, true).hardwareWriteOffsets, (std::vector<std::size_t>{20, 21, 22, 27, 28, 29}));
    auto none = outputWritePolicy(false, false);
    EXPECT_FALSE(none.allowHardwareWrites);
    EXPECT_TRUE(none.hardwareWriteOffsets.empty());
}

TEST(OutputControl, NonFiniteAndOutOfRangeValuesAreRejectedBeforeStaging) {
    SettingsMessage m;
    ASSERT_TRUE(m.setSpeakerVolumeDb(-23.5).ok());
    const SettingsMessage before = m;
    for (double bad : {kNaN, kInf, -kInf, -60.01, 0.01, -1000.0})
        EXPECT_FALSE(stageOutput(m, Output::Speaker, bad, true).ok()) << bad;
    EXPECT_TRUE(m == before);                                   // nothing half-staged (mute untouched too)
    EXPECT_FALSE(normalizeOutputDb(kNaN).has_value());
    EXPECT_FALSE(normalizeOutputDb(kInf).has_value());
    EXPECT_EQ(*normalizeOutputDb(-80), -60.0);
    EXPECT_EQ(*normalizeOutputDb(5), 0.0);
    EXPECT_DOUBLE_EQ(*normalizeOutputDb(-23.5), -23.5);
    EXPECT_DOUBLE_EQ(*normalizeOutputDb(-60.0), -60.0);
    EXPECT_DOUBLE_EQ(*normalizeOutputDb(0.0), 0.0);
}

TEST(OutputControl, BoundaryValuesEncodeExactly) {
    SettingsMessage m;
    ASSERT_TRUE(stageOutput(m, Output::Speaker, -60.0, std::nullopt).ok());
    EXPECT_DOUBLE_EQ(m.speakerVolumeDb(), -60.0);
    ASSERT_TRUE(stageOutput(m, Output::Speaker, 0.0, std::nullopt).ok());
    EXPECT_DOUBLE_EQ(m.speakerVolumeDb(), 0.0);
    ASSERT_TRUE(stageOutput(m, Output::Speaker, -30.0, std::nullopt).ok());
    EXPECT_EQ(m.bytes()[20], 0x00);
    EXPECT_EQ(m.bytes()[21], 0xE2);                              // -30 dB = 0xE200 Q8.8, little endian
}

TEST(OutputControl, LimitIncreaseOnlyLimitsIncreases) {
    EXPECT_DOUBLE_EQ(limitIncrease(-40, -10, 10), -30);
    EXPECT_DOUBLE_EQ(limitIncrease(-40, -35, 10), -35);
    EXPECT_DOUBLE_EQ(limitIncrease(-10, -50, 10), -50);          // decreases are never limited
    EXPECT_DOUBLE_EQ(limitIncrease(-40, 0, 0), -40);
    EXPECT_DOUBLE_EQ(limitIncrease(-40, -10, kNaN), -40);        // broken config: no increase at all
    EXPECT_DOUBLE_EQ(limitIncrease(-40, -10, -3), -40);
    EXPECT_DOUBLE_EQ(limitIncrease(-40, kNaN, 10), -40);
}

TEST(OutputControl, SpeakerAndHeadphoneAreIndependent) {
    SettingsMessage m;
    ASSERT_TRUE(m.setHeadphoneVolumeDb(-32.4).ok());
    ASSERT_TRUE(m.setHeadphoneMute(true).ok());
    const auto hpBefore = outputLevel(m, Output::Headphone);
    ASSERT_TRUE(stageOutput(m, Output::Speaker, -12.0, false).ok());
    EXPECT_NEAR(outputLevel(m, Output::Headphone).db, hpBefore.db, 1e-9);
    EXPECT_EQ(outputLevel(m, Output::Headphone).muted, true);
    EXPECT_DOUBLE_EQ(outputLevel(m, Output::Speaker).db, -12.0);
    ASSERT_TRUE(stageOutput(m, Output::Headphone, -20.0, false).ok());
    EXPECT_DOUBLE_EQ(outputLevel(m, Output::Speaker).db, -12.0);
}

TEST(OutputControl, SpeakerPolicyCannotWriteHeadphoneOrAnythingElseOnHardware) {
    Rig r(outputWritePolicy(true, false));
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return stageOutput(m, Output::Headphone, -10.0, false); }).ok());
    auto res = r.s->commitAndVerify(fastPoll());
    EXPECT_EQ(res.state, WriteState::Failed);
    EXPECT_EQ(res.error.code, Errc::WritesDisabled);
    EXPECT_EQ(settingsWrites(r.mock), 0);
    r.s->discardPending();
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return m.setLedBrightness(5); }).ok());
    EXPECT_EQ(r.s->commitAndVerify(fastPoll()).state, WriteState::Failed);
    EXPECT_EQ(settingsWrites(r.mock), 0);
    r.s->discardPending();
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return stageOutput(m, Output::Speaker, -18.0, false); }).ok());
    EXPECT_EQ(r.s->commitAndVerify(fastPoll()).state, WriteState::Verified);
    EXPECT_EQ(settingsWrites(r.mock), 1);
}

TEST(OutputControl, IgnoredWriteIsReportedUnverifiedAndNotRetried) {
    Rig r(outputWritePolicy(true, false));
    r.hw->dropWrites = true;                                    // the device ACKs and ignores
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return stageOutput(m, Output::Speaker, -18.0, false); }).ok());
    auto res = r.s->commitAndVerify(fastPoll());
    EXPECT_EQ(res.state, WriteState::VerificationTimedOut);
    EXPECT_EQ(r.hw->writeCalls, 1);                             // exactly one SET, never retried
}

TEST(Authorization, OnlyExactlyOneAuthorizes) {
    EXPECT_TRUE(envFlagExactlyOne("1"));
    for (const char* v : {"", "0", "true", "yes", "1 ", " 1", "11", "01", "1\n"}) EXPECT_FALSE(envFlagExactlyOne(v)) << v;
    EXPECT_FALSE(envFlagExactlyOne(nullptr));
}
