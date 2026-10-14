// libusb error-code classification. Uses the (ABI-stable) numeric values of
// enum libusb_error so it also compiles in mock-only builds without libusb.
#include <string>

#include "UsbTransport.hpp"

namespace lewitt {
namespace {
const char* libusbName(int rc) {
    switch (rc) {
        case 0: return "SUCCESS";
        case -1: return "IO";
        case -2: return "INVALID_PARAM";
        case -3: return "ACCESS";
        case -4: return "NO_DEVICE";
        case -5: return "NOT_FOUND";
        case -6: return "BUSY";
        case -7: return "TIMEOUT";
        case -8: return "OVERFLOW";
        case -9: return "PIPE";
        case -10: return "INTERRUPTED";
        case -11: return "NO_MEM";
        case -12: return "NOT_SUPPORTED";
        default: return "OTHER";
    }
}
}  // namespace

Error mapLibusbError(int rc, const char* what) {
    Errc c = Errc::Io;
    const char* hint = "";
    switch (rc) {
        case -7: c = Errc::Timeout; break;
        case -4: c = Errc::Disconnected; break;
        case -9:
            c = Errc::Stall;
            hint = " (device rejected the request; unsupported request variant or recipient?)";
            break;
        case -6:
            c = Errc::Busy;
            hint = " (interface is owned by another driver, e.g. snd-usb-audio; this is a kernel "
                   "ownership conflict, not a file-permission problem)";
            break;
        case -3:
            c = Errc::AccessDenied;
            hint = " (no filesystem permission on /dev/bus/usb node; a udev rule/uaccess is needed; "
                   "unrelated to driver ownership)";
            break;
        case -5: c = Errc::NotFound; break;
        case -10: c = Errc::Cancelled; break;
        case -12: c = Errc::NotSupported; break;
        default: break;
    }
    return makeError(c, std::string(what) + ": LIBUSB_ERROR_" + libusbName(rc) + hint);
}

}  // namespace lewitt
