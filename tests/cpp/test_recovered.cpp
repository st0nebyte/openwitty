// Findings from phase 5A. Fixtures are built from values read out of the app's
// disassembly/decompilation (constructor immediates, schema table, bit tests),
// not from our encoder. Simulated only -- not hardware-verified.
#include <gtest/gtest.h>
#include <set>

#include "core/DeviceSession.hpp"
#include "transport/MockTransport.hpp"

using namespace lewitt;

namespace {
Bytes blob(std::size_t n) { return Bytes(n, 0); }
}

TEST(Schema, CoversBytes0To70ExactlyOnce) {
    std::array<int, 128> seen{};
    for (auto f : kSettingsSchema)
        for (unsigned i = 0; i < f.length; ++i) ++seen[f.offset + i];
    for (std::size_t i = 0; i < kSettingsDefinedBytes; ++i) EXPECT_EQ(seen[i], 1) << "byte " << i;
    for (std::size_t i = kSettingsDefinedBytes; i < 128; ++i) EXPECT_EQ(seen[i], 0) << "byte " << i;
}

TEST(Schema, MultiByteFieldsMatchKnownQ88Offsets) {
    auto isFieldStart = [](unsigned off, unsigned len) {
        for (auto f : kSettingsSchema) if (f.offset == off) return f.length == len;
        return false;
    };
    for (unsigned off : {0u, 10u, 20u, 27u, 35u}) EXPECT_TRUE(isFieldStart(off, 2)) << off;
    EXPECT_TRUE(isFieldStart(63, 4));
}

TEST(CustomButton, ValuesFromRadioButtonConstructors) {
    EXPECT_EQ(static_cast<int>(CustomButtonFunction::ShowControlCenter), 0);
    EXPECT_EQ(static_cast<int>(CustomButtonFunction::ToggleHeadphoneSpeakerMute), 1);
    EXPECT_EQ(static_cast<int>(CustomButtonFunction::ToggleHeadphoneMicSpeakerMute), 2);
    EXPECT_EQ(static_cast<int>(CustomButtonFunction::DimOutputs), 8);
    EXPECT_EQ(static_cast<int>(CustomButtonFunction::MonoStereoOutputs), 9);
    EXPECT_EQ(static_cast<int>(CustomButtonFunction::TogglePreampMic), 0xC);
    EXPECT_EQ(static_cast<int>(CustomButtonFunction::TogglePreampInstrument), 0xD);
    EXPECT_EQ(static_cast<int>(CustomButtonFunction::ToggleBrightness), 0xE);
}

TEST(CustomButton, UnknownValuesPreservedNotInterpreted) {
    auto b = blob(0x80);
    b[53] = 0x07;   // not offered by the UI
    auto s = SettingsMessage::fromBytes(b).value();
    EXPECT_FALSE(s.customButtonFunction().has_value());
    EXPECT_EQ(s.customButton(), 7);
    b[53] = 0x0C;
    EXPECT_EQ(SettingsMessage::fromBytes(b).value().customButtonFunction(),
              CustomButtonFunction::TogglePreampMic);
    SettingsMessage m;
    ASSERT_TRUE(m.setCustomButtonFunction(CustomButtonFunction::DimOutputs).ok());
    EXPECT_EQ(m.bytes()[53], 8);
}

TEST(MixToSpeakers, BooleanToggleSemantics) {
    SettingsMessage s;
    EXPECT_FALSE(s.mixToSpeakers());          // guide: speaker monitoring off by default
    ASSERT_TRUE(s.setMixToSpeakers(true).ok());
    EXPECT_EQ(s.bytes()[23], 1);
    EXPECT_TRUE(s.mixToSpeakers());
    auto b = blob(0x80); b[23] = 5;           // value the UI never writes: kept, read as "on"
    EXPECT_TRUE(SettingsMessage::fromBytes(b).value().mixToSpeakers());
}

