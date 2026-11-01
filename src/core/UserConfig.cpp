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
    if (key == "follow_active_output") return c.followActiveOutput;
    return c.vendorFallback;   // "vendor_fallback"
}
int parseBool(const std::string& v) {     // 1 / 0 / -1 (invalid)
    if (v == "true" || v == "yes" || v == "on" || v == "1") return 1;
    if (v == "false" || v == "no" || v == "off" || v == "0") return 0;
    return -1;
}
}  // namespace

bool isObsoletePermissionKey(const std::string& key) {
    static const char* const keys[] = {"speaker_control", "headphone_control", "led_brightness_control",
                                       "input_control", "processing_control", "monitoring_control",
                                       "appearance_control", "phantom_control", "output_modes_control",
                                       "measurement_control", "routing_control", "output_control"};
    for (const char* k : keys)
        if (key == k) return true;
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
        if (isObsoletePermissionKey(key)) continue;   // evaluation-build permission: no longer needed
        const bool known = key == "desktop_volume_sync" || key == "vendor_fallback" || key == "follow_active_output" ||
                           key == "follow_both_on" || key == "follow_both_muted" || key == "follow_move_focus" ||
                           key == "name_channels" || key == "button_follow";
        if (!known) {
            r.warnings.push_back("unknown key '" + key + "' ignored");
            continue;
        }
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
            if (std::find(duplicated.begin(), duplicated.end(), key) == duplicated.end()) duplicated.push_back(key);
        } else {
            seen.push_back(key);
        }
        if (key == "button_follow") {
            r.config.buttonFollow.clear();
            std::string item;
            std::istringstream items(value);
            while (std::getline(items, item, ',')) {
                item = trim(item);
                if (item.empty()) continue;
                const auto c = item.find(':');
                const std::string k = c == std::string::npos ? "" : item.substr(0, c);
                const std::string b = c == std::string::npos ? "" : item.substr(c + 1);
                const bool keyOk = k.size() == 16 && k.find_first_not_of("0123456789abcdef") == std::string::npos;
                if (keyOk && (b == "1" || b == "2")) r.config.buttonFollow.push_back(k + ":" + b);
                else r.warnings.push_back("button_follow: '" + item + "' is not <16 hex digits>:<1|2>, ignored");
            }
        } else if (key == "follow_move_focus" || key == "name_channels") {
            const int b = parseBool(value);
            if (b < 0) r.warnings.push_back(key + ": '" + value + "' is not true|false, using true");
            (key == "name_channels" ? r.config.nameChannels : r.config.followMoveFocus) = b != 0;
        } else if (key == "follow_both_on" || key == "follow_both_muted") {
            const bool on = key == "follow_both_on";
            FollowPref& f = on ? r.config.followBothOn : r.config.followBothMuted;
            const FollowPref def = on ? FollowPref::Speakers : FollowPref::Keep;
            if (value == "speakers") f = FollowPref::Speakers;
            else if (value == "headphones") f = FollowPref::Headphones;
            else if (value == "keep") f = FollowPref::Keep;
            else if (value == "disable" && !on) f = FollowPref::Disable;
            else {
                f = def;
                r.warnings.push_back(key + ": '" + value + "' is not " +
                                     (on ? "speakers|headphones|keep" : "keep|speakers|headphones|disable") + ", using " +
                                     (on ? "speakers" : "keep"));
            }
        } else if (key == "desktop_volume_sync") {
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
        else if (key == "follow_both_on") r.config.followBothOn = FollowPref::Speakers;
        else if (key == "follow_both_muted") r.config.followBothMuted = FollowPref::Keep;
        else if (key == "follow_move_focus") r.config.followMoveFocus = true;
        else if (key == "name_channels") r.config.nameChannels = true;
        else if (key == "button_follow") r.config.buttonFollow.clear();
        else flag(r.config, key) = false;
    }
    return r;
}

std::string serializeUserConfig(const UserConfig& c) {
    std::ostringstream o;
    o << "# OpenWitty settings. Edited by OpenWitty's Settings; delete this file to reset.\n"
      << "[openwitty]\n"
      << "# off | mirror: show the CONNECT 2's hardware speaker knob and Mute button in the desktop volume.\n"
      << "desktop_volume_sync = " << (c.desktopVolumeSync == DesktopVolumeSync::Mirror ? "mirror" : "off") << "\n"
      << "# Experimental (debug mode): apply a desktop volume change the device did not follow (vendor protocol).\n"
      << "vendor_fallback = " << (c.vendorFallback ? "true" : "false") << "\n"
      << "# Keep the system volume keys on the audible output (switches the CONNECT 2's volume-key mapping).\n"
      << "follow_active_output = " << (c.followActiveOutput ? "true" : "false") << "\n";
    auto pref = [](FollowPref f) {
        return f == FollowPref::Speakers ? "speakers" : f == FollowPref::Headphones ? "headphones"
             : f == FollowPref::Keep ? "keep" : "disable";
    };
    o << "# With follow_active_output: which output the system volume controls when both are unmuted\n"
      << "# (speakers | headphones | keep) and when both are muted (keep | speakers | headphones | disable).\n"
      << "follow_both_on = " << pref(c.followBothOn) << "\n"
      << "follow_both_muted = " << pref(c.followBothMuted) << "\n"
      << "# With follow_active_output: also move the touch-wheel focus to that output.\n"
      << "follow_move_focus = " << (c.followMoveFocus ? "true" : "false") << "\n"
      << "# Readable channel names for the CONNECT 2 in audio apps (1 Microphone ... ; display only).\n"
      << "name_channels = " << (c.nameChannels ? "true" : "false") << "\n";
    if (!c.buttonFollow.empty()) {
        o << "# Custom button \"switch outputs + follow volume\" per device (key = hash of the serial number).\n"
          << "button_follow = ";
        for (std::size_t i = 0; i < c.buttonFollow.size(); ++i) o << (i ? ", " : "") << c.buttonFollow[i];
        o << "\n";
    }
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
