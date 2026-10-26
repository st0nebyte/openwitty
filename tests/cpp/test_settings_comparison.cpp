// Phase 10B.1: read-only snapshot comparison for owner-approved hardware tests, and the "was a SET sent"
// bookkeeping the GUI uses to decide whether to stop writing. Simulation only.
#include <gtest/gtest.h>

#include "FakeHardware.hpp"
#include "core/SettingsComparison.hpp"

using namespace lewitt;

namespace {
SettingsMessage base() {
    MockTransport m;
    return SettingsMessage::fromBytes(m.settingsBytes()).value();
}
SettingsMessage withByte(SettingsMessage s, std::size_t off, std::uint8_t v) {
    Bytes b(s.bytes().begin(), s.bytes().end());
    b[off] = v;
    return SettingsMessage::fromBytes(b).value();
}
struct Rig {
    MockTransport mock;
    lewitt::testing::FakeHardware* hw{nullptr};
    std::unique_ptr<DeviceSession> s;
    Rig() {
        WritePolicy p;
        p.allowHardwareWrites = true;
        p.hardwareWriteOffsets = {settings_off::kLedBrightness};
        auto t = std::make_unique<lewitt::testing::FakeHardware>(&mock);
        hw = t.get();
        s = std::make_unique<DeviceSession>(std::move(t), p);
        EXPECT_TRUE(s->connect().ok());
    }
};
}  // namespace

TEST(SettingsComparison, IdenticalSnapshotsReportNothing) {
    const auto a = base();
    const auto c = compareSettings(a, a, {settings_off::kLedBrightness});
    EXPECT_TRUE(c.changed.empty());
    EXPECT_TRUE(c.unexpected.empty());
    const std::string d = describeComparison(c);
    EXPECT_EQ(d.substr(d.find("; ")), "; changed: none; unexpected: none");
}

TEST(SettingsComparison, LedChangeIsExpectedAnythingElseIsUnexpected) {
    const auto a = withByte(base(), settings_off::kLedBrightness, 60);
    const auto b = withByte(withByte(a, settings_off::kLedBrightness, 40), settings_off::kSpeakerMute, 1);
    const auto c = compareSettings(a, b, {settings_off::kLedBrightness});
    EXPECT_EQ(c.changed, (std::vector<std::size_t>{settings_off::kSpeakerMute, settings_off::kLedBrightness}));
    EXPECT_EQ(c.unexpected, (std::vector<std::size_t>{settings_off::kSpeakerMute}));
    EXPECT_EQ(c.ledBrightnessFirst, 60);
    EXPECT_EQ(c.ledBrightnessSecond, 40);
    EXPECT_EQ(describeComparison(c),
              "LED brightness 60 -> 40; changed: 22 (speaker mute), 37 (LED brightness); unexpected: 22 (speaker mute)");
}

TEST(SettingsComparison, ReportCarriesNoRawValuesOtherThanLedBrightness) {
    const auto a = base();
    const auto b = withByte(withByte(a, settings_off::kSpeakerVolume, 0xAB), 100, 0xCD);   // unknown offset 100
    const std::string d = describeComparison(compareSettings(a, b, {}));
    EXPECT_EQ(d.find("171"), std::string::npos);    // 0xAB
    EXPECT_EQ(d.find("205"), std::string::npos);    // 0xCD
    EXPECT_EQ(d.find("ab"), std::string::npos);
    EXPECT_NE(d.find("20 (speaker volume)"), std::string::npos);
    EXPECT_NE(d.find("100 (offset 100)"), std::string::npos);
}

TEST(WriteBookkeeping, VerifiedWriteWasSubmittedOnce) {
    Rig r;
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return m.setLedBrightness(42); }).ok());
    PollPolicy pp = lewitt::testing::fastPoll();
    pp.requireStableSnapshot = true;
    const auto res = r.s->commitAndVerify(pp);
    EXPECT_EQ(res.state, WriteState::Verified);
    EXPECT_TRUE(res.submitted);
    EXPECT_EQ(r.hw->writeCalls, 1);
}

TEST(WriteBookkeeping, UnstableSnapshotSendsNothingAndSaysSo) {
    Rig r;
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return m.setLedBrightness(42); }).ok());
    r.mock.injectExternalSettingsChange(1, settings_off::kSpeakerMute, 1);   // lands between the two reads
    PollPolicy pp = lewitt::testing::fastPoll();
    pp.requireStableSnapshot = true;
    const auto res = r.s->commitAndVerify(pp);
    EXPECT_EQ(res.state, WriteState::Failed);
    EXPECT_FALSE(res.submitted);
    EXPECT_EQ(r.hw->writeCalls, 0);
    EXPECT_NE(r.mock.settingsBytes()[settings_off::kLedBrightness], 42);
    EXPECT_EQ(r.mock.settingsBytes()[settings_off::kSpeakerMute], 1);   // the physical change survives
}

TEST(WriteBookkeeping, IgnoredSetTimesOutAsSubmittedAndIsNeverResent) {
    Rig r;
    r.hw->dropWrites = true;
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return m.setLedBrightness(42); }).ok());
    const auto res = r.s->commitAndVerify(lewitt::testing::fastPoll());
    EXPECT_EQ(res.state, WriteState::VerificationTimedOut);
    EXPECT_TRUE(res.submitted);
    EXPECT_EQ(r.hw->writeCalls, 1);
}

TEST(WriteBookkeeping, NoOpEditSubmitsNothing) {
    Rig r;
    const int now = r.s->settings()->ledBrightness();
    ASSERT_TRUE(r.s->edit([&](SettingsMessage& m) { return m.setLedBrightness(now); }).ok());
    const auto res = r.s->commitAndVerify(lewitt::testing::fastPoll());
    EXPECT_EQ(res.state, WriteState::Verified);
    EXPECT_FALSE(res.submitted);
    EXPECT_EQ(r.hw->writeCalls, 0);
}
