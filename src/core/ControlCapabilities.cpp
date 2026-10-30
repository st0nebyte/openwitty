#include "core/ControlCapabilities.hpp"

#include <algorithm>
#include <map>

namespace lewitt {

namespace {
namespace o = settings_off;
std::vector<std::size_t> two(std::size_t first) { return {first, first + 1}; }   // i16 Q8.8 fields
std::vector<std::size_t> rgb(std::size_t first) { return {first, first + 1, first + 2}; }
std::vector<Trigger> measure(Trigger t) {
    return {Trigger::BeginMeasurementSession, t, Trigger::EndMeasurementSession};
}

const std::map<std::string, ControlFootprint, std::less<>>& table() {
    static const std::map<std::string, ControlFootprint, std::less<>> t = [] {
        std::map<std::string, ControlFootprint, std::less<>> m;
        for (int i = 0; i < 2; ++i) {
            const std::string p = i == 0 ? "in1." : "in2.";
            m[p + "gain"] = {two(o::kGain[i]), {}};
            m[p + "mute"] = {{o::kMute[i]}, {}};
            m[p + "highPass"] = {{o::kHighPass[i]}, {}};
            m[p + "clipguard"] = {{o::kClipguard[i]}, {}};
            m[p + "preamp"] = {{o::kPreamp[i]}, {}};
            m[p + "denoise"] = {{o::kDenoise[i]}, {}};
            m[p + "compress"] = {{o::kCompress[i]}, {}};
        }
        m["in1.phantom"] = {{o::kPhantom1}, {}};
        m["loop.mute"] = {{o::kLoopbackMute}, {}};
        m["spk.volume"] = {two(o::kSpeakerVolume), {}};
        m["spk.mute"] = {{o::kSpeakerMute}, {}};
        m["hp.volume"] = {two(o::kHeadphoneVolume), {}};
        m["hp.mute"] = {{o::kHeadphoneMute}, {}};
        m["mon.mix"] = {two(o::kInputPlaybackMix), {}};
        m["mon.mic"] = {{o::kMonitorSource}, {}};
        m["mon.inst"] = {{o::kMonitorSource}, {}};
        m["out.dim"] = {{o::kDim}, {}};
        m["out.mono"] = {{o::kMono}, {}};
        m["set.gainLock"] = {{o::kExternalGainLock}, {}};
        m["set.mixToSpeakers"] = {{o::kMixToSpeakers}, {}};
        m["set.mobileStreaming"] = {{o::kMobileStreaming}, {}};
        m["set.highImpedance"] = {{o::kHighImpedanceHeadphone}, {}};
        m["set.highVolume"] = {{o::kHighVolumeSpeaker}, {}};
        m["set.ledBrightness"] = {{o::kLedBrightness}, {}};
        m["set.customButton"] = {{o::kCustomButton}, {}};
        m["set.led.icons"] = {rgb(o::kLedColorIcons), {}};
        m["set.led.mute"] = {rgb(o::kLedColorMute), {}};
        m["set.led.parameter"] = {rgb(o::kLedColorParameter), {}};
        m["set.led.metering"] = {rgb(o::kLedColorMetering), {}};
        m["set.led.gainReduction"] = {rgb(o::kLedColorGainReduction), {}};
        m["set.macosStreaming"] = {{}, {Trigger::SwitchToMacOSStreamingMode, Trigger::SwitchToNormalMode}, true};
        m["autogain1"] = {{}, measure(Trigger::AutoGainMicrophone)};
        m["autogain2"] = {{}, measure(Trigger::AutoGainInstrument)};
        m["autogainBoth"] = {{}, measure(Trigger::AutoGainBoth)};
        m["denoise1"] = {{}, measure(Trigger::DenoiseMeasureMicrophone)};
        m["denoise2"] = {{}, measure(Trigger::DenoiseMeasureInstrument)};
        // The Autosetup wizard writes preamp, Clipguard, Denoise (both inputs), 48 V and runs an autogain.
        ControlFootprint a;
        for (const char* k : {"in1.preamp", "in2.preamp", "in1.clipguard", "in2.clipguard", "in1.denoise",
                              "in2.denoise", "in1.phantom"})
            for (auto off : m[k].settingsOffsets) a.settingsOffsets.push_back(off);
        a.triggers = measure(Trigger::AutoGainBoth);
        for (Trigger t : {Trigger::AutoGainMicrophone, Trigger::AutoGainInstrument}) a.triggers.push_back(t);
        m["autosetup"] = a;
        return m;
    }();
    return t;
}
}  // namespace

const ControlFootprint* controlFootprint(std::string_view key) {
    auto it = table().find(key);
    return it == table().end() ? nullptr : &it->second;
}

const std::vector<std::string>& controlKeys() {
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> k;
        for (const auto& [name, _] : table()) k.push_back(name);
        return k;
    }();
    return keys;
}

bool controlWritable(std::string_view key, const WritePolicy& policy, bool simulated) {
    const ControlFootprint* f = controlFootprint(key);
    if (!f) return false;
    if (simulated) return true;
    if (f->simulationOnly || !policy.allowHardwareWrites) return false;
    auto has = [](const auto& list, auto v) { return std::find(list.begin(), list.end(), v) != list.end(); };
    for (auto off : f->settingsOffsets)
        if (!has(policy.hardwareWriteOffsets, off)) return false;
    for (Trigger t : f->triggers)
        if (!has(policy.hardwareTriggers, t)) return false;
    return !f->settingsOffsets.empty() || !f->triggers.empty();
}

}  // namespace lewitt
