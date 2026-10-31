// Phase 8D: DesktopVolumeLink against a simulated device (MockTransport posing as hardware) and a
// simulated ALSA control. No real ALSA, no real USB.
#include <gtest/gtest.h>

#include <unistd.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

#include "FakeHardware.hpp"
#include "core/OutputControl.hpp"
#include "desktop/DesktopMixer.hpp"
#include "desktop/DesktopVolumeLink.hpp"

using namespace lewitt;
using namespace lewitt::testing;
using namespace std::chrono_literals;

namespace {

class MockMixer final : public IDesktopMixer {
public:
    Status open() override { open_ = true; return okStatus(); }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    int cardIndex() const override { return open_ ? card : -1; }
    std::optional<DesktopLevel> read() override { return present ? std::optional(level) : std::nullopt; }
    Status write(const DesktopLevel& d) override {
        ++writes;
        level = DesktopLevel{std::floor(d.db * 2 + 1e-9) / 2, d.muted};   // ALSA 0.5 dB grid, rounded down
        return okStatus();
    }
    int waitForEvent(int) override { return 0; }
    DesktopLevel level{-30, false};
    bool present{true};
    int writes{0};
    int card{3};
private:
    bool open_{true};
};

struct Rig {
    MockTransport mock;
    FakeHardware* hw{nullptr};
    std::unique_ptr<DeviceSession> session;
    MockMixer mixer;
    std::unique_ptr<DesktopVolumeLink> link;
    std::vector<std::string> log;
    VolumeSync::TimePoint t0 = VolumeSync::TimePoint{} + 100s;

    explicit Rig(DeviceWriteMode mode = DeviceWriteMode::Fallback, bool desktopWrites = true, double hwDb = -30,
                 bool hwMuted = false) {
        auto t = std::make_unique<FakeHardware>(&mock);
        hw = t.get();
        session = std::make_unique<DeviceSession>(std::move(t), outputWritePolicy(true, false));
        EXPECT_TRUE(session->connect().ok());
        setDevice(hwDb, hwMuted);
        LinkOptions o;
        o.deviceWrites = mode;
        o.desktopWrites = desktopWrites;
        o.verify = fastPoll();
        link = std::make_unique<DesktopVolumeLink>(*session, mixer, VolumeSyncConfig{}, o,
                                                   [this](const std::string& s) { log.push_back(s); });
    }
    // Change the device directly (hardware knob / FU2 following by itself).
    void setDevice(double db, bool muted) {
        auto b = mock.settingsBytes();
        auto r = SettingsMessage::fromBytes(b);
        ASSERT_TRUE(r.ok());
        SettingsMessage m = r.value();
        ASSERT_TRUE(stageOutput(m, Output::Speaker, db, muted).ok());
        auto bytes = m.bytes();
        mock.setSettingsBytes(Bytes(bytes.begin(), bytes.end()));
    }
    OutputLevel device() {
        auto r = SettingsMessage::fromBytes(mock.settingsBytes());
        return outputLevel(r.value(), Output::Speaker);
    }
    OutputLevel headphone() {
        auto r = SettingsMessage::fromBytes(mock.settingsBytes());
        return outputLevel(r.value(), Output::Headphone);
    }
};

}  // namespace

TEST(DesktopLink, StartupIsReadOnlyForTheDeviceEvenIfTheDesktopIsLouder) {
    Rig r;
    r.mixer.level = {0.0, false};                               // KDE says 100 %
    ASSERT_TRUE(r.link->start(r.t0));
    EXPECT_EQ(r.hw->writeCalls, 0);
    EXPECT_DOUBLE_EQ(r.device().db, -30);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -30);                    // desktop now shows the real level
    // the echo of that alignment is not a request
    r.link->onDesktopEvent(r.t0 + 50ms);
    EXPECT_EQ(r.hw->writeCalls, 0);
}

TEST(DesktopLink, DryRunWritesNothingAnywhere) {
    Rig r(DeviceWriteMode::None, /*desktopWrites=*/false);
    r.mixer.level = {0.0, false};
    ASSERT_TRUE(r.link->start(r.t0));
    r.mixer.level = {-20.0, false};
    r.link->onDesktopEvent(r.t0 + 10s);
    r.link->onPollTick(r.t0 + 11s);
    EXPECT_EQ(r.hw->writeCalls, 0);
    EXPECT_EQ(r.mixer.writes, 0);
    bool sawDryRun = false;
    for (auto& l : r.log) sawDryRun |= l.find("dry run: would write speaker") != std::string::npos;
    EXPECT_TRUE(sawDryRun);
}

