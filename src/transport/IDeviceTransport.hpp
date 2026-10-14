// Transport abstraction: the protocol layer never knows how bytes reach the device.
#pragma once
#include <string>

#include "protocol/Types.hpp"

namespace lewitt {

struct DeviceInfo {
    std::uint16_t vid{0};
    std::uint16_t pid{0};
    std::string product;
    std::string serial;
    std::string backend;
};

class IDeviceTransport {
public:
    virtual ~IDeviceTransport() = default;
    virtual std::string name() const = 0;
    virtual Status open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual DeviceInfo info() const = 0;
    // Must return exactly the message's size or an error (short transfer => error).
    virtual Result<Bytes> read(MessageId id) = 0;
    virtual Status write(MessageId id, std::span<const std::uint8_t> data) = 0;
    // True only for backends that never touch real hardware.
    virtual bool isSimulated() const { return false; }
    // Thread-safe, non-blocking. Asks the backend to abandon the in-flight request (if it
    // can) and to fail later requests with Errc::Cancelled until `clearCancel()`.
    // Backends that cannot interrupt a request still honour it between requests.
    virtual void requestCancel() {}
    virtual void clearCancel() {}
};

}  // namespace lewitt
