#include "UsbDiagnostics.hpp"

#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace lewitt {
namespace {

std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\r')) s.pop_back();
    return s;
}
unsigned long hexFile(const std::string& p) { return std::strtoul(trim(readFile(p)).c_str(), nullptr, 16); }

std::string linkName(const std::string& p) {
    char buf[512];
    ssize_t n = readlink(p.c_str(), buf, sizeof buf - 1);
    if (n < 0) return {};
    buf[n] = 0;
    std::string s(buf);
    auto k = s.rfind('/');
    return k == std::string::npos ? s : s.substr(k + 1);
}

void diagnoseNode(UsbDeviceReport& r) {
    char path[64];
    std::snprintf(path, sizeof path, "/dev/bus/usb/%03d/%03d", r.bus, r.address);
    r.usbfsNode = path;
    struct stat st{};
    if (stat(path, &st) != 0) return;
    r.nodeExists = true;
    // access(2) honours ACLs and group membership: this is the exact question libusb_open asks.
    r.nodeReadable = access(path, R_OK) == 0;
    r.nodeWritable = access(path, W_OK) == 0;
    if (auto* pw = getpwuid(st.st_uid)) r.nodeOwner = pw->pw_name;
    if (auto* gr = getgrgid(st.st_gid)) r.nodeGroup = gr->gr_name;
    char mode[8];
    std::snprintf(mode, sizeof mode, "%04o", st.st_mode & 07777);
    r.nodeMode = mode;
    r.nodeHasAcl = false;
    // An ACL ('+' in `ls -l`) is visible as the xattr system.posix_acl_access.
    r.nodeHasAcl = getxattr(path, "system.posix_acl_access", nullptr, 0) > 0;
}

}  // namespace

bool parseDescriptors(const Bytes& raw, UsbDeviceReport& out) {
    if (raw.size() < 18 || raw[0] != 18 || raw[1] != 1) return false;
    out.vid = static_cast<std::uint16_t>(raw[8] | (raw[9] << 8));
    out.pid = static_cast<std::uint16_t>(raw[10] | (raw[11] << 8));
    out.bcdDevice = static_cast<std::uint16_t>(raw[12] | (raw[13] << 8));
    std::size_t i = 18;
    out.interfaces.clear();
    UsbInterfaceInfo* cur = nullptr;
    int config = 1;
    while (i + 2 <= raw.size()) {
        std::uint8_t len = raw[i], type = raw[i + 1];
        if (len < 2 || i + len > raw.size()) break;
        if (type == 2 && len >= 9) {            // configuration descriptor
            config = raw[i + 5];
            cur = nullptr;
        } else if (type == 4 && len >= 9) {     // interface descriptor
            UsbInterfaceInfo f;
            f.config = config;
            f.number = raw[i + 2];
            f.altSetting = raw[i + 3];
            f.klass = raw[i + 5];
            f.subclass = raw[i + 6];
            f.protocol = raw[i + 7];
            out.interfaces.push_back(f);
            cur = &out.interfaces.back();
        } else if (type == 5 && len >= 7 && cur) {   // endpoint
            cur->endpoints.push_back({raw[i + 2], raw[i + 3],
                                      static_cast<std::uint16_t>(raw[i + 4] | (raw[i + 5] << 8)), raw[i + 6]});
        } else if (type == 0x24 && len >= 5 && cur && cur->klass == 1 && cur->subclass == 1 &&
                   raw[i + 2] == 0x01) {            // class-specific AC header: bcdADC
            cur->bcdAdc = static_cast<std::uint16_t>(raw[i + 3] | (raw[i + 4] << 8));
        }
        i += len;
    }
    return true;
}

std::vector<UsbDeviceReport> scanSysfs(std::uint16_t vendorId, const std::string& root) {
    std::vector<UsbDeviceReport> out;
    DIR* d = opendir(root.c_str());
    if (!d) return out;
    while (auto* e = readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == ".." || name.find(':') != std::string::npos) continue;  // devices only
        std::string base = root + "/" + name;
        std::string vid = trim(readFile(base + "/idVendor"));
        if (vid.empty() || std::strtoul(vid.c_str(), nullptr, 16) != vendorId) continue;
        UsbDeviceReport r;
        r.source = "sysfs";
        r.sysfsPath = base;
        r.vid = vendorId;
        r.pid = static_cast<std::uint16_t>(hexFile(base + "/idProduct"));
        r.bus = static_cast<int>(std::strtol(trim(readFile(base + "/busnum")).c_str(), nullptr, 10));
        r.address = static_cast<int>(std::strtol(trim(readFile(base + "/devnum")).c_str(), nullptr, 10));
        r.product = trim(readFile(base + "/product"));
        r.manufacturer = trim(readFile(base + "/manufacturer"));
        r.serial = trim(readFile(base + "/serial"));
        auto raw = readFile(base + "/descriptors");
        Bytes b(raw.begin(), raw.end());
        parseDescriptors(b, r);
        // bound drivers: <dev>:<cfg>.<if>/driver symlinks
        if (DIR* di = opendir(base.c_str())) {
            while (auto* ie = readdir(di)) {
                std::string in = ie->d_name;
                if (in.rfind(name + ":", 0) != 0) continue;
                auto dot = in.rfind('.');
                if (dot == std::string::npos) continue;
                int ifn = std::atoi(in.c_str() + dot + 1);
                std::string drv = linkName(base + "/" + in + "/driver");
                for (auto& f : r.interfaces)
                    if (f.number == ifn && f.boundDriver.empty()) f.boundDriver = drv;
            }
            closedir(di);
        }
        diagnoseNode(r);
        out.push_back(std::move(r));
    }
    closedir(d);
    return out;
}

