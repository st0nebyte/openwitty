// Session + mock-transport behaviour. All simulated; no hardware.
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

#include "core/DeviceSession.hpp"
#include "transport/MockTransport.hpp"
#include "transport/UsbTransport.hpp"

using namespace lewitt;

namespace {
struct Fixture {
    MockTransport* mock;
    std::unique_ptr<DeviceSession> session;
    Fixture() {
        auto m = std::make_unique<MockTransport>();
        mock = m.get();
        session = std::make_unique<DeviceSession>(std::move(m));
    }
};
}  // namespace

TEST(Session, ConnectSyncsSettings) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    EXPECT_EQ(f.session->state(), ConnectionState::Connected);
    ASSERT_TRUE(f.session->settings().has_value());
    EXPECT_DOUBLE_EQ(f.session->settings()->gainDb(0), 25.0);
}

TEST(Session, SettingsTimingSeparatesDelayedTransportFromDecodeAndNeverWrites) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    f.mock->setReadHook([] { std::this_thread::sleep_for(std::chrono::milliseconds(8)); });
    SettingsRefreshTiming timing;
    ASSERT_TRUE(f.session->refreshSettings(&timing).ok());
    EXPECT_LE(timing.started, timing.transportDone);
    EXPECT_LE(timing.transportDone, timing.decoded);
    EXPECT_LE(timing.decoded, timing.merged);
    EXPECT_GE(timing.transportDone - timing.started, std::chrono::milliseconds(8));
    EXPECT_TRUE(f.mock->writeLog().empty());
}

TEST(Session, StatusTimingSeparatesDelayedTransportFromDecodeAndNeverWrites) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    f.mock->setReadHook([] { std::this_thread::sleep_for(std::chrono::milliseconds(8)); });
    StatusRefreshTiming timing;
    ASSERT_TRUE(f.session->refreshStatus(&timing).ok());
    EXPECT_LE(timing.started, timing.transportDone);
    EXPECT_LE(timing.transportDone, timing.decoded);
    EXPECT_GE(timing.transportDone - timing.started, std::chrono::milliseconds(8));
    EXPECT_TRUE(f.mock->writeLog().empty());
}

TEST(Session, FailedSettingsReadLeavesDecodeTimingUnsetAndNoWrite) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    f.mock->injectFault(MockTransport::Fault::Timeout);
    SettingsRefreshTiming timing;
    ASSERT_FALSE(f.session->refreshSettings(&timing).ok());
    EXPECT_LE(timing.started, timing.transportDone);
    EXPECT_EQ(timing.decoded, std::chrono::steady_clock::time_point{});
    EXPECT_TRUE(f.mock->writeLog().empty());
}

TEST(Session, ReconnectDoesNotReuseOldStatusAsReadinessEvidence) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    ASSERT_TRUE(f.session->status().has_value());
    int readNumber = 0;
    f.mock->setReadHook([&] {
        if (++readNumber == 3) f.mock->injectFault(MockTransport::Fault::Timeout);
    });
    // API and settings respond; initial status GET fails. Control connection is available,
    // but the pre-reconnect status frame must not be presented as current.
    ASSERT_TRUE(f.session->reconnect().ok());
    EXPECT_EQ(f.session->state(), ConnectionState::Connected);
    EXPECT_FALSE(f.session->status().has_value());
    ASSERT_TRUE(f.session->refreshStatus().ok());
    EXPECT_TRUE(f.session->status().has_value());
    EXPECT_TRUE(f.mock->writeLog().empty());
}