TEST(DesktopLink, DesktopChangeAfterTheHoldIsWrittenVerifiedAndHeadphoneIsUntouched) {
    Rig r;
    const auto hpBefore = r.headphone();
    ASSERT_TRUE(r.link->start(r.t0));
    r.mixer.level = {-36.0, false};
    r.link->onDesktopEvent(r.t0 + 6s);
    EXPECT_EQ(r.hw->writeCalls, 0);                             // the kernel path gets its grace period first
    r.link->onPollTick(r.t0 + 7600ms);
    EXPECT_EQ(r.hw->writeCalls, 1);
    EXPECT_DOUBLE_EQ(r.device().db, -36);
    EXPECT_EQ(r.link->sync().phase(), SyncPhase::Synced);
    EXPECT_DOUBLE_EQ(r.headphone().db, hpBefore.db);
    EXPECT_EQ(r.headphone().muted, hpBefore.muted);
}

TEST(DesktopLink, DeviceThatAlreadyFollowedIsNotWrittenAgain) {
    Rig r;
    ASSERT_TRUE(r.link->start(r.t0));
    r.mixer.level = {-20.0, false};
    r.setDevice(-20.0, false);                                  // healthy FU2: the kernel already applied it
    r.link->onDesktopEvent(r.t0 + 6s);
    EXPECT_EQ(r.hw->writeCalls, 0);
    EXPECT_DOUBLE_EQ(r.device().db, -20);
}

TEST(DesktopLink, KnobChangeReachesTheDesktopWithoutAWriteOrLoop) {
    Rig r;
    ASSERT_TRUE(r.link->start(r.t0));
    r.setDevice(-14.0, false);                                  // knob turned up
    r.link->onPollTick(r.t0 + 7s);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -14);
    r.link->onDesktopEvent(r.t0 + 7s + 30ms);                   // ALSA event caused by our own update
    r.link->onPollTick(r.t0 + 8s);
    EXPECT_EQ(r.hw->writeCalls, 0);
}

TEST(DesktopLink, IgnoredWriteIsNotRetriedAndTheDesktopIsCorrected) {
    Rig r;
    ASSERT_TRUE(r.link->start(r.t0));
    r.hw->dropWrites = true;                                    // device ignores the SET
    r.mixer.level = {-25.0, false};
    r.link->onDesktopEvent(r.t0 + 6s);
    r.link->onPollTick(r.t0 + 7600ms);                          // grace passed: the one vendor write
    EXPECT_EQ(r.hw->writeCalls, 1);
    EXPECT_EQ(r.link->sync().phase(), SyncPhase::Uncertain);
    r.link->onDesktopEvent(r.t0 + 7700ms);
    EXPECT_EQ(r.hw->writeCalls, 1);                             // nothing re-sent
    r.link->onPollTick(r.t0 + 8s);
    EXPECT_EQ(r.link->sync().phase(), SyncPhase::Synced);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -30);                    // desktop shows the real (unchanged) level
    for (int i = 1; i <= 4; ++i) r.link->onPollTick(r.t0 + 8s + std::chrono::seconds(i));
    EXPECT_EQ(r.hw->writeCalls, 1);                             // and never retried later either
}

TEST(DesktopLink, MuteAndUnmuteFollowTheDesktop) {
    Rig r;
    ASSERT_TRUE(r.link->start(r.t0));
    r.mixer.level = {-30.0, true};
    r.link->onDesktopEvent(r.t0 + 6s);
    r.link->onPollTick(r.t0 + 7600ms);
    EXPECT_TRUE(r.device().muted);
    r.mixer.level = {-30.0, false};
    r.link->onDesktopEvent(r.t0 + 8s);
    r.link->onPollTick(r.t0 + 9600ms);
    EXPECT_FALSE(r.device().muted);
    EXPECT_DOUBLE_EQ(r.device().db, -30);
    EXPECT_EQ(r.hw->writeCalls, 2);
}

