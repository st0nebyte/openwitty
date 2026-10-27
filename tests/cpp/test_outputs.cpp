// Output/monitoring/LED/policy tests. Byte vectors are hand-built from offsets
// decoded in the macOS binary; documented restrictions come from the official
// online guide. Simulated only -- nothing here is hardware-verified.
#include <gtest/gtest.h>

#include "core/DeviceSession.hpp"
#include "core/Policy.hpp"
#include "transport/MockTransport.hpp"

using namespace lewitt;

namespace {
Bytes blob(std::size_t n) { return Bytes(n, 0); }
SettingsMessage settingsFrom(const Bytes& b) { return SettingsMessage::fromBytes(b).value(); }
}  // namespace

TEST(Outputs, SpeakerAndHeadphoneAreIndependent) {
    auto b = blob(0x80);
    b[20] = 0x00; b[21] = 0xE2;  // 0xE200 = -7680 -> -30 dB (speaker)
    b[22] = 1;                    // speaker muted
    b[27] = 0x00; b[28] = 0xF4;  // 0xF400 = -3072 -> -12 dB (headphone)
    b[29] = 0;
    b[19] = 1;                    // loopback muted
    auto s = settingsFrom(b);
    EXPECT_DOUBLE_EQ(s.speakerVolumeDb(), -30.0);
    EXPECT_TRUE(s.speakerMute());
    EXPECT_DOUBLE_EQ(s.headphoneVolumeDb(), -12.0);
    EXPECT_FALSE(s.headphoneMute());
    EXPECT_TRUE(s.loopbackMute());
}

TEST(Outputs, SettingOneDoesNotTouchTheOther) {
    SettingsMessage s;
    ASSERT_TRUE(s.setSpeakerVolumeDb(-20).ok());
    EXPECT_DOUBLE_EQ(s.headphoneVolumeDb(), 0.0);
    ASSERT_TRUE(s.setHeadphoneMute(true).ok());
    EXPECT_FALSE(s.speakerMute());
    EXPECT_FALSE(s.setSpeakerVolumeDb(-61).ok());
    EXPECT_FALSE(s.setHeadphoneVolumeDb(1).ok());
}

TEST(Monitor, StateMachineMatchesAppPairTables) {
    // byte34: 0 = instrument only, 1 = microphone only, 2 = both. Never "none".
    struct Case { std::uint8_t from; bool mic; bool on; std::uint8_t to; };
    const Case cases[] = {
        {0, true, true, 2},  {1, true, true, 1},  {2, true, true, 2},   // enable mic
        {0, true, false, 0}, {1, true, false, 1}, {2, true, false, 0},  // disable mic
        {1, false, true, 2}, {0, false, true, 0}, {2, false, true, 2},  // enable instrument
        {1, false, false, 1}, {0, false, false, 0}, {2, false, false, 1},  // disable instrument
    };
    for (auto c : cases) {
        auto b = blob(0x80);
        b[34] = c.from;
        auto s = settingsFrom(b);
        ASSERT_TRUE((c.mic ? s.setMonitorMicrophone(c.on) : s.setMonitorInstrument(c.on)).ok());
        EXPECT_EQ(s.bytes()[34], c.to) << "from " << int(c.from) << (c.mic ? " mic " : " inst ") << c.on;
        EXPECT_TRUE(s.monitorMicrophone() || s.monitorInstrument());
    }
}

TEST(Monitor, UnknownValueRejectedAndPreserved) {
    auto b = blob(0x80);
    b[34] = 9;
    auto s = settingsFrom(b);
    EXPECT_EQ(s.setMonitorMicrophone(true).error().code, Errc::Malformed);
    EXPECT_EQ(s.bytes()[34], 9);
}

TEST(Monitor, InputPlaybackMixSignInverted) {
    auto b = blob(0x80);
    b[35] = 0x80; b[36] = 0xFF;  // raw -128 -> +0.5
    EXPECT_DOUBLE_EQ(settingsFrom(b).inputPlaybackMix(), 0.5);
    SettingsMessage s;
    ASSERT_TRUE(s.setInputPlaybackMix(-1.0).ok());
    EXPECT_EQ(rdI16(s.bytes(), 35), 256);  // stored inverted
    ASSERT_TRUE(s.setInputPlaybackMix(-100.0).ok());          // device scale (Phase 11A)
    EXPECT_EQ(rdI16(s.bytes(), 35), 25600);
    EXPECT_DOUBLE_EQ(s.inputPlaybackMix(), -100.0);
    EXPECT_FALSE(s.setInputPlaybackMix(0.5).ok());            // above the right stop
    EXPECT_FALSE(s.setInputPlaybackMix(-100.5).ok());
}