TEST(DeviceMode, PidMapping) {
    EXPECT_EQ(deviceModeFromPid(0x0004), DeviceMode::Normal);
    EXPECT_EQ(deviceModeFromPid(0x0002), DeviceMode::MacOSStreaming);
    EXPECT_EQ(deviceModeFromPid(0x000A), DeviceMode::Unknown);   // app throws for other PIDs
    EXPECT_EQ(deviceModeFromPid(0x1234), DeviceMode::Unknown);
}

TEST(Triggers, CommandWordsConfirmedByCreationOrder) {
    EXPECT_EQ(static_cast<int>(Trigger::AutoGainMicrophone), 1);
    EXPECT_EQ(static_cast<int>(Trigger::AutoGainInstrument), 2);
    EXPECT_EQ(static_cast<int>(Trigger::AutoGainBoth), 3);
    EXPECT_EQ(static_cast<int>(Trigger::DenoiseMeasureMicrophone), 6);
    EXPECT_EQ(static_cast<int>(Trigger::DenoiseMeasureInstrument), 7);
    EXPECT_EQ(static_cast<int>(Trigger::DenoiseMeasureBoth), 8);
    EXPECT_EQ(static_cast<int>(Trigger::ResetLedColors), 0x103);
    EXPECT_EQ(static_cast<int>(Trigger::FactoryReset), 0x106);
    auto t = TriggerMessage(Trigger::FactoryReset).bytes();
    EXPECT_EQ(t[0], 0x06); EXPECT_EQ(t[1], 0x01);
}

TEST(Triggers, FactoryResetNeverSentLedResetNeedsPolicy) {
    auto mk = [](bool simulated) {
        struct Fake final : IDeviceTransport {
            explicit Fake(bool s) : sim(s) {}
            std::string name() const override { return "fake"; }
            Status open() override { return okStatus(); }
            void close() override {}
            bool isOpen() const override { return true; }
            DeviceInfo info() const override { return {}; }
            Result<Bytes> read(MessageId id) override {
                Bytes b(messageInfo(id)->size, 0);
                if (id == MessageId::ApiVersion) b[0] = 1;
                return b;
            }
            Status write(MessageId, std::span<const std::uint8_t>) override { ++writes; return okStatus(); }
            bool isSimulated() const override { return sim; }
            bool sim; int writes{0};
        };
        return std::make_pair(new Fake(simulated), simulated);
    };
    // simulated transport: LED reset allowed, factory reset still refused
    {
        auto [raw, sim] = mk(true);
        DeviceSession s{std::unique_ptr<IDeviceTransport>(raw)};
        ASSERT_TRUE(s.connect().ok());
        EXPECT_TRUE(s.sendTrigger(Trigger::ResetLedColors).ok());
        EXPECT_EQ(raw->writes, 1);
        EXPECT_EQ(s.sendTrigger(Trigger::FactoryReset).error().code, Errc::NotSupported);
        EXPECT_EQ(raw->writes, 1);
    }
    // "hardware" transport: LED reset needs BOTH hardware writes and persistent changes enabled
    {
        auto [raw, sim] = mk(false);
        DeviceSession none{std::unique_ptr<IDeviceTransport>(raw)};
        ASSERT_TRUE(none.connect().ok());
        EXPECT_EQ(none.sendTrigger(Trigger::ResetLedColors).error().code, Errc::WritesDisabled);
        EXPECT_EQ(raw->writes, 0);
    }
    {
        auto [raw, sim] = mk(false);
        DeviceSession hwOnly{std::unique_ptr<IDeviceTransport>(raw),
                             WritePolicy{.allowHardwareWrites = true}};
        ASSERT_TRUE(hwOnly.connect().ok());
        EXPECT_EQ(hwOnly.sendTrigger(Trigger::ResetLedColors).error().code, Errc::WritesDisabled);
        EXPECT_EQ(raw->writes, 0);
        // Phase 10A: hardware writes alone no longer authorize any trigger
        EXPECT_EQ(hwOnly.sendTrigger(Trigger::AutoGainBoth).error().code, Errc::WritesDisabled);
        EXPECT_EQ(raw->writes, 0);
    }
    {
        auto [raw, sim] = mk(false);
        DeviceSession listed{std::unique_ptr<IDeviceTransport>(raw),
                             WritePolicy{.allowHardwareWrites = true, .hardwareTriggers = {Trigger::AutoGainBoth}}};
        ASSERT_TRUE(listed.connect().ok());
        EXPECT_TRUE(listed.sendTrigger(Trigger::AutoGainBoth).ok());   // explicitly listed trigger passes
        EXPECT_EQ(raw->writes, 1);
    }
    {
        auto [raw, sim] = mk(false);
        DeviceSession both{std::unique_ptr<IDeviceTransport>(raw),
                           WritePolicy{.allowPersistentChanges = true, .allowHardwareWrites = true,
                                       .hardwareTriggers = {Trigger::ResetLedColors, Trigger::FactoryReset}}};
        ASSERT_TRUE(both.connect().ok());
        EXPECT_TRUE(both.sendTrigger(Trigger::ResetLedColors).ok());
        EXPECT_EQ(both.sendTrigger(Trigger::FactoryReset).error().code, Errc::NotSupported);  // always
        EXPECT_EQ(raw->writes, 1);
    }
}