TEST(DesktopLink, LostDeviceMakesTheLinkUnhealthyWithoutWrites) {
    Rig r;
    ASSERT_TRUE(r.link->start(r.t0));
    r.mock.setPresent(false);
    for (int i = 0; i < 4; ++i) r.link->onPollTick(r.t0 + std::chrono::seconds(7 + i));
    r.mixer.level = {-10.0, false};
    r.link->onDesktopEvent(r.t0 + 12s);
    EXPECT_FALSE(r.link->healthy());
    EXPECT_EQ(r.hw->writeCalls, 0);
}

TEST(DesktopLink, RestartAfterReconnectIsReadOnlyAgain) {
    Rig r;
    ASSERT_TRUE(r.link->start(r.t0));
    r.link->reset();
    r.mixer.level = {0.0, false};                               // the desktop moved while we were away
    ASSERT_TRUE(r.link->start(r.t0 + 60s));
    EXPECT_EQ(r.hw->writeCalls, 0);
    EXPECT_DOUBLE_EQ(r.device().db, -30);
}

TEST(DesktopMixerHelpers, CardIsFoundByUsbIdOnlyAndNeverGuessed) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("ow-asound-" + std::to_string(::getpid()));
    fs::remove_all(root);
    auto card = [&](int n, const char* id) {
        fs::create_directories(root / ("card" + std::to_string(n)));
        if (id) std::ofstream(root / ("card" + std::to_string(n)) / "usbid") << id << "\n";
    };
    card(0, nullptr);
    card(1, "1234:5678");
    card(3, "29C2:0004");
    fs::create_directories(root / "cards");
    EXPECT_EQ(findCardByUsbId(root.string(), "29c2:0004"), 3);
    fs::remove_all(root / "card3");
    card(5, "29c2:0004");                                       // reconnect: new card number
    EXPECT_EQ(findCardByUsbId(root.string(), "29c2:0004"), 5);
    card(6, "29c2:0004");                                       // two CONNECT 2: ambiguous -> no card
    EXPECT_FALSE(findCardByUsbId(root.string(), "29c2:0004").has_value());
    fs::remove_all(root);
    EXPECT_FALSE(findCardByUsbId(root.string(), "29c2:0004").has_value());
}

TEST(DesktopMixerHelpers, RawDbMappingMatchesTheConnect2Control) {
    // PCM Playback Volume: 0..120, -60..0 dB (0.5 dB steps)
    EXPECT_DOUBLE_EQ(rawToDb(0, 0, 120, -60, 0), -60);
    EXPECT_DOUBLE_EQ(rawToDb(73, 0, 120, -60, 0), -23.5);
    EXPECT_DOUBLE_EQ(rawToDb(120, 0, 120, -60, 0), 0);
    EXPECT_DOUBLE_EQ(rawToDb(500, 0, 120, -60, 0), 0);          // clamped
    EXPECT_EQ(dbToRaw(-23.5, 0, 120, -60, 0), 73);
    EXPECT_EQ(dbToRaw(-23.6, 0, 120, -60, 0), 73);
    EXPECT_EQ(dbToRaw(-100, 0, 120, -60, 0), 0);
    EXPECT_EQ(dbToRaw(10, 0, 120, -60, 0), 120);
    EXPECT_EQ(dbToRaw(std::numeric_limits<double>::quiet_NaN(), 0, 120, -60, 0), 0);   // never louder on NaN
    EXPECT_EQ(dbToRaw(-23.4, 0, 120, -60, 0, /*roundDown=*/true), 73);          // -23.5, not -23.0
    EXPECT_EQ(dbToRaw(-23.5, 0, 120, -60, 0, true), 73);                        // exact grid value kept
    EXPECT_EQ(dbToRaw(0.0, 0, 120, -60, 0, true), 120);
    for (double db = -60; db <= 0; db += 0.137)
        EXPECT_LE(rawToDb(dbToRaw(db, 0, 120, -60, 0, true), 0, 120, -60, 0), db + 1e-9);   // never louder
}