TEST(Session, PersistentSettingsFailuresAreNotHiddenBySuccessfulMeterReads) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    for (int i = 1; i <= DeviceSession::kMaxConsecutiveFailures; ++i) {
        f.mock->injectFault(MockTransport::Fault::Timeout);
        ASSERT_FALSE(f.session->refreshSettings().ok());
        if (i < DeviceSession::kMaxConsecutiveFailures) {
            ASSERT_TRUE(f.session->refreshStatus().ok());
            EXPECT_EQ(f.session->state(), ConnectionState::Connected);
        }
    }
    EXPECT_EQ(f.session->state(), ConnectionState::Error);
    EXPECT_FALSE(f.mock->isOpen());
    EXPECT_TRUE(f.mock->writeLog().empty());
}

TEST(Session, EditCommitRoundTrip) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    ASSERT_TRUE(f.session->edit([](SettingsMessage& s) { return s.setGainDb(0, 40.0); }).ok());
    EXPECT_TRUE(f.session->dirty());
    ASSERT_TRUE(f.session->commit().ok());
    EXPECT_FALSE(f.session->dirty());
    EXPECT_DOUBLE_EQ(f.session->settings()->gainDb(0), 40.0);
    EXPECT_EQ(f.mock->writeLog().size(), 1u);
}

TEST(Session, FailedValidationSendsNothing) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    EXPECT_FALSE(f.session->edit([](SettingsMessage& s) { return s.setGainDb(0, 200.0); }).ok());
    EXPECT_FALSE(f.session->dirty());
    EXPECT_TRUE(f.mock->writeLog().empty());
}

TEST(Session, TimeoutOnConnect) {
    Fixture f;
    f.mock->injectFault(MockTransport::Fault::Timeout);
    auto s = f.session->connect();
    ASSERT_FALSE(s.ok());
    EXPECT_EQ(s.error().code, Errc::Timeout);
    EXPECT_NE(f.session->state(), ConnectionState::Connected);
}

TEST(Session, ShortReadRejected) {
    Fixture f;
    f.mock->injectFault(MockTransport::Fault::ShortRead);
    auto s = f.session->connect();
    ASSERT_FALSE(s.ok());
    EXPECT_EQ(s.error().code, Errc::ShortTransfer);
}

TEST(Session, DisconnectDuringOperation) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    f.mock->injectFault(MockTransport::Fault::Disconnect);
    auto s = f.session->refreshStatus();
    ASSERT_FALSE(s.ok());
    EXPECT_EQ(f.session->state(), ConnectionState::Disconnected);
    // reconnect after replug
    f.mock->setPresent(true);
    ASSERT_TRUE(f.session->connect().ok());
    EXPECT_EQ(f.session->state(), ConnectionState::Connected);
}

TEST(Session, ConnectFailsWhenAbsent) {
    Fixture f;
    f.mock->setPresent(false);
    auto s = f.session->connect();
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(f.session->state(), ConnectionState::Disconnected);
}

TEST(Session, TriggerValidation) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    EXPECT_TRUE(f.session->sendTrigger(Trigger::AutoGainMicrophone).ok());
    EXPECT_EQ(f.session->sendTrigger(Trigger::FactoryReset).error().code, Errc::NotSupported);
}

TEST(Mock, WriteOnlyAndSizeChecks) {
    MockTransport m;
    ASSERT_TRUE(m.open().ok());
    EXPECT_EQ(m.read(MessageId::Triggers).error().code, Errc::NotSupported);
    Bytes small(4, 0);
    EXPECT_EQ(m.write(MessageId::Settings, small).error().code, Errc::InvalidArgument);
    Bytes v(0x40, 0);
    EXPECT_EQ(m.write(MessageId::Version, v).error().code, Errc::NotSupported);
}

