#include "DeviceManager.hpp"

#include <algorithm>

namespace lewitt {

namespace {
bool sameDevice(const DeviceCandidate& a, const DeviceCandidate& b) {
    return a.usb.bus == b.usb.bus && a.usb.address == b.usb.address && a.usb.pid == b.usb.pid &&
           a.usb.sysfsPath == b.usb.sysfsPath;
}
}  // namespace

DeviceManager::DeviceManager(Scanner s) : scanner_(std::move(s)) {
    if (!scanner_) scanner_ = [] { return scanSysfs(kLewittVendorId); };
}

DeviceCandidate DeviceManager::classify(const UsbDeviceReport& r) {
    DeviceCandidate c;
    c.usb = r;
    c.mode = deviceModeFromPid(r.pid);
    c.isConnect2 = r.vid == kLewittVendorId &&
                   std::find(kConnect2ProductIds.begin(), kConnect2ProductIds.end(), r.pid) != kConnect2ProductIds.end();
    c.appMode = c.mode != DeviceMode::Unknown;
    return c;
}

std::vector<DeviceCandidate> DeviceManager::scan() {
    std::vector<DeviceCandidate> out;
    for (const auto& r : scanner_()) out.push_back(classify(r));
    return out;
}

std::vector<DeviceManager::Change> DeviceManager::poll() {
    auto now = scan();
    std::vector<Change> ch;
    for (const auto& n : now)
        if (std::none_of(last_.begin(), last_.end(), [&](const auto& o) { return sameDevice(o, n); }))
            ch.push_back({Change::Kind::Added, n});
    for (const auto& o : last_)
        if (std::none_of(now.begin(), now.end(), [&](const auto& n) { return sameDevice(o, n); }))
            ch.push_back({Change::Kind::Removed, o});
    last_ = std::move(now);
    return ch;
}

}  // namespace lewitt