TEST(DesktopLink, NeverPiggybacksOnAnotherPendingEdit) {
    Rig r;
    ASSERT_TRUE(r.link->start(r.t0));
    // the GUI left a failed LED edit pending for the user to retry/discard
    ASSERT_TRUE(r.session->edit([](SettingsMessage& m) { return m.setLedBrightness(7); }).ok());
    r.mixer.level = {-36.0, false};
    r.link->onDesktopEvent(r.t0 + 6s);
    r.link->onPollTick(r.t0 + 7600ms);
    EXPECT_EQ(r.hw->writeCalls, 0);
    ASSERT_TRUE(r.session->pendingSettings().has_value());       // the other edit is left untouched
    EXPECT_EQ(r.session->pendingSettings()->ledBrightness(), 7);
    EXPECT_EQ(r.link->sync().phase(), SyncPhase::Synced);
    r.session->discardPending();                                 // user discards it: the next move applies
    r.mixer.level = {-37.0, false};
    r.link->onDesktopEvent(r.t0 + 8s);
    r.link->onPollTick(r.t0 + 9600ms);
    EXPECT_EQ(r.hw->writeCalls, 1);
    EXPECT_DOUBLE_EQ(r.device().db, -37);
}

TEST(DesktopLink, HostSampleCostsOnlyTheSafetyReReadBeforeAnAlignment) {
    Rig r;
    ASSERT_TRUE(r.link->start(r.t0));
    int reads = r.hw->readCalls;
    r.link->onHardwareSample(OutputLevel{-30.0, false}, r.t0 + 7s);   // unchanged: no I/O at all
    EXPECT_EQ(r.hw->readCalls, reads);
    r.setDevice(-12.0, false);
    r.link->onHardwareSample(OutputLevel{-12.0, false}, r.t0 + 8s);   // knob moved: one re-read, then align
    EXPECT_EQ(r.hw->readCalls, reads + 1);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -12);
    EXPECT_EQ(r.hw->writeCalls, 0);
}

TEST(DesktopLink, AlignmentNeverWritesALevelTheDeviceNoLongerHas) {
    Rig r;
    ASSERT_TRUE(r.link->start(r.t0));
    // the host sampled -12 dB, but the knob went down to -40 dB before the alignment could happen
    r.setDevice(-40.0, false);
    r.link->onHardwareSample(OutputLevel{-12.0, false}, r.t0 + 7s);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -40);                     // never the stale, louder -12
    EXPECT_EQ(r.hw->writeCalls, 0);
}

TEST(DesktopLink, AlignmentNeverUnmutesAndExplainsTheDifference) {
    Rig r;                                                       // device unmuted at -30
    r.mixer.level = {-30.0, true};                               // desktop shows muted
    ASSERT_TRUE(r.link->start(r.t0));
    EXPECT_TRUE(r.mixer.level.muted);                            // left muted
    EXPECT_EQ(r.hw->writeCalls, 0);
    EXPECT_NE(r.link->notice().find("not muted"), std::string::npos);
    r.mixer.level = {-30.0, false};                              // user unmutes in KDE
    r.link->onDesktopEvent(r.t0 + 6s);
    EXPECT_TRUE(r.link->notice().empty());
    EXPECT_EQ(r.hw->writeCalls, 0);                              // device already unmuted
}

TEST(DesktopLink, AlignmentRoundsDownToTheControlGrid) {
    Rig r(DeviceWriteMode::Fallback, true, -23.4, false);        // between two 0.5 dB steps
    r.mixer.level = {0.0, false};
    ASSERT_TRUE(r.link->start(r.t0));
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -23.5);                   // not -23.0
    EXPECT_EQ(r.hw->writeCalls, 0);
}

TEST(DesktopLink, ChangeRejectedForAPendingEditIsVisibleAndRestoresConsistency) {
    Rig r;
    ASSERT_TRUE(r.link->start(r.t0));
    ASSERT_TRUE(r.session->edit([](SettingsMessage& m) { return m.setLedBrightness(7); }).ok());
    r.mixer.level = {-36.0, false};
    r.link->onDesktopEvent(r.t0 + 6s);
    r.link->onPollTick(r.t0 + 7600ms);
    EXPECT_EQ(r.hw->writeCalls, 0);                              // the pending edit is not committed
    EXPECT_EQ(r.session->pendingSettings()->ledBrightness(), 7);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -30);                     // KDE shows the real level again
    EXPECT_NE(r.link->notice().find("not applied"), std::string::npos);
    r.session->discardPending();
    r.link->onHardwareSample(OutputLevel{-30.0, false}, r.t0 + 8s);
    EXPECT_TRUE(r.link->notice().empty());
}

