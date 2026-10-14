// Phase 5C: backend hardening. All simulated (mock transport + fault injection);
// nothing here has been verified against real hardware.
#include <gtest/gtest.h>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>

#include "core/DeviceManager.hpp"
#include "core/DeviceSession.hpp"
#include "protocol/WriteGuard.hpp"
#include "transport/MockTransport.hpp"
#include "transport/SerializedTransport.hpp"
#include "transport/UsbDiagnostics.hpp"
#include "transport/UsbTransport.hpp"

using namespace lewitt;
using namespace std::chrono_literals;

namespace {
struct Rig {
    MockTransport* mock;
    std::unique_ptr<DeviceSession> s;
    explicit Rig(WritePolicy p = {}) {
        auto m = std::make_unique<MockTransport>();
        mock = m.get();
        s = std::make_unique<DeviceSession>(std::move(m), p);
    }
};
}  // namespace

// ---- error taxonomy -----------------------------------------------------
TEST(Taxonomy, EveryCodeHasAStableName) {
    std::set<std::string> names;
    for (int i = 0; i <= static_cast<int>(Errc::Internal); ++i) names.insert(errcName(static_cast<Errc>(i)));
    EXPECT_EQ(names.size(), static_cast<std::size_t>(Errc::Internal) + 1);
    EXPECT_EQ(std::string(errcName(Errc::AccessDenied)), "access-denied");
}

TEST(Taxonomy, SuggestionsDistinguishPermissionOwnershipAndStall) {
    auto acc = suggestionFor({Errc::AccessDenied, ""});
    auto busy = suggestionFor({Errc::Busy, ""});
    auto stall = suggestionFor({Errc::Stall, ""});
    EXPECT_NE(acc.find("permission"), std::string::npos);
    EXPECT_NE(busy.find("kernel driver"), std::string::npos);
    EXPECT_NE(stall.find("still connected"), std::string::npos);
    EXPECT_NE(acc, busy);
    EXPECT_NE(busy, stall);
}

// ---- stalls are not disconnects; failures escalate only for dead links ---
TEST(Lifecycle, StallsNeverEscalateToDisconnect) {
    Rig r;
    ASSERT_TRUE(r.s->connect().ok());
    for (int i = 0; i < 10; ++i) {
        r.mock->injectFault(MockTransport::Fault::Stall);
        EXPECT_EQ(r.s->refreshStatus().error().code, Errc::Stall);
    }
    EXPECT_EQ(r.s->state(), ConnectionState::Connected);
    EXPECT_EQ(r.s->consecutiveFailures(), 0);
}

TEST(Lifecycle, BusyAndAccessDeniedDoNotCountAsDeadLink) {
    Rig r;
    ASSERT_TRUE(r.s->connect().ok());
    r.mock->injectFault(MockTransport::Fault::Busy, 5);
    for (int i = 0; i < 5; ++i) EXPECT_EQ(r.s->refreshStatus().error().code, Errc::Busy);
    r.mock->injectFault(MockTransport::Fault::AccessDenied, 5);
    for (int i = 0; i < 5; ++i) EXPECT_EQ(r.s->refreshStatus().error().code, Errc::AccessDenied);
    EXPECT_EQ(r.s->state(), ConnectionState::Connected);
}

TEST(Lifecycle, RepeatedTimeoutsMarkLinkDeadAndReconnectRecovers) {
    Rig r;
    ASSERT_TRUE(r.s->connect().ok());
    r.mock->injectFault(MockTransport::Fault::Timeout, 3);
    for (int i = 0; i < DeviceSession::kMaxConsecutiveFailures; ++i) (void)r.s->refreshStatus();
    EXPECT_EQ(r.s->state(), ConnectionState::Error);
    EXPECT_TRUE(r.s->needsReconnect());
    ASSERT_TRUE(r.s->reconnect().ok());
    EXPECT_EQ(r.s->state(), ConnectionState::Connected);
    EXPECT_EQ(r.s->consecutiveFailures(), 0);
}

