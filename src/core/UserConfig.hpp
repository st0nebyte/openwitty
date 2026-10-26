// The one persistent user configuration of OpenWitty, shared by the GUI and openwitty-volumed (Qt-free).
//
// Location (XDG Base Directory): $XDG_CONFIG_HOME/openwitty/openwitty.conf, or ~/.config/openwitty/openwitty.conf.
// Format: "key = value" lines, '#' comments, an optional [openwitty] header; unknown keys are kept as
// warnings and ignored. Every value that is missing or cannot be parsed falls back to its safe default:
// nothing writes the device, nothing synchronizes, until the user turns it on.
#pragma once
#include <string>
#include <vector>

#include "protocol/Types.hpp"

namespace lewitt {

enum class DesktopVolumeSync { Off, Mirror };

struct UserConfig {
    // Keep the desktop (KDE/PipeWire) volume of the CONNECT 2 in step with the hardware speaker knob and
    // Mute button. Never writes the device itself (the ALSA update it makes is forwarded by the kernel
    // with the level the device already has).
    DesktopVolumeSync desktopVolumeSync{DesktopVolumeSync::Off};
    // Only with desktopVolumeSync: if the device does not follow a desktop change, apply it through the
    // vendor settings message (writes the device). Requires the user's explicit confirmation in the GUI.
    bool vendorFallback{false};
    // Phase 10B: three independent GUI write permissions, each confirmed on its own (writes the device):
    bool speakerControl{false};        // speaker volume + mute: settings bytes 20-22
    bool headphoneControl{false};      // headphone volume + mute: settings bytes 27-29
    bool ledBrightnessControl{false};  // LED brightness: settings byte 37
    // Legacy (before Phase 10B): "output_control = true" authorized speaker AND headphone with one confirmation.
    // It grants NOTHING now; this only records that the user should be asked to reconfirm the new permissions.
    bool legacyOutputControlPending{false};

    bool operator==(const UserConfig&) const = default;
};

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