// Native (default) mode: KDE/PipeWire stay authoritative through the kernel path; the link only mirrors.
TEST(DesktopLinkNative, DeviceThatFollowsTheKernelIsLeftAlone) {
    Rig r(DeviceWriteMode::Native);
    ASSERT_TRUE(r.link->start(r.t0));
    r.mixer.level = {-40.0, false};
    r.setDevice(-40.0, false);                                  // the kernel's SET_CUR reached the device
    r.link->onDesktopEvent(r.t0 + 6s);
    EXPECT_EQ(r.hw->writeCalls, 0);
    EXPECT_TRUE(r.link->notice().empty());
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -40);
}

TEST(DesktopLinkNative, ADesktopChangeTheDeviceIgnoresIsReportedNeverWritten) {
    Rig r(DeviceWriteMode::Native);
    ASSERT_TRUE(r.link->start(r.t0));
    r.mixer.level = {-40.0, false};                             // device ignores FU2: stays at -30
    r.link->onDesktopEvent(r.t0 + 6s);
    EXPECT_TRUE(r.link->notice().empty());                      // not judged before the grace period
    r.link->onPollTick(r.t0 + 7s);
    EXPECT_TRUE(r.link->notice().empty());
    r.link->onPollTick(r.t0 + 7600ms);                          // grace (1.5 s) passed, still not followed
    EXPECT_EQ(r.hw->writeCalls, 0);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -40);                    // the user's desktop value is not overwritten
    EXPECT_NE(r.link->notice().find("did not follow"), std::string::npos);
    r.mixer.level = {-30.0, false};                             // back in step (user moved back)
    r.link->onDesktopEvent(r.t0 + 8s);
    EXPECT_TRUE(r.link->notice().empty());
    EXPECT_EQ(r.hw->writeCalls, 0);
}

TEST(DesktopLinkNative, KnobIsMirroredWithoutAnyDeviceWrite) {
    Rig r(DeviceWriteMode::Native);
    ASSERT_TRUE(r.link->start(r.t0));
    r.setDevice(-22.0, false);
    r.link->onPollTick(r.t0 + 7s);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -22);
    r.setDevice(-26.0, true);                                   // knob down and mute button
    r.link->onPollTick(r.t0 + 9s);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -26);
    EXPECT_TRUE(r.mixer.level.muted);
    EXPECT_EQ(r.hw->writeCalls, 0);
}

TEST(DesktopLinkNative, LargeDesktopIncreaseIsTheUsersChoiceAndNotCorrected) {
    Rig r(DeviceWriteMode::Native);
    ASSERT_TRUE(r.link->start(r.t0));
    r.mixer.level = {-5.0, false};
    r.setDevice(-5.0, false);                                   // the kernel applied the user's slider
    r.link->onDesktopEvent(r.t0 + 6s);
    EXPECT_EQ(r.hw->writeCalls, 0);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -5);                     // no clamp fight with the native path
}

// Phase 8H (observed on the device): the CONNECT 2 reports a level the kernel forwarded slightly after the
// ALSA control changed. During fast volume-key presses a single immediate read showed the previous step.
TEST(DesktopLinkNative, DeviceThatFollowsLateIsNotReported) {
    Rig r(DeviceWriteMode::Native, true, -23.5, false);
    ASSERT_TRUE(r.link->start(r.t0));
    r.mixer.level = {-20.0, false};                             // volume key up; the device still shows -23.5
    r.link->onDesktopEvent(r.t0 + 60s);
    r.setDevice(-20.0, false);                                  // ...and applies it a moment later
    r.link->onPollTick(r.t0 + 60s + 400ms);
    r.link->onPollTick(r.t0 + 63s);
    EXPECT_TRUE(r.link->notice().empty());
    EXPECT_EQ(r.hw->writeCalls, 0);
    for (auto& l : r.log) EXPECT_EQ(l.find("did not follow"), std::string::npos) << l;
}

