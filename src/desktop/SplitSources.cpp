#include "desktop/SplitSources.hpp"

#include <regex>

namespace lewitt {

bool isConnect2CaptureNodeName(const std::string& name) {
    static const std::regex re("alsa_input\\.usb-Lewitt_GmbH_CONNECT_2[A-Za-z0-9._:-]*");
    return name.size() < 200 && std::regex_match(name, re);
}

namespace {
constexpr const char* kMarker = "# openwitty-split-target: ";

std::string source(const std::string& target, const char* id, const char* description, const char* capturePos,
                   const char* playbackPos, const char* extraPlaybackProps = "") {
    return std::string("  { name = libpipewire-module-loopback\n"
                       "    args = {\n"
                       "      node.description = \"") + description + "\"\n"
           "      capture.props = {\n"
           "        node.name = \"openwitty.connect2." + id + ".capture\"\n"
           "        audio.position = [ " + capturePos + " ]\n"
           "        stream.dont-remix = true\n"
           "        target.object = \"" + target + "\"\n"
           "        node.passive = true\n"
           "      }\n"
           "      playback.props = {\n"
           "        node.name = \"openwitty_connect2_" + id + "\"\n"
           "        media.class = \"Audio/Source\"\n"
           "        audio.position = [ " + playbackPos + " ]\n" + extraPlaybackProps +
           "      }\n"
           "    }\n"
           "  }\n";
}
}  // namespace

std::optional<std::string> renderSplitSourcesConfig(const std::string& captureNodeName) {
    if (!isConnect2CaptureNodeName(captureNodeName)) return std::nullopt;
    std::string s;
    s += "# OpenWitty (unofficial): separate desktop inputs for the LEWITT CONNECT 2. Written by OpenWitty\n";
    s += "# (Settings > Desktop integration); remove it there or delete this file, then restart PipeWire.\n";
    s += "# Channel order verified on hardware: 1 microphone, 2 instrument, 3/4 loopback L/R.\n";
    s += "# Each source is active only while an application records from it (node.passive).\n";
    s += "# The microphone is preferred as the default input (priority.session).\n";
    s += std::string(kMarker) + captureNodeName + "\n";
    s += "context.modules = [\n";
    // The microphone is the preferred default input (higher than the 4-channel device's 2109): an app that records
    // "the default" gets the mic only, never mic + instrument + computer audio mixed together.
    s += source(captureNodeName, "microphone", "CONNECT 2 Microphone", "FL", "MONO", "        priority.session = 2200\n");
    s += source(captureNodeName, "instrument", "CONNECT 2 Instrument", "FR", "MONO");
    s += source(captureNodeName, "loopback", "CONNECT 2 Loopback (computer audio)", "RL RR", "FL FR");
    s += "]\n";
    return s;
}

std::optional<std::string> splitSourcesTarget(const std::string& text) {
    const auto p = text.find(kMarker);
    if (p == std::string::npos) return std::nullopt;
    const auto b = p + std::string(kMarker).size();
    const auto e = text.find('\n', b);
    std::string name = text.substr(b, e == std::string::npos ? std::string::npos : e - b);
    if (!isConnect2CaptureNodeName(name)) return std::nullopt;
    return name;
}

std::string splitSourcesPath(const std::string& configHome) {
    if (configHome.empty()) return {};
    return configHome + "/pipewire/pipewire.conf.d/" + kSplitSourcesFile;
}

}  // namespace lewitt
