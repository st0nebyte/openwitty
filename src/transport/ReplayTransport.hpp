// Deterministic, read-only transport for sanitized settings/status capture fixtures.
// It intentionally has no USB implementation, serial number, or write capability.
#pragma once

#include <vector>

#include "IDeviceTransport.hpp"

namespace lewitt {

class ReplayTransport final : public IDeviceTransport {
public:
    // Each Settings/Status read advances one frame; after the final frame it repeats it.
    // Fixtures must contain exactly message-sized, already-redacted payloads.
    ReplayTransport(std::vector<Bytes> settingsFrames, std::vector<Bytes> statusFrames = {});

    std::string name() const override { return "replay"; }
    Status open() override;
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    DeviceInfo info() const override { return {kLewittVendorId, 0x0004, "CONNECT 2 (replay)", "", "replay"}; }
    Result<Bytes> read(MessageId id) override;
    Status write(MessageId, std::span<const std::uint8_t>) override {
        return makeError(Errc::WritesDisabled, "replay transport is read-only");
    }

private:
    Result<Bytes> next(std::vector<Bytes>& frames, std::size_t& index, MessageId id);
    bool open_{false};
    std::vector<Bytes> settings_;
    std::vector<Bytes> status_;
    std::size_t settingsIndex_{0};
    std::size_t statusIndex_{0};
};

}  // namespace lewitt
