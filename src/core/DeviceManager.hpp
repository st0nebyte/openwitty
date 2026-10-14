// Device discovery (Qt-free). Discovery uses sysfs only: it needs no usbfs access, sends
// no control traffic and works for an unprivileged user.
#pragma once
#include <functional>
#include <vector>

#include "protocol/Messages.hpp"
#include "transport/UsbDiagnostics.hpp"

namespace lewitt {

struct DeviceCandidate {
    UsbDeviceReport usb;
    DeviceMode mode{DeviceMode::Unknown};
    bool isConnect2{false};      // VID 0x29c2 and PID in kConnect2ProductIds
    bool appMode{false};         // PID 0x0004 / 0x0002 (the two modes the original accepts)
};

class DeviceManager {
public:
    using Scanner = std::function<std::vector<UsbDeviceReport>()>;
    explicit DeviceManager(Scanner scanner = {});

    std::vector<DeviceCandidate> scan();
    struct Change {
        enum class Kind { Added, Removed } kind;
        DeviceCandidate device;
    };
    // Differences since the previous poll (first poll reports everything as Added).
    std::vector<Change> poll();
    static DeviceCandidate classify(const UsbDeviceReport& r);

private:
    Scanner scanner_;
    std::vector<DeviceCandidate> last_;
};

}  // namespace lewitt
