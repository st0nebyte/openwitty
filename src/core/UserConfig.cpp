#include "core/UserConfig.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace lewitt {

namespace {
std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}
bool& flag(UserConfig& c, const std::string& key) {
    if (key == "vendor_fallback") return c.vendorFallback;
    for (const auto& p : devicePermissions())
        if (key == p.key) return c.*(p.field);
    return c.legacyOutputControlPending;   // "output_control" (legacy)
}
bool isPermissionKey(const std::string& key) {
    for (const auto& p : devicePermissions())
        if (key == p.key) return true;
    return false;
}
int parseBool(const std::string& v) {     // 1 / 0 / -1 (invalid)
    if (v == "true" || v == "yes" || v == "on" || v == "1") return 1;
    if (v == "false" || v == "no" || v == "off" || v == "0") return 0;
    return -1;
}
}  // namespace

const std::vector<DevicePermission>& devicePermissions() {
    static const std::vector<DevicePermission> t = {
        {"speaker", "speaker_control", &UserConfig::speakerControl,
         "OpenWitty's speaker fader and MUTE change the speaker volume and mute (settings bytes 20-22)."},
        {"headphone", "headphone_control", &UserConfig::headphoneControl,
         "OpenWitty's headphone fader and MUTE change the headphone volume and mute (bytes 27-29)."},
        {"ledBrightness", "led_brightness_control", &UserConfig::ledBrightnessControl,
         "OpenWitty's LED brightness slider changes the LED brightness (byte 37)."},
        {"inputs", "input_control", &UserConfig::inputControl,
         "microphone and instrument gain and mute, external gain lock (bytes 0-2, 10-12, 57)."},
        {"processing", "processing_control", &UserConfig::processingControl,
         "low-cut, Clipguard, preamp style, Denoise and Comp switches of both inputs (bytes 3-4, 6-7, 9, 13-16, 18)."},
        {"monitoring", "monitoring_control", &UserConfig::monitoringControl,
         "direct monitoring source, input/playback mix, Dim, Mono, loopback mute (bytes 19, 32-36)."},
        {"appearance", "appearance_control", &UserConfig::appearanceControl,
         "LED colours and the custom button function (bytes 38-53)."},
        {"phantom", "phantom_control", &UserConfig::phantomControl,
         "48 V phantom power for the microphone input (byte 5); each switch-on is confirmed again."},
        {"outputModes", "output_modes_control", &UserConfig::outputModesControl,
         "high-volume speaker mode and high-impedance headphone mode (bytes 24, 31); each switch-on is confirmed again."},
        {"measurements", "measurement_control", &UserConfig::measurementControl,
         "autogain and Denoise measurements (the device then changes the input gain or Denoise by itself)."},
        {"routing", "routing_control", &UserConfig::routingControl,
         "input/playback mix to the speakers and mobile streaming mode (bytes 23, 67); each switch-on is confirmed again."},
    };
    return t;
}

const DevicePermission* findDevicePermission(const std::string& name) {
    for (const auto& p : devicePermissions())
        if (name == p.name) return &p;
    return nullptr;
}

bool anyDeviceWritePermission(const UserConfig& c) {
    for (const auto& p : devicePermissions())
        if (c.*(p.field)) return true;
    return false;
}

ParsedUserConfig parseUserConfig(const std::string& text) {
    // Fail-safe rules: an invalid value sets its key to the safe default (even after a valid one), and a
    // key given more than once is ambiguous, so it also falls back to the safe default. Nothing that can
    // enable device writes is ever "repaired" into an enabled state.
    ParsedUserConfig r;
    std::istringstream in(text);
    std::string raw;
    int lineNo = 0;
    std::vector<std::string> seen;
    std::vector<std::string> duplicated;
    while (std::getline(in, raw)) {
        ++lineNo;
        std::string line = trim(raw.substr(0, raw.find('#')));
        if (line.empty() || line == "[openwitty]") continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            r.warnings.push_back("line " + std::to_string(lineNo) + ": expected 'key = value', ignored");
            continue;
        }
        const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        const bool known = key == "desktop_volume_sync" || key == "vendor_fallback" || key == "output_control" ||
                           isPermissionKey(key);
        if (!known) {
            r.warnings.push_back("unknown key '" + key + "' ignored");
            continue;
        }
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
            if (std::find(duplicated.begin(), duplicated.end(), key) == duplicated.end()) duplicated.push_back(key);
        } else {
            seen.push_back(key);
        }
        if (key == "desktop_volume_sync") {
            if (value == "mirror") r.config.desktopVolumeSync = DesktopVolumeSync::Mirror;
            else {
                r.config.desktopVolumeSync = DesktopVolumeSync::Off;
                if (value != "off") r.warnings.push_back("desktop_volume_sync: '" + value + "' is not off|mirror, using off");
            }
        } else {
            const int b = parseBool(value);
            if (b < 0) r.warnings.push_back(key + ": '" + value + "' is not true|false, using false");
            flag(r.config, key) = b == 1;
        }
    }
    for (const auto& key : duplicated) {
        r.warnings.push_back("'" + key + "' is set more than once: ambiguous, using the safe default");
        if (key == "desktop_volume_sync") r.config.desktopVolumeSync = DesktopVolumeSync::Off;
        else flag(r.config, key) = false;
    }
    // Legacy migration, fail-closed: the old combined output permission is never turned into the new ones. It
    // only asks for a reconfirmation - and only while none of the new output permissions has been given.
    if (r.config.legacyOutputControlPending && (r.config.speakerControl || r.config.headphoneControl))
        r.config.legacyOutputControlPending = false;
    if (r.config.legacyOutputControlPending)
        r.warnings.push_back("output_control (from an older OpenWitty) no longer grants anything: turn on the speaker "
                             "and/or headphone control again in Settings > Desktop integration if you want them");
    return r;
}