TEST(DesktopLinkNative, FastKeyPressesWithALaggingDeviceGiveNoFalseNotice) {
    Rig r(DeviceWriteMode::Native, true, -23.5, false);
    ASSERT_TRUE(r.link->start(r.t0));
    const auto t = r.t0 + 60s;
    // the sequence of the observed incident: up, up, down, down; the device is always one step behind
    r.mixer.level = {-20.0, false};
    r.link->onDesktopEvent(t);
    r.setDevice(-20.0, false);
    r.mixer.level = {-17.5, false};
    r.link->onDesktopEvent(t + 160ms);
    r.setDevice(-17.5, false);
    r.mixer.level = {-20.0, false};
    r.link->onDesktopEvent(t + 500ms);
    r.setDevice(-20.0, false);
    r.mixer.level = {-23.0, false};
    r.link->onDesktopEvent(t + 700ms);
    r.setDevice(-23.0, false);
    for (int i = 1; i <= 4; ++i) r.link->onPollTick(t + 700ms + std::chrono::seconds(i));
    EXPECT_TRUE(r.link->notice().empty());
    EXPECT_EQ(r.hw->writeCalls, 0);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -23);
    for (auto& l : r.log) EXPECT_EQ(l.find("did not follow"), std::string::npos) << l;
}

TEST(DesktopLinkNative, ANewerDesktopChangeRestartsTheGracePeriod) {
    Rig r(DeviceWriteMode::Native);
    ASSERT_TRUE(r.link->start(r.t0));
    r.mixer.level = {-34.0, false};
    r.link->onDesktopEvent(r.t0 + 6s);
    r.mixer.level = {-38.0, false};                             // next key press 1.2 s later
    r.link->onDesktopEvent(r.t0 + 7200ms);
    r.link->onPollTick(r.t0 + 7800ms);                          // 1.8 s after the first, 0.6 s after the second
    EXPECT_TRUE(r.link->notice().empty());
    r.link->onPollTick(r.t0 + 8800ms);                          // the device never followed
    EXPECT_NE(r.link->notice().find("did not follow"), std::string::npos);
    EXPECT_EQ(r.hw->writeCalls, 0);
}

TEST(DesktopLinkNative, IgnoredMuteIsReportedAfterTheGracePeriod) {
    Rig r(DeviceWriteMode::Native);
    ASSERT_TRUE(r.link->start(r.t0));
    r.mixer.level = {-30.0, true};                              // KDE mute; the device stays unmuted
    r.link->onDesktopEvent(r.t0 + 6s);
    EXPECT_EQ(r.link->notice().find("did not follow"), std::string::npos);   // (the mute difference is explained)
    r.link->onHardwareSample(OutputLevel{-30.0, false}, r.t0 + 8s);
    EXPECT_NE(r.link->notice().find("did not follow"), std::string::npos);
    EXPECT_EQ(r.hw->writeCalls, 0);
    EXPECT_FALSE(r.device().muted);
}

// Phase 8H follow-up: a delayed intermediate hardware report during a burst of desktop changes must not be
// mistaken for a knob change (which would mirror the stale level back and, through the kernel, undo the
// user's latest request).
TEST(DesktopLinkNative, StaleIntermediateReportDoesNotOverwriteTheLatestDesktopRequest) {
    Rig r(DeviceWriteMode::Native);                            // device and desktop at -30
    ASSERT_TRUE(r.link->start(r.t0));
    const int alsaWritesBefore = r.mixer.writes;
    r.mixer.level = {-34.0, false};                            // desktop requests A
    r.link->onDesktopEvent(r.t0 + 6s);
    r.mixer.level = {-38.0, false};                            // ...then B, before the device reported A
    r.link->onDesktopEvent(r.t0 + 6300ms);
    r.setDevice(-34.0, false);                                 // delayed intermediate report: A
    r.link->onPollTick(r.t0 + 6600ms);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -38);                   // the latest request stays
    EXPECT_EQ(r.mixer.writes, alsaWritesBefore);               // no ALSA write of the stale level
    r.setDevice(-38.0, false);                                 // finally B
    r.link->onPollTick(r.t0 + 7s);
    r.link->onPollTick(r.t0 + 9s);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -38);
    EXPECT_EQ(r.mixer.writes, alsaWritesBefore);
    EXPECT_TRUE(r.link->notice().empty());
    EXPECT_EQ(r.hw->writeCalls, 0);
}