// Panic Mute (MUTE button hold), confirmed on real hardware 2026-10-16
// (docs/dev/HARDWARE_TEST_RESULTS.md): sets exactly the four primary mute bytes (2, 12, 22, 29)
// simultaneously; loopback mute (19) and the channel-select byte (56) are confirmed untouched;
// there is no separate dedicated flag. MockTransport::simulatePanicMute() mirrors this exactly.
TEST(Session, PanicMuteSetsExactlyTheFourPrimaryMuteFields) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    // Start from a known, fully-unmuted state so the transition is unambiguous (mirrors the
    // "clean" hardware trial, not the first, confounded one).
    ASSERT_TRUE(f.session->edit([](SettingsMessage& s) -> Status {
                     if (auto r = s.setMute(0, false); !r) return r;
                     if (auto r = s.setMute(1, false); !r) return r;
                     if (auto r = s.setSpeakerMute(false); !r) return r;
                     if (auto r = s.setHeadphoneMute(false); !r) return r;
                     return s.setLoopbackMute(false);
                 })
                    .ok());
    ASSERT_TRUE(f.session->commit().ok());
    ASSERT_FALSE(f.session->settings()->mute(0));
    ASSERT_FALSE(f.session->settings()->headphoneMute());

    f.mock->simulatePanicMute();
    ASSERT_TRUE(f.session->refreshSettings().ok());

    EXPECT_TRUE(f.session->settings()->mute(0));
    EXPECT_TRUE(f.session->settings()->mute(1));
    EXPECT_TRUE(f.session->settings()->speakerMute());
    EXPECT_TRUE(f.session->settings()->headphoneMute());
    EXPECT_FALSE(f.session->settings()->loopbackMute())
        << "loopback mute confirmed NOT affected by Panic Mute on real hardware";
}

// The existing device-wins merge must handle Panic Mute (4 fields changing at once, not just 1)
// exactly like any other external change: fields Panic Mute touched are adopted from the device;
// an unrelated pending user edit (gain) survives untouched.
TEST(Session, PanicMutePreservesUnrelatedPendingEditViaExistingMerge) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    ASSERT_TRUE(f.session->edit([](SettingsMessage& s) { return s.setMute(0, false); }).ok());
    ASSERT_TRUE(f.session->commit().ok());
    ASSERT_TRUE(f.session->edit([](SettingsMessage& s) { return s.setGainDb(0, 55.0); }).ok());
    ASSERT_TRUE(f.session->dirty());   // mic gain edit staged, not yet sent

    f.mock->simulatePanicMute();   // "hardware button" changes mute fields while our edit is pending
    ASSERT_TRUE(f.session->refreshSettings().ok());

    ASSERT_TRUE(f.session->dirty());   // our gain edit must survive the merge
    EXPECT_DOUBLE_EQ(f.session->effectiveSettings()->gainDb(0), 55.0);
    EXPECT_TRUE(f.session->effectiveSettings()->mute(0));   // device's Panic Mute change adopted
}

// Real-hardware write policy: UsbTransport refuses writes by default and does
// not need a device to do so.
TEST(UsbTransport, WritesRefusedByDefault) {
    UsbTransport t;
    Bytes b(0x80, 0);
    auto s = t.write(MessageId::Settings, b);
    ASSERT_FALSE(s.ok());
    EXPECT_EQ(s.error().code, Errc::WritesDisabled);
}

// --- error taxonomy: permission vs. claimed interface vs. stall ---------
#ifdef LEWITT_HAVE_USB
#include <libusb.h>
#endif
#ifdef LEWITT_HAVE_USB
TEST(UsbErrors, PermissionBusyStallAreDistinct) {
    auto acc = mapLibusbError(LIBUSB_ERROR_ACCESS, "open");
    auto busy = mapLibusbError(LIBUSB_ERROR_BUSY, "ctrl");
    auto pipe = mapLibusbError(LIBUSB_ERROR_PIPE, "ctrl");
    auto nodev = mapLibusbError(LIBUSB_ERROR_NO_DEVICE, "ctrl");
    auto tmo = mapLibusbError(LIBUSB_ERROR_TIMEOUT, "ctrl");
    EXPECT_EQ(acc.code, Errc::AccessDenied);
    EXPECT_EQ(busy.code, Errc::Busy);
    EXPECT_EQ(pipe.code, Errc::Stall);
    EXPECT_EQ(nodev.code, Errc::Disconnected);
    EXPECT_EQ(tmo.code, Errc::Timeout);
    EXPECT_NE(acc.message.find("udev"), std::string::npos);
    EXPECT_NE(busy.message.find("ownership"), std::string::npos);
}
#endif

