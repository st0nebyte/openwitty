// Control groups of the window's write policy (Phase 11A groups; product mode 2026-11-01: all of them, no switches).
// Simulation only.
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>

#include "core/ControlCapabilities.hpp"
#include "core/OutputControl.hpp"

using namespace lewitt;

namespace {
std::set<std::string> writableKeys(const WritePolicy& p) {
    std::set<std::string> s;
    for (const auto& k : controlKeys())
        if (controlWritable(k, p, /*simulated=*/false)) s.insert(k);
    return s;
}
WritePolicy only(const std::string& group) { return groupsWritePolicy({group}); }
}  // namespace

TEST(ControlGroups, EachGroupAuthorizesExactlyItsControls) {
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
    ASSERT_EQ(expected.size(), controlGroups().size());
    for (const auto& g : controlGroups()) EXPECT_EQ(writableKeys(only(g)), expected.at(g)) << g;
    EXPECT_TRUE(writableKeys(only("everything")).empty());   // unknown names add nothing
    EXPECT_FALSE(only("everything").allowHardwareWrites);
}

TEST(ControlGroups, TheAppPolicyCoversEveryVerifiedControlButNeverTheDeviceRestart) {
    const auto keys = writableKeys(appWritePolicy());
    for (const auto& g : controlGroups())
        for (const auto& k : writableKeys(only(g))) EXPECT_TRUE(keys.count(k)) << g << " " << k;
    EXPECT_TRUE(keys.count("autosetup"));
    EXPECT_FALSE(keys.count("set.macosStreaming"));
    // Autosetup needs processing + 48 V + measurements together
    std::vector<std::string> noPhantom;
    for (const auto& g : controlGroups())
        if (g != "phantom") noPhantom.push_back(g);
    EXPECT_FALSE(writableKeys(groupsWritePolicy(noPhantom)).count("autosetup"));
    // the service lets its clients write exactly what the window may write
    EXPECT_EQ(serviceClientPolicy().hardwareWriteOffsets, appWritePolicy().hardwareWriteOffsets);
    EXPECT_EQ(serviceClientPolicy().hardwareTriggers, appWritePolicy().hardwareTriggers);
}

TEST(ControlGroups, PolicyGrantsRoutingOnlyViaItsGroupAndNeverPersistenceOrDangerousTriggers) {
    const WritePolicy w = appWritePolicy();
    EXPECT_TRUE(w.allowHardwareWrites);
    EXPECT_TRUE(w.allowRoutingChanges);
    EXPECT_FALSE(w.allowPersistentChanges);
    for (const auto& g : controlGroups()) EXPECT_EQ(only(g).allowRoutingChanges, g == "routing") << g;
    for (const auto& g : controlGroups()) {   // the touch focus (56) only through follow active output
        const auto o = only(g).hardwareWriteOffsets;
        EXPECT_EQ(std::count(o.begin(), o.end(), settings_off::kSelectedChannel) == 1, g == "followOutput") << g;
    }
    for (std::size_t forbidden : {std::size_t(54), std::size_t(55), std::size_t(58), std::size_t(70)})
        EXPECT_EQ(std::count(w.hardwareWriteOffsets.begin(), w.hardwareWriteOffsets.end(), forbidden), 0) << forbidden;
    for (std::size_t o : w.hardwareWriteOffsets)   // no byte twice (routing and follow share 59)
        EXPECT_EQ(std::count(w.hardwareWriteOffsets.begin(), w.hardwareWriteOffsets.end(), o), 1) << o;
    for (Trigger t : {Trigger::FactoryReset, Trigger::SwitchToMacOSStreamingMode, Trigger::SwitchToNormalMode,
                      Trigger::ResetLedColors})
        EXPECT_EQ(std::count(w.hardwareTriggers.begin(), w.hardwareTriggers.end(), t), 0);
    for (const auto& g : controlGroups()) {
        if (g == "measurements") EXPECT_FALSE(only(g).hardwareTriggers.empty());
        else EXPECT_TRUE(only(g).hardwareTriggers.empty()) << g;
    }
    EXPECT_FALSE(groupsWritePolicy({}).allowHardwareWrites);
}

TEST(ControlGroups, ThreeFlagOverloadMatchesTheGroups) {
    for (int m = 0; m < 8; ++m) {
        std::vector<std::string> g;
        if (m & 1) g.push_back("speaker");
        if (m & 2) g.push_back("headphone");
        if (m & 4) g.push_back("ledBrightness");
        const auto a = groupsWritePolicy(g), b = guiWritePolicy(m & 1, m & 2, m & 4);
        EXPECT_EQ(a.hardwareWriteOffsets, b.hardwareWriteOffsets);
        EXPECT_EQ(a.allowHardwareWrites, b.allowHardwareWrites);
    }
}

TEST(ControlGroups, GainIncreaseIsBoundedPerStep) {
    EXPECT_DOUBLE_EQ(limitIncrease(20.0, 50.0, kMaxGainIncreaseDb), 26.0);
    EXPECT_DOUBLE_EQ(limitIncrease(20.0, 5.0, kMaxGainIncreaseDb), 5.0);
}
