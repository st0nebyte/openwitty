// commitAndVerify() / WriteVerification state machine. All simulated; no hardware.
// Mirrors real behaviour observed in docs/dev/HARDWARE_TEST_RESULTS.md's Phase 6D timing experiment
// (a GET immediately after a SET can return stale data) without relying on wall-clock timing:
// MockTransport's stale-reads-after-write and external-change injection are count-based, and
// PollPolicy's sleeper is overridden with a no-op so these tests run instantly and deterministically.
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "core/DeviceSession.hpp"
#include "transport/MockTransport.hpp"

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

// No real sleeping: the state machine's logic is exercised via MockTransport's deterministic,
// count-based staleness rather than actual elapsed time.
PollPolicy instantPolicy(std::size_t pollCount) {
    PollPolicy pp;
    pp.delays.assign(pollCount, std::chrono::milliseconds(0));
    pp.sleeper = [](std::chrono::milliseconds) {};
    return pp;
}
}  // namespace

TEST(WriteVerification, NoOpWhenNothingPending) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    auto r = f.session->commitAndVerify();
    EXPECT_EQ(r.state, WriteState::Verified);
    EXPECT_EQ(r.pollAttempts, 0);
    EXPECT_TRUE(f.mock->writeLog().empty());
}

TEST(WriteVerification, VerifiedImmediatelyWhenNoStaleness) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    ASSERT_TRUE(f.session->edit([](SettingsMessage& s) { return s.setLedBrightness(60); }).ok());
    auto r = f.session->commitAndVerify(instantPolicy(5));
    EXPECT_EQ(r.state, WriteState::Verified);
    EXPECT_EQ(r.pollAttempts, 0);   // the readback inside commit() already matched; no polling needed
    EXPECT_EQ(f.session->settings()->ledBrightness(), 60);
}

TEST(WriteVerification, AwaitingConvergenceThenVerified) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    f.mock->setStaleReadsAfterWrite(2);   // first 2 reads after the write return the OLD value
    ASSERT_TRUE(f.session->edit([](SettingsMessage& s) { return s.setLedBrightness(60); }).ok());
    auto r = f.session->commitAndVerify(instantPolicy(5));
    EXPECT_EQ(r.state, WriteState::Verified);
    EXPECT_EQ(r.pollAttempts, 2);   // 1 stale (consumed by commit()'s own readback) + 1 more stale, then match
    EXPECT_EQ(f.session->settings()->ledBrightness(), 60);
}

TEST(WriteVerification, VerificationTimesOutIfNeverConverges) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    f.mock->setStaleReadsAfterWrite(100);   // far more than the poll budget below
    ASSERT_TRUE(f.session->edit([](SettingsMessage& s) { return s.setLedBrightness(60); }).ok());
    auto r = f.session->commitAndVerify(instantPolicy(3));
    EXPECT_EQ(r.state, WriteState::VerificationTimedOut);
    EXPECT_EQ(r.pollAttempts, 3);
    // No automatic retry of the SET: exactly one write was sent, no matter how long verification took.
    EXPECT_EQ(f.mock->writeLog().size(), 1u);
}

TEST(WriteVerification, AmbiguousWhenUnrelatedByteChangesDuringTransfer) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    // afterReads=1: the FIRST read is commitAndVerify's own pre-write fresh refresh (any change
    // landing there is legitimately absorbed into the "before" snapshot via the existing merge
    // algorithm - that's correct, not ambiguous). This fires one read later, on commit()'s own
    // internal post-write readback: a concurrent device-side change (e.g. a hardware button)
    // landing right as our write completes, after "before"/"expected" were already captured.
    f.mock->injectExternalSettingsChange(1, /*offset=*/29 /*kHeadphoneMute*/, 1);
    ASSERT_TRUE(f.session->edit([](SettingsMessage& s) { return s.setLedBrightness(60); }).ok());
    auto r = f.session->commitAndVerify(instantPolicy(5));
    EXPECT_EQ(r.state, WriteState::Ambiguous);
    EXPECT_EQ(r.pollAttempts, 0);   // detected in the immediate post-transfer check, not polling
    ASSERT_EQ(r.unexpectedOffsets.size(), 1u);
    EXPECT_EQ(r.unexpectedOffsets[0], 29u);
    // Ambiguous is not auto-resolved: no further writes were sent attempting to "fix" it.
    EXPECT_EQ(f.mock->writeLog().size(), 1u);
}

