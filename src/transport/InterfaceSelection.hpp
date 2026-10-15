// Choosing which USB interface carries the control (MEM) protocol. Pure logic: no libusb, no
// sysfs, no I/O - callers hand in descriptor facts, so it is fully unit-testable.
//
// Hardware evidence (docs/HARDWARE_TEST_RESULTS.md §2, docs/USB_ACCESS.md §3b), CONNECT 2,
// VID 0x29C2 PID 0x0004, bcdDevice 0121:
//   if 0  class 01/01/20  UAC2 AudioControl     bound to snd-usb-audio   -> EIO on MEM GET
//   if 1,2 class 01/02/20 AudioStreaming         bound to snd-usb-audio
//   if 3  class ff/f0/00  vendor-specific, 0 ep  unbound                 -> MEM GET and SET work
//   if 4  class fe/01/01  DFU (firmware update)  unbound                 -> PROHIBITED
//
// The rule is deliberately narrow: a *generic* vendor-specific interface is not assumed safe.
// Only the documented signature (class 0xFF, subclass 0xF0, no endpoints) on a recognized
// CONNECT 2 APP-mode device qualifies, and only if it is demonstrably not claimed by a kernel
// driver. Anything else fails closed with a diagnostic.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "protocol/Types.hpp"

namespace lewitt {

struct InterfaceDescriptor {
    int number{0};
    std::uint8_t klass{0}, subclass{0}, protocol{0};
    int numEndpoints{0};
    // Kernel driver ownership. `driverKnown == false` means the query failed or was not possible;
    // that is treated as "unsafe", never as "unbound".
    bool driverKnown{false};
    bool driverBound{false};
};

inline constexpr std::uint8_t kUsbClassAudio = 0x01;
inline constexpr std::uint8_t kUsbClassApplicationSpecific = 0xFE;   // DFU
inline constexpr std::uint8_t kUsbClassVendorSpecific = 0xFF;
inline constexpr std::uint8_t kControlInterfaceSubclass = 0xF0;      // observed on the CONNECT 2

// True for interfaces that must never be targeted by this application (firmware update).
constexpr bool isProhibitedInterface(const InterfaceDescriptor& d) {
    return d.klass == kUsbClassApplicationSpecific;
}

// Picks the control interface or fails closed:
//   NotSupported - VID/PID is not a supported CONNECT 2 APP mode (bootloader 0x000A gets its own message)
//   NotFound     - no interface qualifies (message lists every interface and why it was rejected)
//   Busy         - a qualifying interface exists but is bound to a kernel driver / ownership unknown
//   Malformed    - more than one qualifying interface: ambiguous, refuse to guess
Result<int> selectControlInterface(std::uint16_t vid, std::uint16_t pid,
                                   std::span<const InterfaceDescriptor> interfaces);

// Safety check for an explicitly requested interface number (CLI --interface N): refuses
// firmware-update interfaces; unknown numbers are left to the caller (experimentation stays possible).
Status checkExplicitInterface(int number, std::span<const InterfaceDescriptor> interfaces);

}  // namespace lewitt