TEST(Session, StallKeepsConnection) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    f.mock->injectFault(MockTransport::Fault::Stall);
    EXPECT_EQ(f.session->refreshStatus().error().code, Errc::Stall);
    EXPECT_EQ(f.session->state(), ConnectionState::Connected);  // a stall is not a disconnect
    EXPECT_TRUE(f.session->refreshStatus().ok());
}

TEST(Session, FailedCommitKeepsPendingAndSettings) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    ASSERT_TRUE(f.session->edit([](SettingsMessage& s) { return s.setMute(0, true); }).ok());
    f.mock->injectFault(MockTransport::Fault::Timeout);
    EXPECT_EQ(f.session->commit().error().code, Errc::Timeout);
    EXPECT_FALSE(f.session->settings()->mute(0));  // device state unchanged
    EXPECT_TRUE(f.session->dirty());               // edit not lost
    EXPECT_TRUE(f.session->commit().ok());         // retry succeeds
    EXPECT_TRUE(f.session->settings()->mute(0));
}

TEST(Session, WritesDisabledForRealPolicy) {
    // A non-simulated transport + default policy must refuse commit and triggers.
    struct Fake final : IDeviceTransport {
        std::string name() const override { return "fake-hw"; }
        Status open() override { open_ = true; return okStatus(); }
        void close() override { open_ = false; }
        bool isOpen() const override { return open_; }
        DeviceInfo info() const override { return {}; }
        Result<Bytes> read(MessageId id) override {
            Bytes b(messageInfo(id)->size, 0);
            if (id == MessageId::ApiVersion) b[0] = 1;   // device speaks API v1
            return b;
        }
        Status write(MessageId, std::span<const std::uint8_t>) override { ++writes; return okStatus(); }
        bool open_{false}; int writes{0};
    };
    auto t = std::make_unique<Fake>();
    Fake* raw = t.get();
    DeviceSession s(std::move(t));
    ASSERT_TRUE(s.connect().ok());
    EXPECT_FALSE(s.writesEnabled());
    ASSERT_TRUE(s.edit([](SettingsMessage& m) { return m.setDim(true); }).ok());
    EXPECT_EQ(s.commit().error().code, Errc::WritesDisabled);
    EXPECT_EQ(s.sendTrigger(Trigger::AutoGainBoth).error().code, Errc::WritesDisabled);
    EXPECT_EQ(raw->writes, 0);
}

TEST(Session, DisconnectClearsState) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    f.session->disconnect();
    EXPECT_FALSE(f.session->settings().has_value());
    EXPECT_FALSE(f.session->status().has_value());
    EXPECT_EQ(f.session->state(), ConnectionState::Disconnected);
}

TEST(UsbErrors, NumericCodesMatchLibusbAbi) {
    // enum libusb_error values are ABI-stable; this guards the libusb-free mapping.
    EXPECT_EQ(mapLibusbError(-3, "x").code, Errc::AccessDenied);
    EXPECT_EQ(mapLibusbError(-6, "x").code, Errc::Busy);
    EXPECT_EQ(mapLibusbError(-9, "x").code, Errc::Stall);
    EXPECT_EQ(mapLibusbError(-7, "x").code, Errc::Timeout);
    EXPECT_EQ(mapLibusbError(-4, "x").code, Errc::Disconnected);
    EXPECT_EQ(mapLibusbError(-5, "x").code, Errc::NotFound);
    EXPECT_EQ(mapLibusbError(-10, "x").code, Errc::Cancelled);
    EXPECT_EQ(mapLibusbError(-99, "x").code, Errc::Io);
}
