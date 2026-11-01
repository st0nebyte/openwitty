// The one persistent user configuration of OpenWitty, shared by the GUI and openwitty-volumed (Qt-free).
//
// Location (XDG Base Directory): $XDG_CONFIG_HOME/openwitty/openwitty.conf, or ~/.config/openwitty/openwitty.conf.
// Format: "key = value" lines, '#' comments, an optional [openwitty] header; unknown keys are kept as
// warnings and ignored. Every value that is missing or cannot be parsed falls back to its safe default:
// nothing synchronizes, follows or falls back until the user turns it on.
#pragma once
#include <string>
#include <vector>

#include "protocol/Types.hpp"

namespace lewitt {

enum class DesktopVolumeSync { Off, Mirror };

enum class FollowPref { Speakers, Headphones, Keep, Disable };

struct UserConfig {
    // Keep the desktop (KDE/PipeWire) volume of the CONNECT 2 in step with the hardware speaker knob and
    // Mute button. Never writes the device itself (the ALSA update it makes is forwarded by the kernel
    // with the level the device already has).
    DesktopVolumeSync desktopVolumeSync{DesktopVolumeSync::Off};
    // Only with desktopVolumeSync: if the device does not follow a desktop change, apply it through the
    // vendor settings message (writes the device). Experimental: offered only in debug mode.
    bool vendorFallback{false};
    // Phase 12C: OpenWitty keeps the native volume mapping (byte 59) on the audible output (writes the device).
    bool followActiveOutput{false};
    FollowPref followBothOn{FollowPref::Speakers};    // both outputs unmuted: speakers | headphones | keep
    FollowPref followBothMuted{FollowPref::Keep};     // both muted: keep | speakers | headphones | disable
    bool followMoveFocus{true};                       // also move the touch-wheel focus (byte 56)
    bool nameChannels{true};                          // readable CONNECT 2 port names in audio apps (display only)
    // Phase 12D: extended custom-button modes per device: "<device key>:<native base function>" (1 or 2). The key is
    // a hash of the USB serial number (never the number itself). Choosing such a mode in the GUI authorizes the follow
    // writes (bytes 59, 56) for that device.
    std::vector<std::string> buttonFollow;

    bool operator==(const UserConfig&) const = default;
};

// Keys of the per-group write permissions of the evaluation builds (before 2026-11-01). They are ignored without a
// warning and dropped at the next save: the window controls every verified setting without switches.
bool isObsoletePermissionKey(const std::string& key);

struct ParsedUserConfig {
    UserConfig config;
    std::vector<std::string> warnings;   // unknown keys, invalid values (each fell back to the default)
};

ParsedUserConfig parseUserConfig(const std::string& text);
std::string serializeUserConfig(const UserConfig& c);

// $XDG_CONFIG_HOME/openwitty/openwitty.conf (absolute XDG_CONFIG_HOME only, per the spec) or
// $HOME/.config/openwitty/openwitty.conf. Empty if neither variable is usable.
std::string userConfigPath();

// Missing file = defaults, no warning. Unreadable file = defaults plus a warning.
ParsedUserConfig loadUserConfig(const std::string& path);
// Atomic replace (write to a temporary file in the same directory, fsync, rename); creates the
// directory (0700) if needed. The file is written 0600.
Status saveUserConfig(const UserConfig& c, const std::string& path);

}  // namespace lewitt
