#pragma once
#include <deque>
#include <mutex>

#include "IDeviceTransport.hpp"

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
    std::vector<MessageId> log_;
    int agProgress_{-1};
    std::uint8_t agOutcome_{0x20};  // SIMULATED result bits (meaning of values unverified)
};

}  // namespace lewitt
