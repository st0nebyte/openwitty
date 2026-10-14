// Session + mock-transport behaviour. All simulated; no hardware.
#include <gtest/gtest.h>

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
    EXPECT_TRUE(f.session->sendTrigger(Trigger::AutoGainInput1).ok());
    EXPECT_EQ(f.session->sendTrigger(Trigger::Unknown0x103).error().code, Errc::NotSupported);
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
#include <libusb.h>
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
            return Bytes(messageInfo(id)->size, 0);
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
