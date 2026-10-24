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
        level = DesktopLevel{std::round(d.db * 2) / 2, d.muted};   // ALSA 0.5 dB grid
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

    explicit Rig(bool hardwareWrites = true, bool desktopWrites = true, double hwDb = -30, bool hwMuted = false) {
        auto t = std::make_unique<FakeHardware>(&mock);
        hw = t.get();
        session = std::make_unique<DeviceSession>(std::move(t), outputWritePolicy(true, false));
        EXPECT_TRUE(session->connect().ok());
        setDevice(hwDb, hwMuted);
        LinkOptions o;
        o.hardwareWrites = hardwareWrites;
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
    Rig r(/*hardwareWrites=*/false, /*desktopWrites=*/false);
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
    EXPECT_EQ(r.hw->writeCalls, 1);
    EXPECT_EQ(r.link->sync().phase(), SyncPhase::Uncertain);
    r.link->onDesktopEvent(r.t0 + 7s);
    EXPECT_EQ(r.hw->writeCalls, 1);                             // nothing re-sent
    r.link->onPollTick(r.t0 + 8s);
    EXPECT_EQ(r.link->sync().phase(), SyncPhase::Synced);
    EXPECT_DOUBLE_EQ(r.mixer.level.db, -30);                    // desktop shows the real (unchanged) level
    EXPECT_EQ(r.hw->writeCalls, 1);
}

TEST(DesktopLink, MuteAndUnmuteFollowTheDesktop) {
    Rig r;
    ASSERT_TRUE(r.link->start(r.t0));
    r.mixer.level = {-30.0, true};
    r.link->onDesktopEvent(r.t0 + 6s);
    EXPECT_TRUE(r.device().muted);
    r.mixer.level = {-30.0, false};
    r.link->onDesktopEvent(r.t0 + 7s);
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
}
