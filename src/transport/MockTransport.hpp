#pragma once
#include <deque>
#include <mutex>

#include "IDeviceTransport.hpp"
#include "protocol/Messages.hpp"

namespace lewitt {

// Simulated device with fault injection. Never touches hardware.
class MockTransport final : public IDeviceTransport {
public:
    enum class Fault { None, Timeout, ShortRead, Disconnect, Stall };
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

    // test / GUI hooks
    void injectFault(Fault f, int count = 1) { fault_ = f; faultCount_ = count; }
    void setPresent(bool present) { present_ = present; if (!present) open_ = false; }
    void setStatusBytes(Bytes b) { status_ = std::move(b); }
    // Simulated auto-gain: progress 0..100 advances by 10 per status read.
    bool autoGainRunning() const { return agProgress_ >= 0; }
    void setAutoGainOutcome(std::uint8_t resultBits) { agOutcome_ = resultBits; }
    // Simulates a change made on the device itself (hardware button, other software).
    void setSettingsBytes(Bytes b) { settings_ = std::move(b); }
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
    DeviceMode mode_{DeviceMode::Normal};
    std::vector<std::uint16_t> triggerLog_;
public:
    std::vector<std::uint16_t> triggers() const { return triggerLog_; }
private:
    std::uint8_t agOutcome_{0x20};  // SIMULATED result bits (meaning of values unverified)
};

}  // namespace lewitt
