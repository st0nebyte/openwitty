#pragma once
#include <atomic>
#include <optional>

#include "ControlLease.hpp"
#include "IDeviceTransport.hpp"

struct libusb_context;
struct libusb_device_handle;

namespace lewitt {

// Maps a libusb error code to our taxonomy. EACCES (file permission), EBUSY
// (claimed by another driver) and EPIPE (device stall) are deliberately distinct.
Error mapLibusbError(int libusbErrorCode, const char* what);

// Which control-request variant to use. Which one the device wants is
// unverified; Uac2Mem is what the macOS app uses for older/default firmware,
// Uac1Mem is selected there when a version-like field is >= 0x0b.
enum class MemRequest { Uac2Mem, Uac1Mem };

// How the control request is addressed. Only `InterfaceRecipient` has a known-good
// shape (it is what both official apps send). The others are experiments.
enum class AccessMethod {
    InterfaceRecipient,   // bmRequestType 0xA1/0x21, no *explicit* claim or detach. Linux usbfs
                          // implicitly claims an unbound interface on first class GET; another
                          // process cannot use that interface until the handle closes.
    DeviceRecipient,      // bmRequestType 0xA0/0x20: usbfs skips the claim check (verified), but whether
                          // the firmware accepts a class request addressed to the device is UNKNOWN.
    DetachAndClaim,       // DISRUPTIVE: detaches snd-usb-audio (sound card disappears), claims, reattaches.
};

// How the interface number for wIndex is chosen when no explicit number is given.
enum class InterfaceStrategy {
    AudioControl,        // legacy: first class 01/01 interface (kernel-owned; EIO on a real CONNECT 2)
    VendorControlAuto,   // descriptor-driven: the unbound vendor-specific ff/f0 control interface
                         // (see InterfaceSelection.hpp); fails closed, never DFU, never kernel-bound
};

struct UsbOptions {
    MemRequest request{MemRequest::Uac2Mem};
    AccessMethod method{AccessMethod::InterfaceRecipient};
    InterfaceStrategy interfaceStrategy{InterfaceStrategy::AudioControl};
    std::uint16_t pid{0};            // 0 = first matching Connect 2 PID
    int interfaceNumber{-1};         // -1 = discover AudioControl interface
    // Legacy alias for method == DetachAndClaim.
    bool allowDetachKernelDriver{false};
    // Writes are refused at transport level unless explicitly enabled.
    bool allowWrites{false};
    // Diagnostics can avoid even reading device string descriptors (especially serial).
    bool readDeviceStrings{true};
    // Transport-level gate for the disruptive detach/claim path: even if a caller selects
    // DetachAndClaim / allowDetachKernelDriver, read() refuses unless this is also set.
    // Only an explicit, user-confirmed action (CLI: LEWITT_ALLOW_DISRUPTIVE=1) may set it.
    bool allowDisruptive{false};
};

// The one configuration the GUI is allowed to use on real hardware: reads only, interface chosen
// from the descriptors, no detach/claim, no disruptive path. Kept as a function so tests can pin it.
inline UsbOptions readOnlyHardwareOptions() {
    UsbOptions o;
    o.request = MemRequest::Uac2Mem;
    o.method = AccessMethod::InterfaceRecipient;
    o.interfaceStrategy = InterfaceStrategy::VendorControlAuto;
    o.interfaceNumber = -1;
    o.allowDetachKernelDriver = false;
    o.allowWrites = false;
    o.allowDisruptive = false;
    return o;
}

// Phase 8D: identical to readOnlyHardwareOptions() except that the transport accepts writes. It is
// only ever paired with outputWritePolicy() (core/OutputControl.hpp), whose byte scope limits real
// hardware writes to the speaker/headphone volume and mute bytes; every DeviceSession guard,
// snapshot check and read-back verification still applies.
inline UsbOptions outputControlHardwareOptions() {
    UsbOptions o = readOnlyHardwareOptions();
    o.allowWrites = true;
    return o;
}

// libusb transport. No explicit claim or detachment in the default path; usbfs may implicitly
// claim the selected interface for this handle. A cooperative per-user lease keeps two OpenWitty
// processes from competing for it. It does not coordinate unrelated software.
class UsbTransport final : public IDeviceTransport {
public:
    explicit UsbTransport(UsbOptions o = {});
    ~UsbTransport() override;
    std::string name() const override { return "usb"; }
    Status open() override;
    void close() override;
    bool isOpen() const override { return handle_ != nullptr; }
    DeviceInfo info() const override { return info_; }
    Result<Bytes> read(MessageId id) override;
    void requestCancel() override { cancel_.store(true); }
    void clearCancel() override { cancel_.store(false); }
    Status write(MessageId id, std::span<const std::uint8_t> data) override;

    // Enumerate only; sends no control traffic.
    static std::vector<DeviceInfo> enumerate();

private:
    Status ensureInterface();
    UsbOptions opt_;
    std::atomic<bool> cancel_{false};
    libusb_context* ctx_{nullptr};
    libusb_device_handle* handle_{nullptr};
    DeviceInfo info_;
    int interface_{-1};
    bool claimed_{false};
    bool detached_{false};
    std::optional<ControlLease> lease_;
};

}  // namespace lewitt
