// Phase 12C "Follow active output": the rule table from the owner spec and the one-SET-no-retry executor.
#include <gtest/gtest.h>

#include "FakeHardware.hpp"
#include "core/OutputControl.hpp"
#include "core/OutputFollow.hpp"
#include "core/UserConfig.hpp"
#include "transport/MockTransport.hpp"

using namespace lewitt;
using namespace lewitt::testing;

TEST(OutputFollow, RuleTableFromTheOwnerSpec) {
    const FollowConfig d;   // defaults: both on -> speakers, both muted -> keep last
    EXPECT_EQ(followMapping(false, true, 2, 2, d), 1);    // only speakers on
    EXPECT_EQ(followMapping(true, false, 1, 1, d), 2);    // only headphones on
    EXPECT_EQ(followMapping(false, false, 2, 2, d), 1);   // both on: speakers
    EXPECT_EQ(followMapping(true, true, 2, 2, d), 2);     // both muted: keep
    EXPECT_EQ(followMapping(true, true, 0, 2, d), 2);     // keep after "none": the last active one
    FollowConfig c;
    c.bothOn = FollowChoice::Headphones;
    c.bothMuted = FollowChoice::Disable;
    EXPECT_EQ(followMapping(false, false, 1, 1, c), 2);
    EXPECT_EQ(followMapping(true, true, 1, 1, c), 0);     // disable until an output is unmuted
    c.bothOn = FollowChoice::Keep;
    EXPECT_EQ(followMapping(false, false, 2, 2, c), 2);
    EXPECT_EQ(followMapping(false, false, 0, 1, c), 1);
}

namespace {
struct Rig {
    MockTransport mock;
    FakeHardware* hw{nullptr};
    std::unique_ptr<DeviceSession> session;
    std::vector<std::string> log;
    explicit Rig(std::vector<std::size_t> offsets = {settings_off::kNativeVolumeMapping}) {
        auto t = std::make_unique<FakeHardware>(&mock);
        hw = t.get();
        WritePolicy p;
        p.allowHardwareWrites = true;
        p.hardwareWriteOffsets = std::move(offsets);
        session = std::make_unique<DeviceSession>(std::move(t), p);
        EXPECT_TRUE(session->connect().ok());
    }
    void setMutes(bool spk, bool hp) {
        auto m = SettingsMessage::fromBytes(mock.settingsBytes()).value();
        auto b = m.bytes();
        Bytes x(b.begin(), b.end());
        x[settings_off::kSpeakerMute] = spk;
        x[settings_off::kHeadphoneMute] = hp;
        mock.setSettingsBytes(x);
        EXPECT_TRUE(session->refreshSettings().ok());
    }
    std::uint8_t mapping() { return mock.settingsBytes()[settings_off::kNativeVolumeMapping]; }
};
PollPolicy fast() { return fastPoll(); }
}  // namespace

TEST(OutputFollow, SwitchesByte59OnceAndOnlyByte59) {
    Rig r;
    OutputFollower f(*r.session, FollowConfig{}, fast(), [&](const std::string& s) { r.log.push_back(s); });
    r.setMutes(false, true);                       // speakers on (mock default mapping: speakers)
    EXPECT_FALSE(f.step());
    EXPECT_EQ(r.hw->writeCalls, 0);
    r.setMutes(true, false);                       // custom button: headphones on
    const auto before = r.mock.settingsBytes();
    EXPECT_TRUE(f.step());
    EXPECT_EQ(r.mapping(), 2);
    const auto after = r.mock.settingsBytes();
    for (std::size_t i = 0; i < before.size(); ++i)
        if (i != settings_off::kNativeVolumeMapping) EXPECT_EQ(before[i], after[i]) << "byte " << i;
    EXPECT_FALSE(f.step()) << "already there: no second SET";
    r.setMutes(true, true);                        // both muted: keep headphones
    EXPECT_FALSE(f.step());
    EXPECT_EQ(r.mapping(), 2);
}

TEST(OutputFollow, AFailedChangeIsNotRetriedForTheSameState) {
    Rig r({});                                     // policy without byte 59: every change is refused
    OutputFollower f(*r.session, FollowConfig{}, fast());
    r.setMutes(true, false);
    const int w = r.hw->writeCalls;
    f.step();
    f.step();
    f.step();
    EXPECT_EQ(r.hw->writeCalls, w) << "refused by policy, never sent";
    EXPECT_EQ(r.mapping(), 1);
}

TEST(OutputFollow, ConfigKeysParseFailClosed) {
    auto r = parseUserConfig("follow_active_output = true\nfollow_both_on = headphones\nfollow_both_muted = disable\n");
    EXPECT_TRUE(r.config.followActiveOutput);
    EXPECT_EQ(r.config.followBothOn, FollowPref::Headphones);
    EXPECT_EQ(r.config.followBothMuted, FollowPref::Disable);
    EXPECT_TRUE(r.warnings.empty());
    r = parseUserConfig("follow_both_on = disable\nfollow_both_muted = loud\n");
    EXPECT_EQ(r.config.followBothOn, FollowPref::Speakers);
    EXPECT_EQ(r.config.followBothMuted, FollowPref::Keep);
    EXPECT_EQ(r.warnings.size(), 2u);
    EXPECT_FALSE(r.config.followActiveOutput);
    auto back = parseUserConfig(serializeUserConfig(parseUserConfig("follow_both_on = keep\n").config));
    EXPECT_EQ(back.config.followBothOn, FollowPref::Keep);
    EXPECT_TRUE(back.warnings.empty());
}