TEST(Lifecycle, SuccessResetsFailureCounter) {
    Rig r;
    ASSERT_TRUE(r.s->connect().ok());
    r.mock->injectFault(MockTransport::Fault::Timeout, 2);
    (void)r.s->refreshStatus(); (void)r.s->refreshStatus();
    EXPECT_EQ(r.s->consecutiveFailures(), 2);
    ASSERT_TRUE(r.s->refreshStatus().ok());
    EXPECT_EQ(r.s->consecutiveFailures(), 0);
    EXPECT_EQ(r.s->state(), ConnectionState::Connected);
}

TEST(Lifecycle, OversizedAndShortResponsesAreRejected) {
    Rig r;
    ASSERT_TRUE(r.s->connect().ok());
    r.mock->injectFault(MockTransport::Fault::LongRead);
    EXPECT_EQ(r.s->refreshStatus().error().code, Errc::Malformed);
    r.mock->injectFault(MockTransport::Fault::ShortRead);
    EXPECT_EQ(r.s->refreshSettings().error().code, Errc::ShortTransfer);
    ASSERT_TRUE(r.s->settings().has_value());   // last good state kept
}

TEST(Lifecycle, ReconnectPreservesPendingEditsAndAdoptsDeviceChanges) {
    Rig r;
    ASSERT_TRUE(r.s->connect().ok());
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return m.setGainDb(0, 44); }).ok());
    auto ext = r.mock->settingsBytes();
    ext[29] = 1;                                   // headphone muted on the device meanwhile
    r.mock->setSettingsBytes(ext);
    ASSERT_TRUE(r.s->reconnect().ok());
    EXPECT_TRUE(r.s->settings()->headphoneMute());
    ASSERT_TRUE(r.s->dirty());
    EXPECT_DOUBLE_EQ(r.s->effectiveSettings()->gainDb(0), 44.0);
}

TEST(Lifecycle, UnsupportedProductIdIsRefusedOnRealTransportsOnly) {
    // product id 0x000a is not an APP mode the original accepts; simulated mock may use anything
    struct Fake final : IDeviceTransport {
        std::string name() const override { return "fake"; }
        Status open() override { open_ = true; return okStatus(); }
        void close() override { open_ = false; }
        bool isOpen() const override { return open_; }
        DeviceInfo info() const override { return DeviceInfo{kLewittVendorId, 0x000A, "", "", "usb"}; }
        Result<Bytes> read(MessageId id) override {
            Bytes b(messageInfo(id)->size, 0);
            if (id == MessageId::ApiVersion) b[0] = 1;
            return b;
        }
        Status write(MessageId, std::span<const std::uint8_t>) override { return okStatus(); }
        bool open_{false};
    };
    DeviceSession s(std::make_unique<Fake>());
    auto r = s.connect();
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::NotSupported);
    EXPECT_NE(r.error().message.find("0x000a"), std::string::npos);
    EXPECT_EQ(s.state(), ConnectionState::Error);
}

TEST(Lifecycle, CapabilitiesReflectHandshake) {
    Rig r;
    ASSERT_TRUE(r.s->connect().ok());
    EXPECT_EQ(r.s->capabilities().apiMajor, 1);
    EXPECT_EQ(r.s->capabilities().mode, DeviceMode::Normal);
    EXPECT_TRUE(r.s->capabilities().simulated);
    r.mock->setPid(0x0002);
    ASSERT_TRUE(r.s->reconnect().ok());
    EXPECT_EQ(r.s->capabilities().mode, DeviceMode::MacOSStreaming);
}

