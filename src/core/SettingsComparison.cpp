#include "core/SettingsComparison.hpp"

#include <algorithm>
#include <utility>

namespace lewitt {

SettingsComparison compareSettings(const SettingsMessage& first, const SettingsMessage& second,
                                   const std::vector<std::size_t>& expected) {
    SettingsComparison c;
    for (std::size_t i = 0; i < SettingsMessage::kSize; ++i) {
        if (first.byteAt(i) == second.byteAt(i)) continue;
        c.changed.push_back(i);
        c.values.emplace_back(first.byteAt(i), second.byteAt(i));
        if (std::find(expected.begin(), expected.end(), i) == expected.end()) c.unexpected.push_back(i);
    }
    c.ledBrightnessFirst = first.ledBrightness();
    c.ledBrightnessSecond = second.ledBrightness();
    return c;
}

std::string settingsFieldName(std::size_t off) {
    using namespace settings_off;
    static const std::pair<std::size_t, const char*> fields[] = {
        {kGain[0], "mic gain"}, {kGain[0] + 1, "mic gain"}, {kMute[0], "mic mute"}, {kHighPass[0], "mic low-cut"},
        {kClipguard[0], "mic Clipguard"}, {kPhantom1, "48 V"}, {kPreamp[0], "mic preamp"}, {kDenoise[0], "mic Denoise"},
        {kCompress[0], "mic Comp"}, {kGain[1], "instrument gain"}, {kGain[1] + 1, "instrument gain"},
        {kMute[1], "instrument mute"}, {kHighPass[1], "instrument low-cut"}, {kClipguard[1], "instrument Clipguard"},
        {kPreamp[1], "instrument preamp"}, {kDenoise[1], "instrument Denoise"}, {kCompress[1], "instrument Comp"},
        {kLoopbackMute, "loopback mute"}, {kSpeakerVolume, "speaker volume"}, {kSpeakerVolume + 1, "speaker volume"},
        {kSpeakerMute, "speaker mute"}, {kMixToSpeakers, "mix to speakers"}, {kHighVolumeSpeaker, "high-volume speaker"},
        {kHeadphoneVolume, "headphone volume"}, {kHeadphoneVolume + 1, "headphone volume"},
        {kHeadphoneMute, "headphone mute"}, {kHighImpedanceHeadphone, "high-impedance headphone"}, {kDim, "dim"}, {kSpeakerDimState, "speaker dim state (set by the device)"},
        {kMono, "mono"}, {kMonitorSource, "monitoring source"}, {kInputPlaybackMix, "monitoring mix"},
        {kInputPlaybackMix + 1, "monitoring mix"}, {kLedBrightness, "LED brightness"},
        {kCustomButton, "custom button"}, {kSelectedChannel, "selected channel"},
        {kExternalGainLock, "gain lock"}, {kNativeVolumeMapping, "volume mapping"},
        {kMobileStreaming, "mobile streaming"}};
    for (const auto& [o, n] : fields)
        if (o == off) return n;
    if (off >= kLedColorIcons && off < kLedColorIcons + 15) return "LED colour";
    return "offset " + std::to_string(off);
}

namespace {
std::string list(const std::vector<std::size_t>& offs) {
    if (offs.empty()) return "none";
    std::string s;
    for (std::size_t i = 0; i < offs.size(); ++i) {
        if (i) s += ", ";
        s += std::to_string(offs[i]) + " (" + settingsFieldName(offs[i]) + ")";
    }
    return s;
}
}  // namespace

std::string describeComparison(const SettingsComparison& c) {
    std::string s = "LED brightness " + std::to_string(c.ledBrightnessFirst) + " -> " + std::to_string(c.ledBrightnessSecond) +
                    "; changed: " + list(c.changed) + "; unexpected: " + list(c.unexpected);
    if (!c.changed.empty()) {
        s += "; values:";
        for (std::size_t i = 0; i < c.changed.size() && i < c.values.size(); ++i)
            s += " " + std::to_string(c.changed[i]) + " " + std::to_string(c.values[i].first) + "->" +
                 std::to_string(c.values[i].second);
    }
    return s;
}

}  // namespace lewitt
