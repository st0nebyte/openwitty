#include "InterfaceSelection.hpp"

#include <algorithm>
#include <cstdio>

namespace lewitt {

namespace {
std::string hex2(unsigned v) {
    char b[8];
    std::snprintf(b, sizeof b, "%02x", v & 0xFF);
    return b;
}
std::string describe(const InterfaceDescriptor& d) {
    return "interface " + std::to_string(d.number) + " class " + hex2(d.klass) + "/" + hex2(d.subclass) + "/" +
           hex2(d.protocol) + ", " + std::to_string(d.numEndpoints) + " endpoint(s), " +
           (!d.driverKnown ? "kernel driver unknown" : d.driverBound ? "kernel driver bound" : "no kernel driver");
}
bool isSupportedDevice(std::uint16_t vid, std::uint16_t pid) {
    if (vid != kLewittVendorId) return false;
    return pid == 0x0004 || pid == 0x0002;   // APP modes the vendor application also accepts
}
}  // namespace

Result<int> selectControlInterface(std::uint16_t vid, std::uint16_t pid,
                                   std::span<const InterfaceDescriptor> interfaces) {
    char id[32];
    std::snprintf(id, sizeof id, "%04x:%04x", vid, pid);
    if (!isSupportedDevice(vid, pid)) {
        if (vid == kLewittVendorId && pid == 0x000A)
            return makeError(Errc::NotSupported,
                             std::string(id) + " is probably a bootloader/firmware-update mode; refusing to open it");
        return makeError(Errc::NotSupported,
                         std::string(id) + " is not a supported CONNECT 2 (expected 29c2:0004 or 29c2:0002)");
    }
    std::vector<const InterfaceDescriptor*> candidates;
    std::string rejected;
    for (const auto& d : interfaces) {
        std::string why;
        if (isProhibitedInterface(d)) why = "firmware-update (DFU) interface, never used";
        else if (d.klass == kUsbClassAudio) why = "audio interface, kernel-owned and not the control channel";
        else if (d.klass != kUsbClassVendorSpecific) why = "not vendor-specific";
        else if (d.subclass != kControlInterfaceSubclass) why = "vendor-specific but not the documented control subclass 0xF0";
        else if (d.numEndpoints != 0) why = "has endpoints; the control interface is control-transfer only";
        if (!why.empty()) {
            rejected += "\n  - " + describe(d) + ": " + why;
            continue;
        }
        candidates.push_back(&d);
    }
    if (candidates.empty())
        return makeError(Errc::NotFound,
                         "no safe control interface (vendor-specific ff/f0, no endpoints) on " + std::string(id) +
                             (rejected.empty() ? " (device exposes no interfaces)" : "; interfaces seen:" + rejected));
    if (candidates.size() > 1) {
        std::string list;
        for (auto* c : candidates) list += "\n  - " + describe(*c);
        return makeError(Errc::Malformed, "ambiguous: more than one interface matches the control signature on " +
                                              std::string(id) + "; refusing to guess:" + list);
    }
    const InterfaceDescriptor& c = *candidates.front();
    if (!c.driverKnown)
        return makeError(Errc::Busy, "cannot determine whether " + describe(c) + " is owned by a kernel driver; "
                                     "refusing to use it");
    if (c.driverBound)
        return makeError(Errc::Busy, describe(c) + ": claimed by a kernel driver; this application never detaches drivers");
    return c.number;
}

Status checkExplicitInterface(int number, std::span<const InterfaceDescriptor> interfaces) {
    for (const auto& d : interfaces)
        if (d.number == number && isProhibitedInterface(d))
            return makeError(Errc::NotSupported, "interface " + std::to_string(number) +
                                                     " is the firmware-update (DFU) interface; refusing to use it");
    return okStatus();
}

}  // namespace lewitt