// ---- write safety enforced in the backend --------------------------------
TEST(WriteGuard, DestructiveWordsNeverPassOnHardware) {
    WriteGuardContext hw{false, true};
    for (auto t : {Trigger::FactoryReset, Trigger::SwitchToNormalMode, Trigger::SwitchToMacOSStreamingMode}) {
        auto b = TriggerMessage(t).bytes();
        EXPECT_EQ(checkWriteAllowed(MessageId::Triggers, b, hw).error().code, Errc::NotSupported)
            << static_cast<int>(t);
    }
    auto led = TriggerMessage(Trigger::ResetLedColors).bytes();
    EXPECT_EQ(checkWriteAllowed(MessageId::Triggers, led, WriteGuardContext{false, false}).error().code,
              Errc::WritesDisabled);
    EXPECT_TRUE(checkWriteAllowed(MessageId::Triggers, led, WriteGuardContext{false, true}).ok());
    // factory reset is refused even on a simulated transport
    auto fr = TriggerMessage(Trigger::FactoryReset).bytes();
    EXPECT_FALSE(checkWriteAllowed(MessageId::Triggers, fr, WriteGuardContext{true, true}).ok());
}

TEST(WriteGuard, UnknownTriggerWordsAndPayloadAreRefused) {
    Bytes b(0x40, 0);
    b[0] = 0x77;
    EXPECT_EQ(checkWriteAllowed(MessageId::Triggers, b, {true, true}).error().code, Errc::NotSupported);
    b[0] = 1; b[10] = 5;
    EXPECT_EQ(checkWriteAllowed(MessageId::Triggers, b, {true, true}).error().code, Errc::InvalidArgument);
    EXPECT_EQ(checkWriteAllowed(MessageId::Status, Bytes(0x40, 0), {true, true}).error().code, Errc::NotSupported);
    EXPECT_EQ(checkWriteAllowed(MessageId::Settings, Bytes(0x10, 0), {true, true}).error().code,
              Errc::InvalidArgument);
}

TEST(WriteGuard, UsbTransportRefusesEvenWithWritesEnabled) {
    UsbOptions o;
    o.allowWrites = true;
    UsbTransport t(o);
    auto fr = TriggerMessage(Trigger::FactoryReset).bytes();
    auto r = t.write(MessageId::Triggers, fr);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::NotSupported);   // refused before any USB I/O
    auto mode = TriggerMessage(Trigger::SwitchToMacOSStreamingMode).bytes();
    EXPECT_EQ(t.write(MessageId::Triggers, mode).error().code, Errc::NotSupported);
}

TEST(WriteGuard, UnknownSettingsBytesCannotChange) {
    SettingsMessage before;
    auto raw = Bytes(before.bytes().begin(), before.bytes().end());
    raw[60] = 0x7F;      // unknown schema byte
    raw[100] = 0x11;     // reserved byte beyond the schema
    auto dev = SettingsMessage::fromBytes(raw).value();
    SettingsMessage ok = dev;
    ASSERT_TRUE(ok.setGainDb(0, 10).ok());
    EXPECT_TRUE(verifyOnlyKnownFieldsChanged(dev, ok).ok());
    auto r2 = Bytes(ok.bytes().begin(), ok.bytes().end());
    r2[60] = 0;          // something tried to change an unknown byte
    auto bad = SettingsMessage::fromBytes(r2).value();
    EXPECT_EQ(verifyOnlyKnownFieldsChanged(dev, bad).error().code, Errc::InvalidArgument);
}

TEST(WriteGuard, SessionNeverWritesUnknownBytesAndPreservesThem) {
    Rig r;
    auto raw = r.mock->settingsBytes();
    raw[60] = 0x5A; raw[69] = 0x33; raw[120] = 0xC3;
    r.mock->setSettingsBytes(raw);
    ASSERT_TRUE(r.s->connect().ok());
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return m.setHeadphoneVolumeDb(-8); }).ok());
    ASSERT_TRUE(r.s->commit().ok());
    auto after = r.mock->settingsBytes();
    EXPECT_EQ(after[60], 0x5A);
    EXPECT_EQ(after[69], 0x33);
    EXPECT_EQ(after[120], 0xC3);
}