TEST(Status, MeasureOutcomeBits) {
    auto b = blob(0x40);
    auto st = [&](std::uint8_t v) { b[0x23] = v; return StatusMessage::fromBytes(b).value(); };
    using O = StatusMessage::MeasureOutcome;
    EXPECT_EQ(st(0x40).measureOutcome(), O::InProgressOrIdle);   // bit 6 wins
    EXPECT_EQ(st(0x60).measureOutcome(), O::InProgressOrIdle);
    EXPECT_EQ(st(0x00).measureOutcome(), O::Failed);             // bit 5 clear
    EXPECT_EQ(st(0x20).measureOutcome(), O::Succeeded);          // bit 5 set
    b[0x24] = 99; EXPECT_FALSE(StatusMessage::fromBytes(b).value().measurementFinished());
    b[0x24] = 100; EXPECT_TRUE(StatusMessage::fromBytes(b).value().measurementFinished());
}

TEST(Status, ClipCountersPerInput) {
    auto b = blob(0x40);
    b[0x2E] = 0x34; b[0x2F] = 0x12;
    b[0x30] = 0x07;
    auto m = StatusMessage::fromBytes(b).value();
    EXPECT_EQ(m.clipCounter(0), 0x1234);
    EXPECT_EQ(m.clipCounter(1), 7);
}

TEST(Merge, DeviceChangesWinUserEditsSurvive) {
    SettingsMessage snap;                                   // last device state
    SettingsMessage device = snap;
    ASSERT_TRUE(device.setHeadphoneVolumeDb(-20).ok());     // changed on the device
    SettingsMessage pending = snap;
    ASSERT_TRUE(pending.setGainDb(0, 40).ok());             // edited by the user
    ASSERT_TRUE(pending.setHeadphoneVolumeDb(-5).ok());     // also edited by user, but device changed it
    pending.mergeExternalChanges(snap, device);
    EXPECT_DOUBLE_EQ(pending.gainDb(0), 40.0);              // kept
    EXPECT_DOUBLE_EQ(pending.headphoneVolumeDb(), -20.0);   // device wins
}

TEST(Merge, FieldGranularityAndUndefinedBytes) {
    SettingsMessage snap;
    auto raw = Bytes(snap.bytes().begin(), snap.bytes().end());
    raw[100] = 0xAA;                                        // beyond byte 70: not a schema field
    auto device = SettingsMessage::fromBytes(raw).value();
    SettingsMessage pending = snap;
    pending.mergeExternalChanges(snap, device);
    EXPECT_EQ(pending.bytes()[100], 0);                     // app never syncs undefined bytes
}

TEST(Session, ExternalChangeIsMergedIntoPendingEdit) {
    auto m = std::make_unique<MockTransport>();
    MockTransport* mock = m.get();
    DeviceSession s(std::move(m));
    ASSERT_TRUE(s.connect().ok());
    ASSERT_TRUE(s.edit([](SettingsMessage& x) { return x.setGainDb(0, 33); }).ok());
    auto ext = mock->settingsBytes();
    ext[22] = 1;                                            // speaker mute pressed on hardware
    mock->setSettingsBytes(ext);
    ASSERT_TRUE(s.refreshSettings().ok());
    EXPECT_TRUE(s.settings()->speakerMute());               // device state adopted
    EXPECT_TRUE(s.dirty());                                 // user's gain edit still pending
    ASSERT_TRUE(s.commit().ok());
    EXPECT_DOUBLE_EQ(s.settings()->gainDb(0), 33.0);
    EXPECT_TRUE(s.settings()->speakerMute());
}

