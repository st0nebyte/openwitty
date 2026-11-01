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

enum class FollowPref { Speakers, Headphones, Keep, Disable };

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
    // Phase 11A: further groups, each off by default and confirmed on its own (writes the device):
    bool inputControl{false};          // mic/instrument gain + mute, external gain lock: bytes 0-2, 10-12, 57
    bool processingControl{false};     // low-cut, Clipguard, preamp, Denoise flag, Comp: bytes 3,4,6,7,9,13-16,18
    bool monitoringControl{false};     // monitoring source, input/playback mix, Dim, Mono, loopback mute: 19,32-36
    bool appearanceControl{false};     // LED colours, custom button: bytes 38-52, 53
    bool phantomControl{false};        // 48 V phantom power (each switch-on is confirmed again): byte 5
    bool outputModesControl{false};    // high-volume speakers, high-impedance headphones (confirmed per use): 24, 31
    bool measurementControl{false};    // autogain measurements: measurement-session and autogain triggers
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
    bool routingControl{false};        // mix to speakers, mobile streaming, native volume mapping (confirmed): 23, 59, 67
    // Legacy (before Phase 10B): "output_control = true" authorized speaker AND headphone with one confirmation.
    // It grants NOTHING now; this only records that the user should be asked to reconfirm the new permissions.
    bool legacyOutputControlPending{false};

    bool operator==(const UserConfig&) const = default;
};

// The device-write permissions in one table: name (GUI/API), configuration key, field, one-line description.
struct DevicePermission {
    const char* name;
    const char* key;
    bool UserConfig::*field;
    const char* description;
};
const std::vector<DevicePermission>& devicePermissions();
const DevicePermission* findDevicePermission(const std::string& name);
bool anyDeviceWritePermission(const UserConfig& c);

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
