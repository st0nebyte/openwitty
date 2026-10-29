// Phase 11A.4: optional separate desktop inputs for the CONNECT 2's four capture channels (verified order, LB-1/LB-2:
// 1 microphone, 2 instrument, 3/4 loopback L/R). PipeWire's loopback module creates three virtual sources from the
// device's 4-channel source; the file is written into the user's PipeWire config by OpenWitty (it names the device's
// node, which carries its serial number, so it is generated on the user's machine and never shipped).
#pragma once
#include <optional>
#include <string>

namespace lewitt {

// File name inside $XDG_CONFIG_HOME/pipewire/pipewire.conf.d/.
inline constexpr const char* kSplitSourcesFile = "53-openwitty-connect2-split.conf";

// A PipeWire node name OpenWitty accepts for the CONNECT 2's 4-channel capture node: ALSA input of the LEWITT
// CONNECT 2, only [A-Za-z0-9._:-] (it is written into a config file).
bool isConnect2CaptureNodeName(const std::string& name);

// Config text for the three virtual sources; nullopt if the node name is not acceptable.
// Assumes the capture positions FL FR RL RR (PipeWire's analog-surround-40 mapping, also used by OpenWitty's profile).
std::optional<std::string> renderSplitSourcesConfig(const std::string& captureNodeName);

// The node name a config written by renderSplitSourcesConfig() targets; nullopt if the text is not such a config.
std::optional<std::string> splitSourcesTarget(const std::string& configText);

// Path of the file below `configHome` (empty if configHome is empty).
std::string splitSourcesPath(const std::string& configHome);

}  // namespace lewitt
