// Read-only USB diagnostics. Nothing in here sends a control transfer to the device and
// nothing claims, detaches or resets anything. Qt-free.
#pragma once
#include <string>
#include <vector>

#include "protocol/Types.hpp"

namespace lewitt {

struct UsbEndpointInfo { std::uint8_t address{0}; std::uint8_t attributes{0}; std::uint16_t maxPacket{0}; std::uint8_t interval{0}; };
struct UsbInterfaceInfo {
    int config{1};                           // bConfigurationValue it belongs to
    int number{0};
    int altSetting{0};
    std::uint8_t klass{0}, subclass{0}, protocol{0};
    std::string boundDriver;                 // from sysfs ("" = none / unknown)
    std::vector<UsbEndpointInfo> endpoints;
    std::uint16_t bcdAdc{0};                 // AudioControl header: 0x0100 = UAC1, 0x0200 = UAC2
};
struct UsbDeviceReport {
    std::uint16_t vid{0}, pid{0}, bcdDevice{0};
    int bus{0}, address{0};
    std::string sysfsPath;                   // e.g. /sys/bus/usb/devices/1-4
    std::string usbfsNode;                   // e.g. /dev/bus/usb/001/004
    std::string product, manufacturer, serial;
    std::vector<UsbInterfaceInfo> interfaces;
    // Permission diagnosis of the usbfs node (what libusb_open needs: read AND write).
    bool nodeExists{false}, nodeReadable{false}, nodeWritable{false};
    std::string nodeOwner, nodeGroup, nodeMode;
    bool nodeHasAcl{false};
    std::string source;                      // "sysfs" (no usbfs access needed)
};

// Enumerates devices with the given vendor id from sysfs only (works unprivileged,
// without opening the usbfs node).
std::vector<UsbDeviceReport> scanSysfs(std::uint16_t vendorId, const std::string& sysRoot = "/sys/bus/usb/devices");

// Parses a raw `descriptors` blob (device + configuration descriptors) into the report.
// Exposed separately so it can be unit-tested with byte fixtures.
bool parseDescriptors(const Bytes& raw, UsbDeviceReport& out);

// Human-readable next steps for a transport error (classification is explicit:
// EACCES/EPERM = filesystem permission, EBUSY = kernel driver ownership, EPIPE = device
// rejected the request).
std::string suggestionFor(const Error& e);

// Multi-line text summary (used by `lewitt-cli diagnose`).
std::string formatReport(const UsbDeviceReport& r);

}  // namespace lewitt
