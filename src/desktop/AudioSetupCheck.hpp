// Read-only diagnosis of the desktop audio setup for the CONNECT 2 (no USB I/O): is the per-device
// kernel compatibility quirk configured and active, does the ALSA mixer load, is the hardware-volume
// control there. Pure decision logic here; the I/O lives in collectAudioSetup().
#pragma once
#include <optional>
#include <string>
#include <vector>

namespace lewitt {

struct AudioSetupStatus {
    bool quirkConfigured{false};          // a modprobe.d file sets the quirk for 29c2:0004
    std::vector<std::string> quirkFiles;  // which files
    bool quirkActive{false};              // the loaded snd_usb_audio module carries it
    bool moduleLoaded{false};
    std::optional<int> card;              // ALSA card of 29c2:0004
    std::optional<int> mixerLoadError;    // snd_mixer_load() result when it failed (negative errno)
    bool volumeControl{false};            // "PCM Playback Volume" with a dB range
};

// True if one configuration line sets ignore_ctl_error for 29c2:0004 (comments ignored, case-insensitive).
bool modprobeTextConfiguresQuirk(const std::string& text);
// True if the module parameter value (as in /sys/module/snd_usb_audio/parameters/quirk_flags) carries it.
bool quirkFlagsParamActive(const std::string& value);
// Phase 11C: OpenWitty's software input mute/volume for the CONNECT 2 is installed in the user's config directory
// (`configHome` = $XDG_CONFIG_HOME or ~/.config): the WirePlumber rule plus the profile set and capture path it needs.
bool softwareInputMuteConfigured(const std::string& configHome);
// $XDG_CONFIG_HOME if absolute, else $HOME/.config; empty if neither is usable.
std::string userConfigHome();
// Plain-language findings, most important first. Empty = everything needed is in place.
std::vector<std::string> audioSetupFindings(const AudioSetupStatus& s);

#ifdef LEWITT_HAVE_ALSA
AudioSetupStatus collectAudioSetup();
#endif

}  // namespace lewitt
