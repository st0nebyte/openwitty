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