std::string suggestionFor(const Error& e) {
    switch (e.code) {
        case Errc::AccessDenied:
            return "Filesystem permission problem (EACCES/EPERM): this user cannot open the USB device node "
                   "read/write. Driver ownership is a separate issue. Without administrator rights the node "
                   "permissions cannot be changed; an administrator can add a udev rule "
                   "(packaging/udev/70-openwitty.rules). Descriptors and driver bindings can still be "
                   "shown from sysfs (`openwitty-cli devices`).";
        case Errc::Busy:
            return "Interface is owned by another kernel driver (EBUSY, normally snd-usb-audio). File "
                   "permissions are not the cause and cannot fix it. Options: try "
                   "`--method device-recipient` (read-only experiment), or see docs/USB_ACCESS.md for the "
                   "kernel-side alternative. Do NOT detach the driver unless you accept losing audio.";
        case Errc::Stall:
            return "The device rejected the request (EPIPE/stall). The link is fine and the device is still "
                   "connected: the request variant, recipient or wValue is probably not what this firmware "
                   "expects. Try `--request uac1` or another --method.";
        case Errc::Timeout:
            return "No answer within 500 ms. The device may be busy or in a different mode; retry once and "
                   "check `openwitty-cli devices` for the current product id.";
        case Errc::Disconnected:
            return "The device disappeared (unplugged or re-enumerating, e.g. after a mode change).";
        case Errc::ShortTransfer:
            return "The device returned fewer bytes than the message size; response discarded.";
        case Errc::Malformed:
            return "The response was not a valid message; it was discarded.";
        case Errc::NotSupported:
            return "Unsupported by this build or device (protocol/API version or device mode).";
        case Errc::NotFound:
            return "No matching CONNECT 2 found (VID 0x29c2, PID 0x0002/0x0004). Check `openwitty-cli devices`.";
        case Errc::Cancelled:
            return "Request was cancelled by the application.";
        default:
            return "";
    }
}

std::string formatReport(const UsbDeviceReport& r) {
    std::ostringstream o;
    char buf[160];
    std::snprintf(buf, sizeof buf, "%04x:%04x  bus %d dev %d  bcdDevice %04x  [%s]\n", r.vid, r.pid, r.bus,
                  r.address, r.bcdDevice, r.sysfsPath.c_str());
    o << buf;
    if (!r.manufacturer.empty() || !r.product.empty())
        o << "  " << r.manufacturer << " / " << r.product << (r.serial.empty() ? "" : "  serial " + r.serial) << "\n";
    for (const auto& i : r.interfaces) {
        std::snprintf(buf, sizeof buf, "  cfg %d if %d alt %d  class %02x/%02x/%02x%s  driver: %s", i.config, i.number,
                      i.altSetting, i.klass, i.subclass, i.protocol,
                      i.bcdAdc ? (i.bcdAdc >= 0x0200 ? "  (UAC2 AudioControl)" : "  (UAC1 AudioControl)") : "",
                      i.boundDriver.empty() ? "(none/unknown)" : i.boundDriver.c_str());
        o << buf << "\n";
        for (const auto& e : i.endpoints) {
            std::snprintf(buf, sizeof buf, "      ep 0x%02x attr 0x%02x maxpkt %u interval %u", e.address,
                          e.attributes, e.maxPacket, e.interval);
            o << buf << "\n";
        }
    }
    o << "  usbfs node " << r.usbfsNode << ": ";
    if (!r.nodeExists) o << "missing\n";
    else {
        o << r.nodeOwner << ":" << r.nodeGroup << " mode " << r.nodeMode << (r.nodeHasAcl ? " +ACL" : "")
          << "  read=" << (r.nodeReadable ? "yes" : "NO") << " write=" << (r.nodeWritable ? "yes" : "NO") << "\n";
        if (!r.nodeWritable)
            o << "  -> libusb_open (O_RDWR) will fail with EACCES; usbfs ioctls also need a writable fd "
                 "(read-only fds get EPERM).\n";
    }
    return o.str();
}

}  // namespace lewitt