TEST(WriteGuard, UnsupportedEnumValuesAreNotRewritten) {
    Bytes raw(0x80, 0);
    raw[6] = 7;       // preamp: unsupported value
    raw[23] = 5;      // mix-to-speakers: only 0/1 are produced by the UI
    raw[34] = 9;      // monitor source: unsupported
    raw[53] = 0x07;   // custom button: not offered by the UI
    raw[22] = 2;      // a mute byte holding 2
    auto s = SettingsMessage::fromBytes(raw).value();
    ASSERT_TRUE(s.setMixToSpeakers(true).ok());      // already "on": untouched
    ASSERT_TRUE(s.setSpeakerMute(true).ok());        // already "muted": untouched
    ASSERT_TRUE(s.setGainDb(0, 12).ok());
    EXPECT_EQ(s.bytes()[6], 7);
    EXPECT_EQ(s.bytes()[23], 5);
    EXPECT_EQ(s.bytes()[22], 2);
    EXPECT_EQ(s.bytes()[34], 9);
    EXPECT_EQ(s.bytes()[53], 7);
    EXPECT_FALSE(s.setMonitorMicrophone(true).ok());  // refuses instead of guessing
    EXPECT_EQ(s.bytes()[34], 9);
    // an explicit change away from the odd value is still possible
    ASSERT_TRUE(s.setSpeakerMute(false).ok());
    EXPECT_EQ(s.bytes()[22], 0);
}

// ---- serialization, cancellation, shutdown -------------------------------
TEST(Serialized, ConcurrentRequestsAreSerializedAndAllSucceed) {
    auto inner = std::make_unique<MockTransport>();
    std::atomic<int> inFlight{0}, maxInFlight{0};
    inner->setReadHook([&] {
        int n = ++inFlight;
        int cur = maxInFlight.load();
        while (n > cur && !maxInFlight.compare_exchange_weak(cur, n)) {}
        std::this_thread::sleep_for(1ms);
        --inFlight;
    });
    SerializedTransport t(std::move(inner));
    ASSERT_TRUE(t.open().ok());
    std::vector<std::thread> th;
    std::atomic<int> okCount{0};
    for (int i = 0; i < 8; ++i)
        th.emplace_back([&] {
            for (int k = 0; k < 10; ++k)
                if (t.read(k % 2 ? MessageId::Status : MessageId::Settings)) ++okCount;
        });
    for (auto& x : th) x.join();
    EXPECT_EQ(okCount.load(), 80);
    EXPECT_EQ(maxInFlight.load(), 1);           // never two requests at once
    EXPECT_EQ(t.stats().reads, 80u);
}

TEST(Serialized, CancelAbortsQueuedAndFutureRequests) {
    auto inner = std::make_unique<MockTransport>();
    SerializedTransport t(std::move(inner));
    ASSERT_TRUE(t.open().ok());
    ASSERT_TRUE(t.read(MessageId::Status).ok());
    t.requestCancel();
    EXPECT_EQ(t.read(MessageId::Status).error().code, Errc::Cancelled);
    Bytes b(0x40, 0); b[0] = 1;
    EXPECT_EQ(t.write(MessageId::Triggers, b).error().code, Errc::Cancelled);
    t.clearCancel();
    EXPECT_TRUE(t.read(MessageId::Status).ok());
    EXPECT_EQ(t.stats().cancelled, 2u);
}

TEST(Serialized, ShutdownDuringInFlightRequestCompletesPromptly) {
    auto inner = std::make_unique<MockTransport>();
    std::mutex m; std::condition_variable cv;
    bool entered = false, release = false;
    inner->setReadHook([&] {
        std::unique_lock lk(m);
        entered = true; cv.notify_all();
        cv.wait_for(lk, 2s, [&] { return release; });    // a "hung" device, bounded by the timeout
    });
    SerializedTransport t(std::move(inner));
    ASSERT_TRUE(t.open().ok());
    std::atomic<bool> done{false};
    std::thread worker([&] { auto r = t.read(MessageId::Status); (void)r; done = true; });
    { std::unique_lock lk(m); ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return entered; })); }
    t.requestCancel();                                    // shutdown begins while a request is stuck
    { std::lock_guard lk(m); release = true; cv.notify_all(); }   // the device finally answers
    auto t0 = std::chrono::steady_clock::now();
    worker.join();
    t.close();                                            // must not deadlock
    EXPECT_TRUE(done.load());
    EXPECT_LT(std::chrono::steady_clock::now() - t0, 1s);
    EXPECT_FALSE(t.isOpen());
}