// Phase 8H follow-up: with a healthy kernel path the device reports a desktop change 0.4-0.7 s late; the
// vendor fallback must give the kernel path that time instead of writing the device itself.
TEST(DesktopLinkFallback, HealthyButLaggingKernelPathGetsNoVendorWrite) {
    Rig r(DeviceWriteMode::Fallback);
    ASSERT_TRUE(r.link->start(r.t0));
    r.mixer.level = {-34.0, false};
    r.link->onDesktopEvent(r.t0 + 6s);                         // device still shows -30
    EXPECT_EQ(r.hw->writeCalls, 0);
    r.setDevice(-34.0, false);                                 // the kernel's SET becomes visible 0.7 s later
    r.link->onPollTick(r.t0 + 6700ms);
    r.link->onPollTick(r.t0 + 9s);
    EXPECT_EQ(r.hw->writeCalls, 0);
    EXPECT_DOUBLE_EQ(r.device().db, -34);
    EXPECT_TRUE(r.link->notice().empty());
}

TEST(DesktopLinkNative, KnobTurnedDuringAPendingDesktopRequestIsStillMirrored) {
    Rig r(DeviceWriteMode::Native);
    ASSERT_TRUE(r.link->start(r.t0));
    r.mixer.level = {-34.0, false};                            // desktop request, device not there yet
    r.link->onDesktopEvent(r.t0 + 6s);
    r.setDevice(-20.0, false);                                 // the knob: a level the desktop never asked for
    r.link->onPollTick(r.t0 + 6400ms);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -20);                   // the knob wins, mirrored at once
    r.link->onPollTick(r.t0 + 9s);
    EXPECT_TRUE(r.link->notice().empty());
    EXPECT_EQ(r.hw->writeCalls, 0);
}

TEST(DesktopLinkNative, AnOldRequestNoLongerMasksAKnobChange) {
    Rig r(DeviceWriteMode::Native);
    ASSERT_TRUE(r.link->start(r.t0));
    r.mixer.level = {-34.0, false};
    r.link->onDesktopEvent(r.t0 + 6s);
    r.setDevice(-34.0, false);                                 // followed
    r.link->onPollTick(r.t0 + 6500ms);
    r.mixer.level = {-38.0, false};
    r.link->onDesktopEvent(r.t0 + 7s);
    r.setDevice(-38.0, false);
    r.link->onPollTick(r.t0 + 7500ms);
    r.mixer.level = {-42.0, false};                            // a later request the device has not reached...
    r.link->onDesktopEvent(r.t0 + 10s);
    r.setDevice(-34.0, false);                                 // ...and the knob lands on -34, requested 4 s ago
    r.link->onPollTick(r.t0 + 10300ms);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -34);                   // that request expired: this is the knob
    EXPECT_EQ(r.hw->writeCalls, 0);
}

TEST(DesktopLinkFallback, BurstWithAnIgnoringDeviceWritesOnlyTheLatestTargetOnce) {
    Rig r(DeviceWriteMode::Fallback);
    ASSERT_TRUE(r.link->start(r.t0));
    r.mixer.level = {-34.0, false};
    r.link->onDesktopEvent(r.t0 + 6s);                         // the device ignores the kernel: stays at -30
    r.mixer.level = {-38.0, false};
    r.link->onDesktopEvent(r.t0 + 6300ms);
    r.link->onPollTick(r.t0 + 7s);
    EXPECT_EQ(r.hw->writeCalls, 0);                            // still within the grace of -38
    r.link->onPollTick(r.t0 + 7900ms);
    EXPECT_EQ(r.hw->writeCalls, 1);
    EXPECT_DOUBLE_EQ(r.device().db, -38);                      // the latest request, never the older -34
    for (int i = 1; i <= 4; ++i) r.link->onPollTick(r.t0 + 8s + std::chrono::seconds(i));
    EXPECT_EQ(r.hw->writeCalls, 1);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -38);
}

