// Single device owner: the service's socket server and the client transport (2026-11-01).
#include <gtest/gtest.h>

#include "core/RetryPolicy.hpp"

#include <filesystem>
#include <sys/stat.h>

#include "FakeHardware.hpp"
#include "core/OutputControl.hpp"
#include "transport/DeviceService.hpp"
#include "transport/MockTransport.hpp"
#include "transport/SerializedTransport.hpp"

using namespace lewitt;
namespace fs = std::filesystem;

namespace {
struct TempDir {
    std::string path;
    TempDir() {
        char t[] = "/tmp/owsvcXXXXXX";
        path = mkdtemp(t);
    }
    ~TempDir() { fs::remove_all(path); }
    std::string sock() const { return path + "/run/device.sock"; }
};

struct Service {
    TempDir dir;
    MockTransport* mock{nullptr};
    std::shared_ptr<IDeviceTransport> shared;
    WritePolicy policy;
    DeviceServer server{[this] { return policy; }, {}, 200};
    // real = the device presents itself as real hardware (byte checks apply)
    explicit Service(bool real = false) {
        auto m = std::make_unique<MockTransport>();
        mock = m.get();
        if (real) {
            auto fh = std::make_unique<lewitt::testing::FakeHardware>(mock);
            ownedMock_ = std::move(m);
            shared = std::make_shared<SerializedTransport>(std::move(fh));
        } else {
            shared = std::make_shared<SerializedTransport>(std::move(m));
        }
        EXPECT_TRUE(shared->open().ok());
        EXPECT_TRUE(server.start(dir.sock()).ok());
        server.setTransport(shared);
    }
    std::unique_ptr<MockTransport> ownedMock_;
};

Bytes settingsWith(MockTransport& m, std::size_t off, std::uint8_t v) {
    Bytes b = m.settingsBytes();
    b[off] = v;
    return b;
}
}  // namespace

TEST(DeviceService, AReaderReadsTheDeviceThroughTheService) {
    Service s;
    ServiceTransport c(s.dir.sock(), ServiceRole::Reader);
    ASSERT_TRUE(c.open().ok());
    EXPECT_EQ(c.info().backend, "service");
    auto r = c.read(MessageId::Settings);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value(), s.mock->settingsBytes());
    EXPECT_FALSE(s.server.controllerConnected()) << "a reader does not pause the service";
    EXPECT_FALSE(c.write(MessageId::Settings, s.mock->settingsBytes()).ok()) << "readers never write";
}