TEST(Session, CancelMakesActiveOperationsReturnCancelled) {
    Rig r;
    ASSERT_TRUE(r.s->connect().ok());
    r.s->cancel();
    EXPECT_EQ(r.s->refreshStatus().error().code, Errc::Cancelled);
    EXPECT_EQ(r.s->consecutiveFailures(), 0);            // cancellation is not a link failure
    r.s->clearCancel();
    EXPECT_TRUE(r.s->refreshStatus().ok());
}

// ---- discovery -----------------------------------------------------------
namespace {
UsbDeviceReport rep(std::uint16_t vid, std::uint16_t pid, int bus, int addr) {
    UsbDeviceReport r;
    r.vid = vid; r.pid = pid; r.bus = bus; r.address = addr;
    r.sysfsPath = "/sys/bus/usb/devices/" + std::to_string(bus) + "-" + std::to_string(addr);
    return r;
}
}  // namespace

TEST(Manager, ClassifiesModesAndDetectsHotplug) {
    std::vector<UsbDeviceReport> devs;
    DeviceManager m([&] { return devs; });
    EXPECT_TRUE(m.poll().empty());
    devs.push_back(rep(0x29C2, 0x0004, 1, 5));
    auto c = m.poll();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].kind, DeviceManager::Change::Kind::Added);
    EXPECT_TRUE(c[0].device.isConnect2);
    EXPECT_EQ(c[0].device.mode, DeviceMode::Normal);
    EXPECT_TRUE(m.poll().empty());                       // no change, no event
    devs[0] = rep(0x29C2, 0x0002, 1, 6);                 // re-enumerated in macOS streaming mode
    c = m.poll();
    ASSERT_EQ(c.size(), 2u);
    EXPECT_EQ(c[0].device.mode, DeviceMode::MacOSStreaming);
    devs.clear();
    c = m.poll();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].kind, DeviceManager::Change::Kind::Removed);
}

TEST(Manager, NonConnect2LewittDeviceIsNotAppMode) {
    auto c = DeviceManager::classify(rep(0x29C2, 0x000A, 1, 2));
    EXPECT_TRUE(c.isConnect2);          // known PID...
    EXPECT_FALSE(c.appMode);            // ...but not one of the two modes the original accepts
    auto o = DeviceManager::classify(rep(0x0D8C, 0x0004, 1, 3));
    EXPECT_FALSE(o.isConnect2);
}

