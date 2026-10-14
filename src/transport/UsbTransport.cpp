#include "UsbTransport.hpp"

#include <libusb.h>

namespace lewitt {
Error mapLibusbError(int rc, const char* what) {
    Errc c = Errc::Io;
    const char* hint = "";
    switch (rc) {
        case LIBUSB_ERROR_TIMEOUT: c = Errc::Timeout; break;
        case LIBUSB_ERROR_NO_DEVICE: c = Errc::Disconnected; break;
        case LIBUSB_ERROR_PIPE:
            c = Errc::Stall;
            hint = " (device rejected the request; unsupported request variant or recipient?)";
            break;
        case LIBUSB_ERROR_BUSY:
            c = Errc::Busy;
            hint = " (interface is owned by another driver, e.g. snd-usb-audio; this is a kernel "
                   "ownership conflict, not a file-permission problem)";
            break;
        case LIBUSB_ERROR_ACCESS:
            c = Errc::AccessDenied;
            hint = " (no filesystem permission on /dev/bus/usb node; a udev rule/uaccess is needed; "
                   "unrelated to driver ownership)";
            break;
        case LIBUSB_ERROR_NOT_FOUND: c = Errc::NotFound; break;
        default: break;
    }
    return makeError(c, std::string(what) + ": " + libusb_error_name(rc) + hint);
}

namespace {
Error mapErr(int rc, const char* what) { return mapLibusbError(rc, what); }
bool isConnect2Pid(std::uint16_t pid) {
    for (auto p : kConnect2ProductIds) if (p == pid) return true;
    return false;
}
}  // namespace

UsbTransport::UsbTransport(UsbOptions o) : opt_(o) {}
UsbTransport::~UsbTransport() { close(); }

std::vector<DeviceInfo> UsbTransport::enumerate() {
    std::vector<DeviceInfo> out;
    libusb_context* c = nullptr;
    if (libusb_init(&c) != 0) return out;
    libusb_device** list = nullptr;
    ssize_t n = libusb_get_device_list(c, &list);
    for (ssize_t i = 0; i < n; ++i) {
        libusb_device_descriptor d{};
        if (libusb_get_device_descriptor(list[i], &d) == 0 && d.idVendor == kLewittVendorId) {
            DeviceInfo di;
            di.vid = d.idVendor;
            di.pid = d.idProduct;
            di.backend = "usb";
            out.push_back(di);
        }
    }
    if (n >= 0) libusb_free_device_list(list, 1);
    libusb_exit(c);
    return out;
}

Status UsbTransport::open() {
    if (handle_) return okStatus();
    if (int rc = libusb_init(&ctx_); rc != 0) return mapErr(rc, "libusb_init");
    libusb_device** list = nullptr;
    ssize_t n = libusb_get_device_list(ctx_, &list);
    libusb_device* found = nullptr;
    libusb_device_descriptor fd{};
    for (ssize_t i = 0; i < n && !found; ++i) {
        libusb_device_descriptor d{};
        if (libusb_get_device_descriptor(list[i], &d) != 0) continue;
        if (d.idVendor != kLewittVendorId) continue;
        if (opt_.pid ? d.idProduct != opt_.pid : !isConnect2Pid(d.idProduct)) continue;
        found = list[i];
        fd = d;
    }
    if (!found) {
        if (n >= 0) libusb_free_device_list(list, 1);
        libusb_exit(ctx_); ctx_ = nullptr;
        return makeError(Errc::NotFound, "no LEWITT CONNECT 2 found");
    }
    int rc = libusb_open(found, &handle_);
    libusb_free_device_list(list, 1);
    if (rc != 0) { libusb_exit(ctx_); ctx_ = nullptr; handle_ = nullptr; return mapErr(rc, "libusb_open"); }
    info_ = DeviceInfo{fd.idVendor, fd.idProduct, "", "", "usb"};
    unsigned char buf[128];
    if (fd.iProduct && libusb_get_string_descriptor_ascii(handle_, fd.iProduct, buf, sizeof buf) > 0)
        info_.product = reinterpret_cast<char*>(buf);
    if (fd.iSerialNumber && libusb_get_string_descriptor_ascii(handle_, fd.iSerialNumber, buf, sizeof buf) > 0)
        info_.serial = reinterpret_cast<char*>(buf);
    if (auto s = ensureInterface(); !s) { close(); return s; }
    return okStatus();
}

Status UsbTransport::ensureInterface() {
    if (opt_.interfaceNumber >= 0) { interface_ = opt_.interfaceNumber; return okStatus(); }
    libusb_config_descriptor* cfg = nullptr;
    if (int rc = libusb_get_active_config_descriptor(libusb_get_device(handle_), &cfg); rc != 0)
        return mapErr(rc, "get_active_config_descriptor");
    interface_ = -1;
    for (int i = 0; i < cfg->bNumInterfaces && interface_ < 0; ++i) {
        const auto& alt = cfg->interface[i].altsetting[0];
        if (alt.bInterfaceClass == 1 && alt.bInterfaceSubClass == 1) interface_ = alt.bInterfaceNumber;
    }
    libusb_free_config_descriptor(cfg);
    if (interface_ < 0) return makeError(Errc::NotFound, "no AudioControl interface");
    return okStatus();
}

void UsbTransport::close() {
    if (handle_) {
        if (claimed_) libusb_release_interface(handle_, interface_);
        if (detached_) libusb_attach_kernel_driver(handle_, interface_);
        libusb_close(handle_);
    }
    claimed_ = detached_ = false;
    handle_ = nullptr;
    if (ctx_) { libusb_exit(ctx_); ctx_ = nullptr; }
}

Result<Bytes> UsbTransport::read(MessageId id) {
    if (!handle_) return makeError(Errc::Disconnected, "not open");
    const MessageInfo* mi = messageInfo(id);
    if (!mi || !mi->readable) return makeError(Errc::NotSupported, "message not readable");
    if (opt_.allowDetachKernelDriver && !claimed_) {
        // EXPERIMENTAL / DISRUPTIVE path, off by default.
        if (libusb_kernel_driver_active(handle_, interface_) == 1) {
            if (int rc = libusb_detach_kernel_driver(handle_, interface_); rc != 0) return mapErr(rc, "detach");
            detached_ = true;
        }
        if (int rc = libusb_claim_interface(handle_, interface_); rc != 0) return mapErr(rc, "claim");
        claimed_ = true;
    }
    Bytes buf(mi->size);
    std::uint8_t req = opt_.request == MemRequest::Uac1Mem ? kReqUac1GetMem : kReqUac2Mem;
    int rc = libusb_control_transfer(handle_, kBmGet, req, wValueFor(id), wIndexFor(interface_),
                                     buf.data(), static_cast<std::uint16_t>(buf.size()), kTimeoutMs);
    if (rc < 0) return mapErr(rc, "control read");
    if (static_cast<std::size_t>(rc) != mi->size)
        return makeError(Errc::ShortTransfer, "short control transfer (" + std::to_string(rc) + " bytes)");
    return buf;
}

Status UsbTransport::write(MessageId id, std::span<const std::uint8_t> data) {
    if (!opt_.allowWrites)
        return makeError(Errc::WritesDisabled, "hardware writes are disabled (unverified protocol)");
    if (!handle_) return makeError(Errc::Disconnected, "not open");
    const MessageInfo* mi = messageInfo(id);
    if (!mi || !mi->writable) return makeError(Errc::NotSupported, "message not writable");
    if (data.size() != mi->size) return makeError(Errc::InvalidArgument, "wrong message size");
    std::uint8_t req = opt_.request == MemRequest::Uac1Mem ? kReqUac1SetMem : kReqUac2Mem;
    Bytes tmp(data.begin(), data.end());
    int rc = libusb_control_transfer(handle_, kBmSet, req, wValueFor(id), wIndexFor(interface_),
                                     tmp.data(), static_cast<std::uint16_t>(tmp.size()), kTimeoutMs);
    if (rc < 0) return mapErr(rc, "control write");
    if (static_cast<std::size_t>(rc) != tmp.size()) return makeError(Errc::ShortTransfer, "short write");
    return okStatus();
}

}  // namespace lewitt