TEST(Leds, ColorBytesAreRgbAndBrightnessRange) {
    auto b = blob(0x80);
    b[38] = 0x11; b[39] = 0x22; b[40] = 0x33;   // icons
    b[41] = 0xFF; b[42] = 0x00; b[43] = 0x00;   // mute (factory red per guide)
    b[37] = 80;
    auto s = settingsFrom(b);
    EXPECT_EQ(s.ledColor(LedElement::Icons), (Rgb{0x11, 0x22, 0x33}));
    EXPECT_EQ(s.ledColor(LedElement::Mute), (Rgb{0xFF, 0, 0}));
    EXPECT_EQ(s.ledBrightness(), 80);
    EXPECT_TRUE(s.setLedBrightness(100).ok());
    EXPECT_FALSE(s.setLedBrightness(101).ok());
    EXPECT_FALSE(s.setLedBrightness(-1).ok());
    ASSERT_TRUE(s.setLedColor(LedElement::GainReduction, {1, 2, 3}).ok());
    EXPECT_EQ(s.bytes()[50], 1); EXPECT_EQ(s.bytes()[51], 2); EXPECT_EQ(s.bytes()[52], 3);
    EXPECT_EQ(s.bytes()[53], 0);  // custom-button byte untouched
}

TEST(Status, OutputMeterOffsets) {
    auto b = blob(0x40);
    auto put = [&](std::size_t o, int db) { wrI16(b, o, doubleToQ88(db)); };
    put(0x12, -10); put(0x14, -11);   // loopback L/R
    put(0x16, -20); put(0x18, -21);   // speaker L/R
    put(0x1A, -30); put(0x1C, -31);   // headphone L/R
    auto m = StatusMessage::fromBytes(b).value();
    EXPECT_DOUBLE_EQ(m.loopbackLevelDb(0), -10); EXPECT_DOUBLE_EQ(m.loopbackLevelDb(1), -11);
    EXPECT_DOUBLE_EQ(m.speakerLevelDb(0), -20);  EXPECT_DOUBLE_EQ(m.speakerLevelDb(1), -21);
    EXPECT_DOUBLE_EQ(m.headphoneLevelDb(0), -30); EXPECT_DOUBLE_EQ(m.headphoneLevelDb(1), -31);
}

TEST(Policy, DspRestrictionAt96k) {
    EXPECT_TRUE(dspAvailable(44100));
    EXPECT_TRUE(dspAvailable(48000));
    EXPECT_FALSE(dspAvailable(96000));
    EXPECT_FALSE(dspAvailable(192000));
}

namespace {
struct MockSession {
    MockTransport* mock;
    std::unique_ptr<DeviceSession> s;
    MockSession() {
        auto m = std::make_unique<MockTransport>();
        mock = m.get();
        s = std::make_unique<DeviceSession>(std::move(m));
    }
};
}  // namespace

TEST(Policy, SessionRejectsDenoiseAt96kButAllowsTurningOff) {
    MockSession f;
    ASSERT_TRUE(f.s->connect().ok());
    f.s->setSampleRateHint(96000);
    ASSERT_TRUE(f.s->edit([](SettingsMessage& m) { return m.setDenoise(0, true); }).ok());
    EXPECT_EQ(f.s->commit().error().code, Errc::NotSupported);
    // non-DSP changes still go through once the offending edit is replaced
    f.s->setSampleRateHint(48000);
    EXPECT_TRUE(f.s->commit().ok());
    EXPECT_TRUE(f.s->settings()->denoise(0));
    f.s->setSampleRateHint(96000);
    ASSERT_TRUE(f.s->edit([](SettingsMessage& m) { return m.setDenoise(0, false); }).ok());
    EXPECT_TRUE(f.s->commit().ok());
}

TEST(Policy, UnknownSampleRateDoesNotRestrict) {
    MockSession f;
    ASSERT_TRUE(f.s->connect().ok());
    ASSERT_EQ(f.s->sampleRate(), 0u);
    ASSERT_TRUE(f.s->edit([](SettingsMessage& m) { return m.setCompress(1, true); }).ok());
    EXPECT_TRUE(f.s->commit().ok());
}

TEST(Policy, RiskAssessment) {
    SettingsMessage a, b = a;
    ASSERT_TRUE(b.setPhantom1(true).ok());
    ASSERT_TRUE(b.setHighVolumeSpeaker(true).ok());
    auto r = assessChange(a, b);
    EXPECT_TRUE(r.phantomPowerEnabled);
    EXPECT_TRUE(r.highVolumeSpeaker);
    EXPECT_FALSE(r.routing);
    auto off = assessChange(b, a);  // turning things off is not flagged
    EXPECT_FALSE(off.phantomPowerEnabled);
    EXPECT_FALSE(off.highVolumeSpeaker);
}

TEST(Mock, SimulatedAutoGainRuns) {
    MockSession f;
    ASSERT_TRUE(f.s->connect().ok());
    ASSERT_TRUE(f.s->sendTrigger(Trigger::AutoGainMicrophone).ok());
    EXPECT_TRUE(f.mock->autoGainRunning());
    int last = -1; int reads = 0;
    while (f.mock->autoGainRunning() && reads++ < 20) {
        ASSERT_TRUE(f.s->refreshStatus().ok());
        int p = f.s->status()->autosetupProgress();
        EXPECT_GE(p, last);   // monotonic progress
        last = p;
    }
    EXPECT_EQ(last, 100);
    ASSERT_TRUE(f.s->refreshStatus().ok());
    EXPECT_FALSE(f.mock->autoGainRunning());
}
