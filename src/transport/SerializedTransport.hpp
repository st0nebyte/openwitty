// Decorator: serialises requests (one in flight at a time), adds cooperative
// cancellation and collects simple statistics. Qt-free.
#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>

#include "IDeviceTransport.hpp"

namespace lewitt {

struct TransportStats {
    std::uint64_t reads{0}, writes{0}, errors{0}, cancelled{0};
    std::chrono::microseconds maxLatency{0};
    std::chrono::microseconds totalLatency{0};
};

class SerializedTransport final : public IDeviceTransport {
public:
    explicit SerializedTransport(std::unique_ptr<IDeviceTransport> inner);

    std::string name() const override { return inner_->name(); }
    Status open() override;
    void close() override;                 // waits for the in-flight request
    bool isOpen() const override;
    DeviceInfo info() const override { return inner_->info(); }
    Result<Bytes> read(MessageId id) override;
    Status write(MessageId id, std::span<const std::uint8_t> data) override;
    bool isSimulated() const override { return inner_->isSimulated(); }

    // Thread-safe and non-blocking; does not take the request lock.
    void requestCancel() override;
    void clearCancel() override;
    bool cancelRequested() const { return cancel_.load(std::memory_order_acquire); }

    TransportStats stats() const;
    IDeviceTransport& inner() { return *inner_; }

private:
    std::unique_ptr<IDeviceTransport> inner_;
    mutable std::mutex m_;                  // one request at a time
    std::atomic<bool> cancel_{false};
    mutable std::mutex statsM_;
    TransportStats stats_;
    void record(bool isWrite, bool ok, Errc code, std::chrono::steady_clock::duration d);
};

}  // namespace lewitt