TEST(DesktopLinkFallback, StaleIntermediateReportOfALaggingKernelPathCausesNoWrite) {
    Rig r(DeviceWriteMode::Fallback);
    ASSERT_TRUE(r.link->start(r.t0));
    const int alsaWritesBefore = r.mixer.writes;
    r.mixer.level = {-34.0, false};
    r.link->onDesktopEvent(r.t0 + 6s);
    r.mixer.level = {-38.0, false};
    r.link->onDesktopEvent(r.t0 + 6300ms);
    r.setDevice(-34.0, false);
    r.link->onPollTick(r.t0 + 6700ms);
    r.setDevice(-38.0, false);
    r.link->onPollTick(r.t0 + 7000ms);
    r.link->onPollTick(r.t0 + 10s);
    EXPECT_EQ(r.hw->writeCalls, 0);
    EXPECT_EQ(r.mixer.writes, alsaWritesBefore);
    EXPECT_DOUBLE_EQ(r.device().db, -38);
}

TEST(DesktopLinkFallback, IncreaseLimitStillAppliesAfterTheGracePeriod) {
    Rig r(DeviceWriteMode::Fallback);
    ASSERT_TRUE(r.link->start(r.t0));
    r.mixer.level = {-5.0, false};                             // +25 dB; the device ignores the kernel
    r.link->onDesktopEvent(r.t0 + 6s);
    EXPECT_EQ(r.hw->writeCalls, 0);
    r.link->onPollTick(r.t0 + 7600ms);
    EXPECT_EQ(r.hw->writeCalls, 1);
    EXPECT_DOUBLE_EQ(r.device().db, -20);                      // at most 10 dB louder per step
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -20);                   // and the desktop shows what was applied
}

namespace {
void setMapping(Rig& r, std::uint8_t v, double hpDb, bool hpMuted) {
    auto m = SettingsMessage::fromBytes(r.mock.settingsBytes()).value();
    ASSERT_TRUE(m.setNativeVolumeMapping(v).ok());
    ASSERT_TRUE(stageOutput(m, Output::Headphone, hpDb, hpMuted).ok());
    auto b = m.bytes();
    r.mock.setSettingsBytes(Bytes(b.begin(), b.end()));
}
}  // namespace

// Phase 12B findings: when byte 59 moves the native volume to the headphones, the desktop first shows the headphone
// level AND its mute (so a mute key press acts on the real state), nothing is written to the device, and the link
// does not loop on the speaker level.
TEST(DesktopLink, FollowsTheNativeVolumeMappingWithLevelAndMute) {
    Rig r(DeviceWriteMode::Native);
    ASSERT_TRUE(r.link->start(r.t0));
    ASSERT_EQ(r.link->target(), Output::Speaker);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -30);
    setMapping(r, 2, -32.5, true);                              // headphones, muted
    r.link->onPollTick(r.t0 + 1s);
    EXPECT_EQ(r.link->target(), Output::Headphone);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -32.5);
    EXPECT_TRUE(r.mixer.level.muted) << "the desktop must show the headphones' mute";
    EXPECT_EQ(r.hw->writeCalls, 0);
    // later polls are quiet: no repeated desktop writes, no device writes
    const int dw = r.mixer.writes;
    for (int i = 2; i < 12; ++i) r.link->onPollTick(r.t0 + std::chrono::seconds(i));
    EXPECT_EQ(r.mixer.writes, dw);
    EXPECT_EQ(r.hw->writeCalls, 0);
    // back to the speakers: the desktop shows the speaker level, unmuted
    setMapping(r, 1, -32.5, true);
    r.link->onPollTick(r.t0 + 20s);
    EXPECT_EQ(r.link->target(), Output::Speaker);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -30);
    EXPECT_FALSE(r.mixer.level.muted);
    EXPECT_EQ(r.hw->writeCalls, 0);
}

TEST(DesktopLink, NoneMappingLeavesEverythingAlone) {
    Rig r(DeviceWriteMode::Fallback);
    ASSERT_TRUE(r.link->start(r.t0));
    setMapping(r, 0, -32.5, true);
    r.link->onPollTick(r.t0 + 1s);
    EXPECT_FALSE(r.link->target().has_value());
    const int dw = r.mixer.writes;
    r.mixer.level = {-6.0, false};                              // the user moves KDE: nothing reaches the device
    r.link->onDesktopEvent(r.t0 + 8s);
    r.link->onPollTick(r.t0 + 10s);
    EXPECT_EQ(r.hw->writeCalls, 0);
    EXPECT_EQ(r.mixer.writes, dw);
    EXPECT_TRUE(r.link->healthy());
}
