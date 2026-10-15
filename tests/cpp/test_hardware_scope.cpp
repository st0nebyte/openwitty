// Phase 6E: scope of settings writes on REAL (non-simulated) hardware, and behaviour around
// non-atomic full-struct SETs. All simulated: a MockTransport wrapped so that it reports
// isSimulated() == false, which is how DeviceSession tells hardware from simulation. This is NOT
// hardware evidence. Deliberately independent of the physical Panic Mute simulation hooks.
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

#include "core/DeviceSession.hpp"
#include "transport/MockTransport.hpp"

using namespace lewitt;

namespace {

// Delegates everything to a MockTransport but claims to be real hardware.
class FakeHardware final : public IDeviceTransport {
public:
    explicit FakeHardware(MockTransport* inner) : m_(inner) {}
    std::string name() const override { return "fake-hardware"; }
    Status open() override { return m_->open(); }
    void close() override { m_->close(); }
    bool isOpen() const override { return m_->isOpen(); }
    DeviceInfo info() const override { return m_->info(); }
    Result<Bytes> read(MessageId id) override { return m_->read(id); }
    Status write(MessageId id, std::span<const std::uint8_t> d) override { return m_->write(id, d); }
    bool isSimulated() const override { return false; }
    void requestCancel() override { m_->requestCancel(); }
    void clearCancel() override { m_->clearCancel(); }
private:
    MockTransport* m_;   // owned by the test (outlives the session)
};

struct Rig {
    MockTransport mock;
    std::unique_ptr<DeviceSession> s;
    explicit Rig(std::vector<std::size_t> offsets, bool allow = true) {
        WritePolicy p;
        p.allowHardwareWrites = allow;
        p.hardwareWriteOffsets = std::move(offsets);
        s = std::make_unique<DeviceSession>(std::make_unique<FakeHardware>(&mock), p);
        EXPECT_TRUE(s->connect().ok());
    }
    int settingsWrites() const {
        auto l = mock.writeLog();
        return static_cast<int>(std::count(l.begin(), l.end(), MessageId::Settings));
    }
};

PollPolicy fast() {
    PollPolicy pp;
    pp.sleeper = [](std::chrono::milliseconds) {};
    return pp;
}

}  // namespace

TEST(HardwareScope, InstrumentMuteOffsetIsTheOnlyByteThatMayChange) {
    Rig r({12});
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return m.setMute(1, true); }).ok());
    auto res = r.s->commitAndVerify(fast());
    EXPECT_EQ(res.state, WriteState::Verified);
    EXPECT_EQ(r.settingsWrites(), 1);
    auto b = r.mock.settingsBytes();
    EXPECT_EQ(b[12], 1);
    EXPECT_EQ(b[2], 0);    // microphone mute untouched
    EXPECT_EQ(b[22], 0);   // speaker mute untouched
}

TEST(HardwareScope, OtherOffsetsAreRefusedOnHardwareEvenWithWritesAllowed) {
    Rig r({12});
    // byte 37 (LED brightness) is a perfectly valid settings field, but not authorized here
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return m.setLedBrightness(10); }).ok());
    auto res = r.s->commitAndVerify(fast());
    EXPECT_EQ(res.state, WriteState::Failed);
    EXPECT_EQ(res.error.code, Errc::WritesDisabled);
    EXPECT_EQ(r.settingsWrites(), 0);
}

TEST(HardwareScope, EmptyAuthorizationListRefusesEverythingOnHardware) {
    Rig r({});
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return m.setMute(1, true); }).ok());
    EXPECT_EQ(r.s->commitAndVerify(fast()).state, WriteState::Failed);
    EXPECT_FALSE(r.s->commit().ok());
    EXPECT_EQ(r.settingsWrites(), 0);
}

TEST(HardwareScope, TwoByteEditIsRefusedWhenOnlyOneIsAuthorized) {
    Rig r({12});
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) {
                     if (auto s = m.setMute(1, true); !s) return s;
                     return m.setMute(0, true);       // byte 2 is not authorized
                 }).ok());
    auto res = r.s->commitAndVerify(fast());
    EXPECT_EQ(res.state, WriteState::Failed);
    EXPECT_EQ(r.settingsWrites(), 0);
}

