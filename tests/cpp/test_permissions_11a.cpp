// Phase 11A: grouped device-write permissions. Simulation only.
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>

#include "core/ControlCapabilities.hpp"
#include "core/OutputControl.hpp"
#include "core/UserConfig.hpp"

using namespace lewitt;

namespace {
std::set<std::string> writableKeys(const WritePolicy& p) {
    std::set<std::string> s;
    for (const auto& k : controlKeys())
        if (controlWritable(k, p, /*simulated=*/false)) s.insert(k);
    return s;
}
UserConfig only(const DevicePermission& perm) {
    UserConfig c;
    c.*(perm.field) = true;
    return c;
}
}  // namespace

TEST(Permissions11A, EveryPermissionDefaultsOffAndIsParsedFailClosed) {
    EXPECT_FALSE(anyDeviceWritePermission(UserConfig{}));
    EXPECT_EQ(devicePermissions().size(), 12u);
    for (const auto& p : devicePermissions()) {
        EXPECT_FALSE(UserConfig{}.*(p.field)) << p.key;
        auto on = parseUserConfig(std::string(p.key) + " = true\n");
        EXPECT_TRUE(on.config.*(p.field)) << p.key;
        EXPECT_TRUE(on.warnings.empty()) << p.key;
        EXPECT_FALSE(parseUserConfig(std::string(p.key) + " = maybe\n").config.*(p.field)) << p.key;
        EXPECT_FALSE(parseUserConfig(std::string(p.key) + " = true\n" + p.key + " = true\n").config.*(p.field)) << p.key;
        // round trip, and no other permission comes along
        const auto back = parseUserConfig(serializeUserConfig(only(p)));
        EXPECT_EQ(back.config, only(p)) << p.key;
        EXPECT_TRUE(findDevicePermission(p.name) == &p);
    }
    EXPECT_EQ(findDevicePermission("everything"), nullptr);
}

TEST(Permissions11A, EachGroupAuthorizesExactlyItsControls) {
    const std::map<std::string, std::set<std::string>> expected = {
        {"speaker", {"spk.volume", "spk.mute"}},
        {"headphone", {"hp.volume", "hp.mute"}},
        {"ledBrightness", {"set.ledBrightness"}},
        {"inputs", {"in1.gain", "in2.gain", "in1.mute", "in2.mute", "set.gainLock"}},
        {"processing", {"in1.highPass", "in2.highPass", "in1.clipguard", "in2.clipguard", "in1.preamp", "in2.preamp",
                        "in1.denoise", "in2.denoise", "in1.compress", "in2.compress"}},
        {"monitoring", {"mon.mix", "mon.mic", "mon.inst", "out.dim", "out.mono", "loop.mute"}},
        {"appearance", {"set.led.icons", "set.led.mute", "set.led.parameter", "set.led.metering", "set.led.gainReduction",
                        "set.customButton"}},
        {"phantom", {"in1.phantom"}},
        {"outputModes", {"set.highVolume", "set.highImpedance"}},
        {"measurements", {"autogain1", "autogain2", "autogainBoth", "denoise1", "denoise2"}},
        {"routing", {"set.mixToSpeakers", "set.mobileStreaming", "set.volumeMapping"}},
        {"followOutput", {"set.volumeMapping"}},
    };
    ASSERT_EQ(expected.size(), devicePermissions().size());
    for (const auto& p : devicePermissions())
        EXPECT_EQ(writableKeys(guiWritePolicy(only(p))), expected.at(p.name)) << p.name;
    // everything on: still never the macOS streaming mode (device restart); Autosetup needs processing + 48 V + measurements
    UserConfig all;
    for (const auto& p : devicePermissions()) all.*(p.field) = true;
    const auto keys = writableKeys(guiWritePolicy(all));
    EXPECT_TRUE(keys.count("set.mixToSpeakers"));
    EXPECT_TRUE(keys.count("set.mobileStreaming"));
    EXPECT_FALSE(keys.count("set.macosStreaming"));
    EXPECT_TRUE(keys.count("autosetup"));
    UserConfig noPhantom = all;
    noPhantom.phantomControl = false;
    EXPECT_FALSE(writableKeys(guiWritePolicy(noPhantom)).count("autosetup"));
}

TEST(Permissions11A, PolicyGrantsRoutingOnlyViaItsPermissionAndNeverPersistenceOrDangerousTriggers) {
    UserConfig all;
    for (const auto& p : devicePermissions()) all.*(p.field) = true;
    const WritePolicy w = guiWritePolicy(all);
    EXPECT_TRUE(w.allowHardwareWrites);
    EXPECT_TRUE(w.allowRoutingChanges);    // only through the routing permission (below)
    EXPECT_FALSE(w.allowPersistentChanges);
    for (const auto& p : devicePermissions())
        EXPECT_EQ(guiWritePolicy(only(p)).allowRoutingChanges, std::string(p.name) == "routing") << p.name;
    for (const auto& p : devicePermissions()) {   // the touch focus (56) only through follow active output
        const auto o = guiWritePolicy(only(p)).hardwareWriteOffsets;
        EXPECT_EQ(std::count(o.begin(), o.end(), settings_off::kSelectedChannel) == 1,
                  std::string(p.name) == "followOutput") << p.name;
    }
    for (std::size_t forbidden : {
                                  std::size_t(54), std::size_t(55), std::size_t(58), std::size_t(70)})
        EXPECT_EQ(std::count(w.hardwareWriteOffsets.begin(), w.hardwareWriteOffsets.end(), forbidden), 0) << forbidden;
    for (Trigger t : {Trigger::FactoryReset, Trigger::SwitchToMacOSStreamingMode, Trigger::SwitchToNormalMode,
                      Trigger::ResetLedColors})
        EXPECT_EQ(std::count(w.hardwareTriggers.begin(), w.hardwareTriggers.end(), t), 0);
    for (const auto& p : devicePermissions()) {
        const auto pol = guiWritePolicy(only(p));
        if (std::string(p.name) == "measurements") EXPECT_FALSE(pol.hardwareTriggers.empty());
        else EXPECT_TRUE(pol.hardwareTriggers.empty()) << p.name;
    }
    EXPECT_EQ(guiWritePolicy(UserConfig{}).allowHardwareWrites, false);
}

TEST(Permissions11A, ThreeFlagOverloadMatchesTheTable) {
    for (int m = 0; m < 8; ++m) {
        UserConfig c;
        c.speakerControl = m & 1;
        c.headphoneControl = m & 2;
        c.ledBrightnessControl = m & 4;
        const auto a = guiWritePolicy(c), b = guiWritePolicy(m & 1, m & 2, m & 4);
        EXPECT_EQ(a.hardwareWriteOffsets, b.hardwareWriteOffsets);
        EXPECT_EQ(a.allowHardwareWrites, b.allowHardwareWrites);
    }
}

TEST(Permissions11A, GainIncreaseIsBoundedPerStep) {
    EXPECT_DOUBLE_EQ(limitIncrease(20.0, 50.0, kMaxGainIncreaseDb), 26.0);
    EXPECT_DOUBLE_EQ(limitIncrease(20.0, 5.0, kMaxGainIncreaseDb), 5.0);
}