// ---- descriptors from raw bytes (independent fixture) ---------------------
TEST(Descriptors, ParsesInterfacesEndpointsAndAdcVersion) {
    // hand-assembled: device descriptor (VID 29c2 PID 0004) + config + AC interface (UAC2 header)
    // + AS interface with one ISO endpoint.
    Bytes d = {18, 1, 0x00, 0x02, 0xEF, 0x02, 0x01, 64, 0xC2, 0x29, 0x04, 0x00, 0x21, 0x01, 1, 2, 3, 1};
    Bytes cfg = {9, 2, 0, 0, 2, 1, 0, 0x80, 50};
    Bytes ac = {9, 4, 0, 0, 0, 1, 1, 0x20, 0};
    Bytes acHdr = {9, 0x24, 0x01, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00};
    Bytes as = {9, 4, 1, 1, 1, 1, 2, 0x20, 0};
    Bytes ep = {7, 5, 0x01, 0x05, 0x00, 0x02, 1};
    Bytes raw;
    for (auto* p : {&d, &cfg, &ac, &acHdr, &as, &ep}) raw.insert(raw.end(), p->begin(), p->end());
    UsbDeviceReport r;
    ASSERT_TRUE(parseDescriptors(raw, r));
    EXPECT_EQ(r.vid, 0x29C2); EXPECT_EQ(r.pid, 0x0004); EXPECT_EQ(r.bcdDevice, 0x0121);
    ASSERT_EQ(r.interfaces.size(), 2u);
    EXPECT_EQ(r.interfaces[0].klass, 1); EXPECT_EQ(r.interfaces[0].subclass, 1);
    EXPECT_EQ(r.interfaces[0].bcdAdc, 0x0200);
    ASSERT_EQ(r.interfaces[1].endpoints.size(), 1u);
    EXPECT_EQ(r.interfaces[1].endpoints[0].address, 0x01);
    EXPECT_EQ(r.interfaces[1].endpoints[0].maxPacket, 0x0200);
}

TEST(Descriptors, RejectsGarbage) {
    UsbDeviceReport r;
    EXPECT_FALSE(parseDescriptors(Bytes{}, r));
    EXPECT_FALSE(parseDescriptors(Bytes(40, 0xFF), r));
    Bytes trunc = {18, 1, 0, 2};
    EXPECT_FALSE(parseDescriptors(trunc, r));
}

// ---- sysfs scan against a fixture tree (unprivileged discovery path) ------
#include <filesystem>
#include <fstream>
TEST(Sysfs, ScansFixtureTreeWithoutAnyUsbIo) {
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / ("lewitt_sysfs_" + std::to_string(::getpid()));
    fs::remove_all(root);
    fs::create_directories(root / "1-4" / "1-4:1.0");
    auto put = [](const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; };
    put(root / "1-4" / "idVendor", "29c2\n");
    put(root / "1-4" / "idProduct", "0004\n");
    put(root / "1-4" / "busnum", "99\n");
    put(root / "1-4" / "devnum", "250\n");
    put(root / "1-4" / "product", "CONNECT 2\n");
    put(root / "1-4" / "manufacturer", "LEWITT\n");
    // device descriptor + config + AC interface (UAC2 header)
    Bytes d = {18, 1, 0x00, 0x02, 0xEF, 0x02, 0x01, 64, 0xC2, 0x29, 0x04, 0x00, 0x21, 0x01, 1, 2, 3, 1,
               9, 2, 0, 0, 1, 1, 0, 0x80, 50, 9, 4, 0, 0, 0, 1, 1, 0x20, 0, 9, 0x24, 1, 0, 2, 8, 0, 0, 0};
    put(root / "1-4" / "descriptors", std::string(d.begin(), d.end()));
    fs::create_symlink("../../../../bus/usb/drivers/snd-usb-audio", root / "1-4" / "1-4:1.0" / "driver");
    // another vendor must be ignored
    fs::create_directories(root / "2-1");
    put(root / "2-1" / "idVendor", "05e3\n");
    auto devs = scanSysfs(0x29C2, root.string());
    ASSERT_EQ(devs.size(), 1u);
    EXPECT_EQ(devs[0].pid, 0x0004);
    EXPECT_EQ(devs[0].bus, 99);
    EXPECT_EQ(devs[0].address, 250);
    EXPECT_EQ(devs[0].product, "CONNECT 2");
    ASSERT_EQ(devs[0].interfaces.size(), 1u);
    EXPECT_EQ(devs[0].interfaces[0].boundDriver, "snd-usb-audio");
    EXPECT_EQ(devs[0].interfaces[0].bcdAdc, 0x0200);
    EXPECT_EQ(devs[0].usbfsNode, "/dev/bus/usb/099/250");
    EXPECT_FALSE(devs[0].nodeExists);          // fixture bus/dev does not exist on this machine
    fs::remove_all(root);
}
