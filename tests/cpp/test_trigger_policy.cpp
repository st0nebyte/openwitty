// Phase 10A: trigger messages (autogain, denoise measurement, measurement sessions) on real hardware.
// A settings-offset authorization (speaker/headphone output control, LED brightness, ...) must never imply
// permission to send triggers: hardwareWriteOffsets only limits settings bytes.
#include <gtest/gtest.h>

#include "FakeHardware.hpp"
#include "core/OutputControl.hpp"

using namespace lewitt;
using namespace lewitt::testing;

namespace {
const Trigger kMeasurementTriggers[] = {
    Trigger::BeginMeasurementSession, Trigger::AutoGainMicrophone, Trigger::AutoGainInstrument,
    Trigger::AutoGainBoth,  Trigger::DenoiseMeasureMicrophone, Trigger::DenoiseMeasureInstrument,
    Trigger::DenoiseMeasureBoth, Trigger::EndMeasurementSession};
}  // namespace

TEST(TriggerPolicy, OutputControlNeverSendsMeasurementTriggersToHardware) {
    MockTransport mock;
    auto t = std::make_unique<FakeHardware>(&mock);
    FakeHardware* hw = t.get();
    DeviceSession s(std::move(t), outputWritePolicy(/*speaker=*/true, /*headphone=*/true));
    ASSERT_TRUE(s.connect().ok());
    ASSERT_TRUE(s.writesEnabled());                       // output control is on ...
    for (Trigger tr : kMeasurementTriggers) {
        auto r = s.sendTrigger(tr);
        EXPECT_FALSE(r.ok()) << static_cast<int>(tr);     // ... but triggers are not authorized
    }
    EXPECT_EQ(hw->writeCalls, 0);
    EXPECT_TRUE(s.sendTrigger(Trigger::ResetLedColors).error().code != Errc::Ok);
    EXPECT_EQ(hw->writeCalls, 0);
}

TEST(TriggerPolicy, ASingleSettingsOffsetAuthorizationGrantsNoTriggers) {
    MockTransport mock;
    auto t = std::make_unique<FakeHardware>(&mock);
    FakeHardware* hw = t.get();
    WritePolicy p;
    p.allowHardwareWrites = true;
    p.hardwareWriteOffsets = {37};                        // the CLI's LED-brightness authorization
    DeviceSession s(std::move(t), p);
    ASSERT_TRUE(s.connect().ok());
    EXPECT_FALSE(s.sendTrigger(Trigger::AutoGainMicrophone).ok());
    EXPECT_EQ(hw->writeCalls, 0);
}

TEST(TriggerPolicy, AnExplicitlyListedTriggerIsTheOnlyOneAllowed) {
    MockTransport mock;
    auto t = std::make_unique<FakeHardware>(&mock);
    FakeHardware* hw = t.get();
    WritePolicy p;
    p.allowHardwareWrites = true;
    p.hardwareTriggers = {Trigger::BeginMeasurementSession, Trigger::AutoGainMicrophone,
                          Trigger::EndMeasurementSession};   // a future, separately approved autogain test
    DeviceSession s(std::move(t), p);
    ASSERT_TRUE(s.connect().ok());
    EXPECT_TRUE(s.sendTrigger(Trigger::BeginMeasurementSession).ok());
    EXPECT_TRUE(s.sendTrigger(Trigger::AutoGainMicrophone).ok());
    EXPECT_FALSE(s.sendTrigger(Trigger::AutoGainInstrument).ok());
    EXPECT_FALSE(s.sendTrigger(Trigger::DenoiseMeasureMicrophone).ok());
    EXPECT_TRUE(s.sendTrigger(Trigger::EndMeasurementSession).ok());
    EXPECT_EQ(hw->writeCalls, 3);
}

TEST(TriggerPolicy, SimulationKeepsItsTriggers) {
    DeviceSession s(std::make_unique<MockTransport>(), WritePolicy{});
    ASSERT_TRUE(s.connect().ok());
    for (Trigger tr : kMeasurementTriggers) EXPECT_TRUE(s.sendTrigger(tr).ok()) << static_cast<int>(tr);
}