TEST(WriteVerification, AmbiguousDuringPolling) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    f.mock->setStaleReadsAfterWrite(1);
    // Reads, in order: (1) commitAndVerify's pre-write fresh refresh [externalChangeAfterReads_
    // 1->0, no apply yet], (2) commit()'s own post-write readback [counter at 0 -> applies the
    // external change to the live settings_ now, but this read is still stale-masked by the
    // snapshot taken at write() time, so the change isn't visible in *this* read's result], (3)
    // the first poll iteration's read [staleness budget exhausted -> returns the real settings_,
    // which reveals both our write AND the external change at once]. So this surfaces specifically
    // during polling (pollAttempts == 1), not in the immediate post-transfer check.
    f.mock->injectExternalSettingsChange(1, /*offset=*/22 /*kSpeakerMute*/, 1);
    ASSERT_TRUE(f.session->edit([](SettingsMessage& s) { return s.setLedBrightness(60); }).ok());
    auto r = f.session->commitAndVerify(instantPolicy(5));
    EXPECT_EQ(r.state, WriteState::Ambiguous);
    EXPECT_EQ(r.pollAttempts, 1);
    ASSERT_EQ(r.unexpectedOffsets.size(), 1u);
    EXPECT_EQ(r.unexpectedOffsets[0], 22u);
}

TEST(WriteVerification, FailedWhenTransferItselfFails) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    ASSERT_TRUE(f.session->edit([](SettingsMessage& s) { return s.setLedBrightness(60); }).ok());
    // Arm the fault precisely for the write call, so it's specifically the SET that fails, not
    // the pre-write fresh refresh (an ordinary read) that commitAndVerify performs first.
    f.mock->setWriteHook([&] { f.mock->injectFault(MockTransport::Fault::Stall, 1); });
    auto r = f.session->commitAndVerify(instantPolicy(5));
    EXPECT_EQ(r.state, WriteState::Failed);
    EXPECT_EQ(r.error.code, Errc::Stall);
    EXPECT_EQ(r.pollAttempts, 0);
}

TEST(WriteVerification, FailedOnDisconnectDuringPolling) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    f.mock->setStaleReadsAfterWrite(10);
    ASSERT_TRUE(f.session->edit([](SettingsMessage& s) { return s.setLedBrightness(60); }).ok());
    // injectFault's count fires on the NEXT N calls immediately, not "after N successful calls",
    // so arm it dynamically once the 3rd read happens (1: commitAndVerify's pre-write refresh,
    // 2: commit()'s own post-write readback, 3: the first poll iteration) - this precisely targets
    // "disconnects while awaiting convergence", not before.
    int reads = 0;
    f.mock->setReadHook([&] {
        if (++reads == 3) f.mock->injectFault(MockTransport::Fault::Disconnect, 1);
    });
    auto r = f.session->commitAndVerify(instantPolicy(5));
    EXPECT_EQ(r.state, WriteState::Failed);
    EXPECT_EQ(r.error.code, Errc::Disconnected);
    EXPECT_EQ(r.pollAttempts, 1);   // confirms it happened during polling, not before
}

namespace {
// Mirrors test_session.cpp's Session.WritesDisabledForRealPolicy: a non-simulated transport with
// the default (writes-disabled) policy must refuse commitAndVerify just as it refuses commit().
struct NonSimulatedFake final : IDeviceTransport {
    std::string name() const override { return "fake-hw"; }
    Status open() override { open_ = true; return okStatus(); }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    DeviceInfo info() const override { return {}; }
    Result<Bytes> read(MessageId id) override {
        Bytes b(messageInfo(id)->size, 0);
        if (id == MessageId::ApiVersion) b[0] = 1;
        return b;
    }
    Status write(MessageId, std::span<const std::uint8_t>) override { ++writes; return okStatus(); }
    bool open_{false};
    int writes{0};
};
}  // namespace

