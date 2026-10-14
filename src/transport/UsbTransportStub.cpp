// Mock-only build (LEWITT_ENABLE_USB=OFF): no libusb. The hardware transport reports
// "not supported" so that every other layer behaves identically.
#include "UsbTransport.hpp"
#include "protocol/WriteGuard.hpp"

namespace lewitt {

UsbTransport::UsbTransport(UsbOptions o) : opt_(o) {}
UsbTransport::~UsbTransport() = default;

static Error unsupported() {
    return makeError(Errc::NotSupported, "this build has no USB support (LEWITT_ENABLE_USB=OFF); "
                                         "use the simulated device");
}
std::vector<DeviceInfo> UsbTransport::enumerate() { return {}; }
Status UsbTransport::open() { return unsupported(); }
void UsbTransport::close() {}
Result<Bytes> UsbTransport::read(MessageId) { return unsupported(); }
Status UsbTransport::write(MessageId id, std::span<const std::uint8_t> data) {
    // Same guard order as the libusb transport so behaviour is identical in both builds.
    if (!opt_.allowWrites)
        return makeError(Errc::WritesDisabled, "hardware writes are disabled (unverified protocol)");
    if (auto g = checkWriteAllowed(id, data, WriteGuardContext{false, false}); !g) return g;
    return unsupported();
}
Status UsbTransport::ensureInterface() { return unsupported(); }

}  // namespace lewitt