TEST(Mock, SimulatedMeasurementFailureAndSuccess) {
    for (bool ok : {true, false}) {
        auto m = std::make_unique<MockTransport>();
        MockTransport* mock = m.get();
        mock->setAutoGainOutcome(ok ? 0x20 : 0x00);
        DeviceSession s(std::move(m));
        ASSERT_TRUE(s.connect().ok());
        ASSERT_TRUE(s.sendTrigger(Trigger::AutoGainBoth).ok());
        for (int i = 0; i < 20 && mock->autoGainRunning(); ++i) ASSERT_TRUE(s.refreshStatus().ok());
        ASSERT_TRUE(s.refreshStatus().ok());
        EXPECT_EQ(s.status()->measureOutcome(),
                  ok ? StatusMessage::MeasureOutcome::Succeeded : StatusMessage::MeasureOutcome::Failed);
    }
}

TEST(ApiVersion, HandshakeAccepted) {
    auto m = std::make_unique<MockTransport>();
    DeviceSession s(std::move(m));
    ASSERT_TRUE(s.connect().ok());
    ASSERT_TRUE(s.apiVersion().has_value());
    EXPECT_EQ(s.apiVersion()->major(), 1);
    EXPECT_EQ(s.apiVersion()->minor(), 0);
}

TEST(ApiVersion, UnsupportedMajorRefusedBeforeAnyOtherTraffic) {
    auto m = std::make_unique<MockTransport>();
    m->setApiVersion(2, 0);
    MockTransport* mock = m.get();
    DeviceSession s(std::move(m));
    auto r = s.connect();
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::NotSupported);
    EXPECT_NE(r.error().message.find("API version not supported"), std::string::npos);
    EXPECT_FALSE(s.settings().has_value());           // settings were never read
    EXPECT_FALSE(mock->isOpen());
}

TEST(ApiVersion, ShortResponseIsAnError) {
    auto b = Bytes(0x20, 0);
    EXPECT_EQ(ApiVersionMessage::fromBytes(b).error().code, Errc::ShortTransfer);
}

TEST(Session, IdenticalEditDoesNotWrite) {
    auto m = std::make_unique<MockTransport>();
    MockTransport* mock = m.get();
    DeviceSession s(std::move(m));
    ASSERT_TRUE(s.connect().ok());
    double g = s.settings()->gainDb(0);
    ASSERT_TRUE(s.edit([&](SettingsMessage& x) { return x.setGainDb(0, g); }).ok());
    EXPECT_FALSE(s.dirty());
    ASSERT_TRUE(s.commit().ok());
    EXPECT_TRUE(mock->writeLog().empty());
    ASSERT_TRUE(s.edit([&](SettingsMessage& x) { return x.setGainDb(0, g + 1); }).ok());
    ASSERT_TRUE(s.commit().ok());
    EXPECT_EQ(mock->writeLog().size(), 1u);
}

TEST(Session, EffectiveSettingsShowPendingOverDevice) {
    auto m = std::make_unique<MockTransport>();
    MockTransport* mock = m.get();
    DeviceSession s(std::move(m));
    ASSERT_TRUE(s.connect().ok());
    ASSERT_TRUE(s.edit([](SettingsMessage& x) { return x.setSpeakerVolumeDb(-30); }).ok());
    EXPECT_DOUBLE_EQ(s.effectiveSettings()->speakerVolumeDb(), -30.0);
    EXPECT_DOUBLE_EQ(s.settings()->speakerVolumeDb(), 0.0);   // device state unchanged until commit
    mock->injectFault(MockTransport::Fault::Timeout);
    EXPECT_FALSE(s.commit().ok());
    EXPECT_TRUE(s.dirty());                                    // not silently dropped
}