TEST(DeviceService, OnlyOneControllerAtATimeAndTheServiceKnowsIt) {
    Service s;
    ServiceTransport a(s.dir.sock(), ServiceRole::Controller), b(s.dir.sock(), ServiceRole::Controller);
    ASSERT_TRUE(a.open().ok());
    for (int i = 0; i < 50 && !s.server.controllerConnected(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(s.server.controllerConnected());
    auto st = b.open();
    ASSERT_FALSE(st.ok());
    EXPECT_EQ(st.error().code, Errc::ControlInUse);
    ServiceTransport reader(s.dir.sock(), ServiceRole::Reader);
    EXPECT_TRUE(reader.open().ok()) << "readers still work while a window controls the device";
    a.close();
    for (int i = 0; i < 100 && s.server.controllerConnected(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(s.server.controllerConnected());
    EXPECT_TRUE(b.open().ok());
}

TEST(DeviceService, OnRealHardwareTheServiceAllowsOnlyPermittedBytesAndTriggers) {
    Service s(/*real=*/true);
    s.policy = groupsWritePolicy({"ledBrightness"});
    ServiceTransport c(s.dir.sock(), ServiceRole::Controller);
    ASSERT_TRUE(c.open().ok());
    const auto before = s.mock->settingsBytes();
    EXPECT_TRUE(c.write(MessageId::Settings, settingsWith(*s.mock, settings_off::kLedBrightness, 42)).ok());
    EXPECT_EQ(s.mock->settingsBytes()[settings_off::kLedBrightness], 42);
    auto bad = c.write(MessageId::Settings,
                       settingsWith(*s.mock, settings_off::kMute[0], before[settings_off::kMute[0]] ^ 1));
    ASSERT_FALSE(bad.ok());
    EXPECT_EQ(bad.error().code, Errc::WritesDisabled);
    EXPECT_EQ(s.mock->settingsBytes()[settings_off::kMute[0]], before[settings_off::kMute[0]]) << "nothing sent";
    Bytes trig(0x40, 0);
    trig[0] = static_cast<std::uint8_t>(Trigger::AutoGainMicrophone);
    EXPECT_FALSE(c.write(MessageId::Triggers, trig).ok()) << "measurements not in the policy";
    s.policy = groupsWritePolicy({"ledBrightness", "measurements"});
    EXPECT_TRUE(c.write(MessageId::Triggers, trig).ok());
    s.policy = WritePolicy{};
    EXPECT_FALSE(c.write(MessageId::Settings, settingsWith(*s.mock, settings_off::kLedBrightness, 43)).ok())
        << "the policy is read at every write";
}

TEST(DeviceService, WithoutTheServiceTheClientOpensTheDeviceDirectly) {
    TempDir d;
    MockTransport* direct = nullptr;
    ServiceOrDirectTransport t(d.sock(), ServiceRole::Controller, [&] {
        auto m = std::make_unique<MockTransport>();
        direct = m.get();
        return std::unique_ptr<IDeviceTransport>(std::move(m));
    });
    ASSERT_TRUE(t.open().ok());
    EXPECT_FALSE(t.viaService());
    ASSERT_NE(direct, nullptr);
    EXPECT_TRUE(t.read(MessageId::Settings).ok());
}

TEST(DeviceService, WithTheServiceTheClientNeverOpensUsb) {
    Service s;
    bool directMade = false;
    ServiceOrDirectTransport t(s.dir.sock(), ServiceRole::Reader, [&] {
        directMade = true;
        return std::unique_ptr<IDeviceTransport>(std::make_unique<MockTransport>());
    });
    ASSERT_TRUE(t.open().ok());
    EXPECT_TRUE(t.viaService());
    EXPECT_FALSE(directMade);
    s.server.setTransport(nullptr);   // the service has no device: an error, never a direct USB open
    auto st = t.open();
    ASSERT_FALSE(st.ok());
    EXPECT_EQ(st.error().code, Errc::Disconnected);
    EXPECT_TRUE(shouldRetryOpen(st.error().code, /*deviceListChanged=*/false));   // the window keeps trying (login)
    EXPECT_FALSE(directMade);
}

TEST(DeviceService, SocketIsPrivateAndASecondServiceIsRefused) {
    Service s;
    struct stat st{};
    ASSERT_EQ(stat(s.dir.sock().c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600u);
    ASSERT_EQ(stat((s.dir.path + "/run").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0700u);
    DeviceServer second([] { return WritePolicy{}; });
    auto r = second.start(s.dir.sock());
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::ControlInUse);
    s.server.stop();
    EXPECT_FALSE(fs::exists(s.dir.sock()));
    DeviceServer third([] { return WritePolicy{}; });
    EXPECT_TRUE(third.start(s.dir.sock()).ok()) << "after the first service stopped";
}

TEST(DeviceService, AClientNoticesTheLostDeviceAndTheServiceIsTold) {
    Service s;
    ServiceTransport c(s.dir.sock(), ServiceRole::Reader);
    ASSERT_TRUE(c.open().ok());
    s.mock->injectFault(MockTransport::Fault::Disconnect);
    auto r = c.read(MessageId::Status);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::Disconnected);
    EXPECT_TRUE(s.server.takeDeviceLost());
    EXPECT_FALSE(s.server.takeDeviceLost()) << "reading clears it";
}