std::string serializeUserConfig(const UserConfig& c) {
    std::ostringstream o;
    o << "# OpenWitty settings. Edited by OpenWitty's Settings > Desktop integration; delete this file to reset.\n"
      << "[openwitty]\n"
      << "# off | mirror: show the CONNECT 2's hardware speaker knob and Mute button in the desktop volume.\n"
      << "desktop_volume_sync = " << (c.desktopVolumeSync == DesktopVolumeSync::Mirror ? "mirror" : "off") << "\n"
      << "# true writes the device: apply a desktop volume change the device did not follow (vendor protocol).\n"
      << "vendor_fallback = " << (c.vendorFallback ? "true" : "false") << "\n"
      << "# Each of the following writes the device, is confirmed on its own and changes only the bytes named.\n";
    for (const auto& p : devicePermissions())
        o << "# true: " << p.description << "\n" << p.key << " = " << (c.*(p.field) ? "true" : "false") << "\n";
    if (c.legacyOutputControlPending)
        o << "# Legacy setting from an older OpenWitty. It grants NOTHING; it is kept only so that Settings can ask\n"
          << "# you to reconfirm the speaker/headphone control above. Removed when you do so or reset.\n"
          << "output_control = true\n";
    return o.str();
}

std::string userConfigPath() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] == '/') return std::string(xdg) + "/openwitty/openwitty.conf";   // relative values are ignored
    const char* home = std::getenv("HOME");
    if (home && home[0] == '/') return std::string(home) + "/.config/openwitty/openwitty.conf";
    return {};
}

ParsedUserConfig loadUserConfig(const std::string& path) {
    ParsedUserConfig r;
    if (path.empty()) {
        r.warnings.push_back("no configuration directory (HOME/XDG_CONFIG_HOME unset): using defaults");
        return r;
    }
    std::ifstream f(path);
    if (!f) {
        if (errno != ENOENT) r.warnings.push_back("cannot read " + path + ": " + std::strerror(errno) + "; using defaults");
        return r;
    }
    std::stringstream b;
    b << f.rdbuf();
    return parseUserConfig(b.str());
}

Status saveUserConfig(const UserConfig& c, const std::string& path) {
    if (path.empty()) return makeError(Errc::InvalidArgument, "no configuration path");
    const auto slash = path.rfind('/');
    const std::string dir = path.substr(0, slash);
    // create parent directories as needed (only the last one is OpenWitty's own: 0700)
    for (std::size_t p = 1; p != std::string::npos; ) {
        p = dir.find('/', p + 1);
        const std::string part = dir.substr(0, p);
        if (mkdir(part.c_str(), p == std::string::npos ? 0700 : 0755) != 0 && errno != EEXIST)
            return makeError(Errc::Io, "create " + part + ": " + std::strerror(errno));
    }
    const std::string tmp = path + ".tmp." + std::to_string(getpid());
    const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return makeError(Errc::Io, "create " + tmp + ": " + std::strerror(errno));
    const std::string text = serializeUserConfig(c);
    const bool ok = write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size()) && fsync(fd) == 0;
    close(fd);
    if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
        const int e = errno;
        unlink(tmp.c_str());
        return makeError(Errc::Io, "save " + path + ": " + std::strerror(e));
    }
    return okStatus();
}

}  // namespace lewitt
