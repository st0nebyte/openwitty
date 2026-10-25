// Phase 10A: per-control write capabilities derived from the session's write policy.
#include <gtest/gtest.h>

#include <set>

#include "core/ControlCapabilities.hpp"
#include "core/OutputControl.hpp"

using namespace lewitt;

namespace {
std::set<std::string> writable(const WritePolicy& p, bool simulated) {
    std::set<std::string> s;
    for (const auto& k : controlKeys())
        if (controlWritable(k, p, simulated)) s.insert(k);
    return s;
}
}  // namespace

TEST(ControlCapabilities, SpeakerHeadphoneControlAuthorizesExactlyTheFourOutputControls) {
    EXPECT_EQ(writable(outputWritePolicy(true, true), false),
              (std::set<std::string>{"hp.mute", "hp.volume", "spk.mute", "spk.volume"}));
}

TEST(ControlCapabilities, SpeakerOnlyPolicyAuthorizesOnlyTheSpeaker) {
    EXPECT_EQ(writable(outputWritePolicy(true, false), false), (std::set<std::string>{"spk.mute", "spk.volume"}));
}

TEST(ControlCapabilities, ReadOnlyAndLedPoliciesAuthorizeOnlyWhatTheyName) {
    EXPECT_TRUE(writable(WritePolicy{}, false).empty());
    WritePolicy led;
    led.allowHardwareWrites = true;
    led.hardwareWriteOffsets = {settings_off::kLedBrightness};
    EXPECT_EQ(writable(led, false), (std::set<std::string>{"set.ledBrightness"}));
    WritePolicy offsetsButNoHardware = led;
    offsetsButNoHardware.allowHardwareWrites = false;     // offsets alone grant nothing
    EXPECT_TRUE(writable(offsetsButNoHardware, false).empty());
}

TEST(ControlCapabilities, MultiByteFieldsNeedEveryByte) {
    WritePolicy p;
    p.allowHardwareWrites = true;
    p.hardwareWriteOffsets = {settings_off::kGain[0]};          // only the first byte of the Q8.8 gain
    EXPECT_FALSE(controlWritable("in1.gain", p, false));
    p.hardwareWriteOffsets.push_back(settings_off::kGain[0] + 1);
    EXPECT_TRUE(controlWritable("in1.gain", p, false));
    EXPECT_FALSE(controlWritable("in2.gain", p, false));
}

TEST(ControlCapabilities, TriggersAndWorkflowsNeedTheirWholeSequence) {
    WritePolicy p;
    p.allowHardwareWrites = true;
    p.hardwareTriggers = {Trigger::AutoGainMicrophone};
    EXPECT_FALSE(controlWritable("autogain1", p, false));       // the measurement bracket is missing
    p.hardwareTriggers = {Trigger::BeginMeasurementSession, Trigger::AutoGainMicrophone, Trigger::EndMeasurementSession};
    EXPECT_TRUE(controlWritable("autogain1", p, false));
    EXPECT_FALSE(controlWritable("autogain2", p, false));
    EXPECT_FALSE(controlWritable("autosetup", p, false));        // also writes preamp, Clipguard, Denoise, 48 V
    EXPECT_FALSE(controlWritable("set.macosStreaming", p, false));
}

TEST(ControlCapabilities, MonitoringSourceButtonsShareOneByte) {
    WritePolicy p;
    p.allowHardwareWrites = true;
    p.hardwareWriteOffsets = {settings_off::kMonitorSource};
    EXPECT_TRUE(controlWritable("mon.mic", p, false));
    EXPECT_TRUE(controlWritable("mon.inst", p, false));
}

TEST(ControlCapabilities, SimulationKeepsEveryControlAndUnknownKeysAreNeverWritable) {
    EXPECT_EQ(writable(WritePolicy{}, true).size(), controlKeys().size());
    EXPECT_FALSE(controlWritable("in2.phantom", WritePolicy{}, true));    // input 2 has no phantom power
    EXPECT_FALSE(controlWritable("nonsense", outputWritePolicy(true, true), false));
}
