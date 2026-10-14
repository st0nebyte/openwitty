#pragma once
#include <atomic>

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
    InterfaceRecipient,   // bmRequestType 0xA1/0x21, no claim, no detach (default). On a kernel-bound
                          // interface usbfs answers EBUSY (verified in devio.c: checkintf->claimintf).
    DeviceRecipient,      // bmRequestType 0xA0/0x20: usbfs skips the claim check (verified), but whether
                          // the firmware accepts a class request addressed to the device is UNKNOWN.
    DetachAndClaim,       // DISRUPTIVE: detaches snd-usb-audio (sound card disappears), claims, reattaches.
};

struct UsbOptions {
    MemRequest request{MemRequest::Uac2Mem};
    AccessMethod method{AccessMethod::InterfaceRecipient};
    std::uint16_t pid{0};            // 0 = first matching Connect 2 PID
    int interfaceNumber{-1};         // -1 = discover AudioControl interface
    // Legacy alias for method == DetachAndClaim.
    bool allowDetachKernelDriver{false};
    // Writes are refused at transport level unless explicitly enabled.
    bool allowWrites{false};
};

// libusb transport. Default behaviour issues control transfers WITHOUT
// claiming or detaching anything: if the kernel driver owns the interface
// the transfer fails (usbfs refuses) and an Errc::Busy is reported.
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
};

}  // namespace lewitt