TEST(ModeSwitch, SimulatedDeviceChangesPidButHardwareRefuses) {
    auto m = std::make_unique<MockTransport>();
    MockTransport* mock = m.get();
    DeviceSession s(std::move(m));
    ASSERT_TRUE(s.connect().ok());
    EXPECT_EQ(deviceModeFromPid(s.info().pid), DeviceMode::Normal);
    ASSERT_TRUE(s.sendTrigger(Trigger::SwitchToMacOSStreamingMode).ok());
    EXPECT_EQ(mock->mode(), DeviceMode::MacOSStreaming);
    EXPECT_EQ(s.info().pid, 0x0002);
    EXPECT_EQ(mock->triggers().back(), 0x104);
    ASSERT_TRUE(s.sendTrigger(Trigger::SwitchToNormalMode).ok());
    EXPECT_EQ(s.info().pid, 0x0004);
    EXPECT_EQ(mock->triggers().back(), 0x105);

    // A non-simulated transport never receives mode switches, even with hardware writes on.
    struct Fake final : IDeviceTransport {
        std::string name() const override { return "fake"; }
        Status open() override { return okStatus(); }
        void close() override {}
        bool isOpen() const override { return true; }
        DeviceInfo info() const override { return {}; }
        Result<Bytes> read(MessageId id) override {
            Bytes b(messageInfo(id)->size, 0);
            if (id == MessageId::ApiVersion) b[0] = 1;
            return b;
        }
        Status write(MessageId, std::span<const std::uint8_t>) override { ++writes; return okStatus(); }
        int writes{0};
    };
    auto f = std::make_unique<Fake>();
    Fake* fr = f.get();
    DeviceSession hw(std::move(f), WritePolicy{.allowRoutingChanges = true, .allowPersistentChanges = true,
                                                .allowHardwareWrites = true});
    ASSERT_TRUE(hw.connect().ok());
    EXPECT_EQ(hw.sendTrigger(Trigger::SwitchToMacOSStreamingMode).error().code, Errc::NotSupported);
    EXPECT_EQ(hw.sendTrigger(Trigger::SwitchToNormalMode).error().code, Errc::NotSupported);
    EXPECT_EQ(fr->writes, 0);
}

TEST(Triggers, AllCommandWordsAreDistinct) {
    std::set<int> seen;
    for (auto t : {Trigger::AutoGainMicrophone, Trigger::AutoGainInstrument, Trigger::AutoGainBoth,
                   Trigger::BeginMeasurementSession, Trigger::EndMeasurementSession,
                   Trigger::DenoiseMeasureMicrophone, Trigger::DenoiseMeasureInstrument,
                   Trigger::DenoiseMeasureBoth, Trigger::ResetLedColors,
                   Trigger::SwitchToMacOSStreamingMode, Trigger::SwitchToNormalMode, Trigger::FactoryReset})
        EXPECT_TRUE(seen.insert(static_cast<int>(t)).second);
    EXPECT_EQ(seen.size(), 12u);
    EXPECT_EQ(static_cast<int>(Trigger::BeginMeasurementSession), 4);
    EXPECT_EQ(static_cast<int>(Trigger::EndMeasurementSession), 5);
}

// Byte 59 "Map native volume controls to" (2026-10-31): 0 none, 1 speakers, 2 headphones; nothing else is accepted.
TEST(NativeVolumeMapping, AcceptsOnlyTheThreeOfficialValues) {
    SettingsMessage s = SettingsMessage::fromBytes(Bytes(SettingsMessage::kSize, 0)).value();
    for (std::uint8_t v : {0, 1, 2}) {
        ASSERT_TRUE(s.setNativeVolumeMapping(v).ok());
        EXPECT_EQ(s.nativeVolumeMapping(), v);
        EXPECT_EQ(s.bytes()[settings_off::kNativeVolumeMapping], v);
    }
    EXPECT_FALSE(s.setNativeVolumeMapping(3).ok());
    EXPECT_EQ(s.nativeVolumeMapping(), 2);   // unchanged after a refused value
}