TEST(WriteVerification, WritesDisabledIsFailedNotSilentlyIgnored) {
    auto t = std::make_unique<NonSimulatedFake>();
    NonSimulatedFake* raw = t.get();
    DeviceSession s(std::move(t));   // default policy: allowHardwareWrites=false
    ASSERT_TRUE(s.connect().ok());
    EXPECT_FALSE(s.writesEnabled());
    ASSERT_TRUE(s.edit([](SettingsMessage& m) { return m.setLedBrightness(60); }).ok());
    auto r = s.commitAndVerify();
    EXPECT_EQ(r.state, WriteState::Failed);
    EXPECT_EQ(r.error.code, Errc::WritesDisabled);
    EXPECT_EQ(raw->writes, 0);   // refused before any USB write was attempted
}

TEST(WriteVerification, PreservesConcurrentUnrelatedDeviceChangeBeforeCommit) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    ASSERT_TRUE(f.session->edit([](SettingsMessage& s) { return s.setLedBrightness(60); }).ok());
    // A concurrent, unrelated device-side change (e.g. a hardware mute button) happens before we
    // commit: simulate it by mutating the mock's backing settings directly, as a hardware button
    // press would, bypassing our session entirely.
    auto changed = f.mock->settingsBytes();
    changed[29] = 1;   // kHeadphoneMute, unrelated to our pending LED edit
    f.mock->setSettingsBytes(changed);
    auto r = f.session->commitAndVerify(instantPolicy(5));
    EXPECT_EQ(r.state, WriteState::Verified);
    // Both the external change AND our edit must be present afterward - neither was clobbered.
    EXPECT_EQ(f.session->settings()->ledBrightness(), 60);
    EXPECT_TRUE(f.session->settings()->headphoneMute());
}

TEST(WriteVerification, StateNamesAreAllDistinctAndNonEmpty) {
    const WriteState all[] = {WriteState::Prepared,  WriteState::Submitted,
                               WriteState::TransferCompleted, WriteState::AwaitingConvergence,
                               WriteState::Verified,  WriteState::VerificationTimedOut,
                               WriteState::Ambiguous, WriteState::Failed};
    std::vector<std::string> names;
    for (auto s : all) {
        std::string n = writeStateName(s);
        EXPECT_FALSE(n.empty());
        names.push_back(n);
    }
    std::sort(names.begin(), names.end());
    EXPECT_EQ(std::adjacent_find(names.begin(), names.end()), names.end());   // all distinct
}

// HW 11B session 1: the device sets byte 25 to the Dim value itself. A Dim write accepts that change; the same
// byte change during any other write is still treated as unexpected.
TEST(WriteVerification, DimWriteAcceptsTheDevicesOwnDimStateByte) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    f.mock->injectExternalSettingsChange(1, settings_off::kSpeakerDimState, 1);
    ASSERT_TRUE(f.session->edit([](SettingsMessage& s) { return s.setDim(true); }).ok());
    auto r = f.session->commitAndVerify(instantPolicy(5));
    EXPECT_EQ(r.state, WriteState::Verified);
    EXPECT_TRUE(r.unexpectedOffsets.empty());
    EXPECT_EQ(f.mock->writeLog().size(), 1u);
}

TEST(WriteVerification, DimStateByteChangingDuringAnotherWriteIsStillUnexpected) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    f.mock->injectExternalSettingsChange(1, settings_off::kSpeakerDimState, 1);
    ASSERT_TRUE(f.session->edit([](SettingsMessage& s) { return s.setMono(true); }).ok());
    auto r = f.session->commitAndVerify(instantPolicy(5));
    EXPECT_EQ(r.state, WriteState::Ambiguous);
    ASSERT_EQ(r.unexpectedOffsets.size(), 1u);
    EXPECT_EQ(r.unexpectedOffsets[0], settings_off::kSpeakerDimState);
}

TEST(WriteVerification, MonoWriteAcceptsTheDevicesOwnMonoStateByte) {
    Fixture f;
    ASSERT_TRUE(f.session->connect().ok());
    f.mock->injectExternalSettingsChange(1, settings_off::kSpeakerMonoState, 1);
    ASSERT_TRUE(f.session->edit([](SettingsMessage& s) { return s.setMono(true); }).ok());
    auto r = f.session->commitAndVerify(instantPolicy(5));
    EXPECT_EQ(r.state, WriteState::Verified);
    EXPECT_TRUE(r.unexpectedOffsets.empty());
}