TEST(HardwareScope, SimulatedTransportsIgnoreTheOffsetList) {
    MockTransport* mock = new MockTransport;   // owned by the session; simulated => not scoped
    WritePolicy p;
    p.hardwareWriteOffsets = {12};
    DeviceSession s(std::unique_ptr<IDeviceTransport>(mock), p);
    ASSERT_TRUE(s.connect().ok());
    ASSERT_TRUE(s.edit([](SettingsMessage& m) { return m.setLedBrightness(10); }).ok());
    EXPECT_EQ(s.commitAndVerify(fast()).state, WriteState::Verified);
}

TEST(HardwareScope, HardwareWritesStayOffWithoutExplicitPolicy) {
    Rig r({12}, /*allow=*/false);
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return m.setMute(1, true); }).ok());
    auto res = r.s->commitAndVerify(fast());
    EXPECT_EQ(res.state, WriteState::Failed);
    EXPECT_EQ(res.error.code, Errc::WritesDisabled);
    EXPECT_EQ(r.settingsWrites(), 0);
}

// ---- non-atomic full-struct SET: external changes around the write ------------------------

TEST(HardwareScope, ExternalMuteBeforePreparationIsAdoptedNotOverwritten) {
    Rig r({12, 22});   // (22 authorized only so the adopted device value is not "our" change)
    // A physical action muted the speaker (byte 22) right before we prepare the write.
    r.mock.injectExternalSettingsChange(0, 22, 1);
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return m.setMute(1, true); }).ok());
    auto res = r.s->commitAndVerify(fast());
    EXPECT_EQ(res.state, WriteState::Verified);
    auto b = r.mock.settingsBytes();
    EXPECT_EQ(b[12], 1) << "intended edit applied";
    EXPECT_EQ(b[22], 1) << "device-side change adopted by the fresh snapshot, not reverted";
}

TEST(HardwareScope, StabilityCheckRefusesToSendWhenDeviceChangesWhilePreparing) {
    Rig r({12});
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return m.setMute(1, true); }).ok());
    // first back-to-back read is clean, the second one sees a change (e.g. a Panic Mute press)
    r.mock.injectExternalSettingsChange(1, 2, 1);
    PollPolicy pp = fast();
    pp.requireStableSnapshot = true;
    auto res = r.s->commitAndVerify(pp);
    EXPECT_EQ(res.state, WriteState::Failed);
    EXPECT_EQ(r.settingsWrites(), 0) << "nothing may be sent on an unstable snapshot";
    EXPECT_EQ(r.mock.settingsBytes()[12], 0);
}

TEST(HardwareScope, StabilityCheckPassesOnQuietDeviceAndStillWritesOnce) {
    Rig r({12});
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return m.setMute(1, true); }).ok());
    PollPolicy pp = fast();
    pp.requireStableSnapshot = true;
    EXPECT_EQ(r.s->commitAndVerify(pp).state, WriteState::Verified);
    EXPECT_EQ(r.settingsWrites(), 1);
}

TEST(HardwareScope, MultiChannelChangeAfterWriteIsReportedAmbiguousWithAllOffsets) {
    Rig r({12});
    ASSERT_TRUE(r.s->edit([](SettingsMessage& m) { return m.setMute(1, true); }).ok());
    // After the SET has been accepted, several channels get muted at once on the device
    // (stand-in for a multi-channel physical action): bytes 2, 22 and 29 flip.
    bool armed = false;
    r.mock.setWriteHook([&] { armed = true; });
    int readsAfterWrite = 0;
    r.mock.setReadHook([&] {
        if (!armed) return;
        if (++readsAfterWrite == 2) {          // after commit()'s own re-read
            auto b = r.mock.settingsBytes();
            b[2] = b[22] = b[29] = 1;
            r.mock.setSettingsBytes(b);
        }
    });
    r.mock.setStaleReadsAfterWrite(1);        // force at least one polling round
    auto res = r.s->commitAndVerify(fast());
    EXPECT_EQ(res.state, WriteState::Ambiguous);
    for (std::size_t off : {2u, 22u, 29u})
        EXPECT_NE(std::find(res.unexpectedOffsets.begin(), res.unexpectedOffsets.end(), off),
                  res.unexpectedOffsets.end()) << "offset " << off;
    EXPECT_EQ(r.settingsWrites(), 1) << "the SET is never retried";
}
