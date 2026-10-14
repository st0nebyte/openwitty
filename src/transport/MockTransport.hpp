#pragma once
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>

#include "IDeviceTransport.hpp"
#include "protocol/Messages.hpp"

namespace lewitt {

// Simulated device with fault injection. Never touches hardware.
class MockTransport final : public IDeviceTransport {
public:
    enum class Fault { None, Timeout, ShortRead, Disconnect, Stall, AccessDenied, Busy, LongRead, Internal };
    MockTransport();
    std::string name() const override { return "mock"; }
    Status open() override;
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    DeviceInfo info() const override;
    // Simulated USB mode: PID 0x0004 normal, 0x0002 macOS streaming (settings are preserved).
    DeviceMode mode() const { return mode_; }
    Result<Bytes> read(MessageId id) override;
    Status write(MessageId id, std::span<const std::uint8_t> data) override;
    bool isSimulated() const override { return true; }
    void requestCancel() override { cancel_.store(true); }
    void clearCancel() override { cancel_.store(false); }
    // Test hook: runs at the start of every read, after the cancel check, before data is
    // returned. Used to hold a request "in flight" from another thread.
    void setReadHook(std::function<void()> h) { readHook_ = std::move(h); }
    // Same idea as setReadHook, for write(). Lets tests arm a fault precisely for a write call
    // (e.g. "the SET itself fails") without an earlier read consuming it.
    void setWriteHook(std::function<void()> h) { writeHook_ = std::move(h); }
    void setPid(std::uint16_t pid) { pidOverride_ = pid; }

    // test / GUI hooks
    void injectFault(Fault f, int count = 1) { fault_ = f; faultCount_ = count; }
    void setPresent(bool present) { present_ = present; if (!present) open_ = false; }
    void setStatusBytes(Bytes b) { status_ = std::move(b); }
    // Simulated auto-gain: progress 0..100 advances by 10 per status read.
    bool autoGainRunning() const { return agProgress_ >= 0; }
    void setAutoGainOutcome(std::uint8_t resultBits) { agOutcome_ = resultBits; }
    // Simulates a change made on the device itself (hardware button, other software).
    void setSettingsBytes(Bytes b) { settings_ = std::move(b); }
    // Simulates the real-hardware settling delay found in Phase 6D (docs/HARDWARE_TEST_RESULTS.md
    // §"timing experiment"): the next `n` reads of Settings after any Settings write return the
    // PRE-write bytes; the (n+1)th and later reads return the real (written) value. n=0 (default)
    // disables this - reads always return the current value immediately, as before.
    void setStaleReadsAfterWrite(int n) { staleReadsBudget_ = n; }
    // Simulates a concurrent device-side change (e.g. a hardware button) landing mid-verification:
    // after `afterReads` subsequent read() calls of any kind, settings_[offset] is set to `value`
    // exactly once. Used to test that commitAndVerify() reports Ambiguous rather than silently
    // accepting or overwriting an out-of-band change.
    void injectExternalSettingsChange(int afterReads, std::size_t offset, std::uint8_t value) {
        externalChangeAfterReads_ = afterReads;
        externalChangeOffset_ = offset;
        externalChangeValue_ = value;
    }
    void setApiVersion(std::uint8_t major, std::uint8_t minor) { api_[0] = major; api_[1] = minor; }
    Bytes settingsBytes() const { return settings_; }
    std::vector<MessageId> writeLog() const { return log_; }

private:
    Result<Bytes> maybeFault();
    bool present_{true};
    bool open_{false};
    Fault fault_{Fault::None};
    int faultCount_{0};
    Bytes settings_;
    Bytes status_;
    Bytes version_;
    Bytes api_;
    std::vector<MessageId> log_;
    int agProgress_{-1};
    std::atomic<bool> cancel_{false};
    std::function<void()> readHook_;
    std::function<void()> writeHook_;
    std::uint16_t pidOverride_{0};
    DeviceMode mode_{DeviceMode::Normal};
    std::vector<std::uint16_t> triggerLog_;
    int staleReadsBudget_{0};
    Bytes preWriteSettings_;
    int staleReadsRemaining_{0};
    int externalChangeAfterReads_{-1};
    std::size_t externalChangeOffset_{0};
    std::uint8_t externalChangeValue_{0};
public:
    std::vector<std::uint16_t> triggers() const { return triggerLog_; }
private:
    std::uint8_t agOutcome_{0x20};  // SIMULATED result bits (meaning of values unverified)
};

}  // namespace lewitt
