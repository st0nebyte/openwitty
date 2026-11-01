#include "core/OutputControl.hpp"

#include <algorithm>
#include <cmath>

#include "core/UserConfig.hpp"

namespace lewitt {

OutputLevel outputLevel(const SettingsMessage& s, Output o) {
    if (o == Output::Speaker) return {s.speakerVolumeDb(), s.speakerMute()};
    return {s.headphoneVolumeDb(), s.headphoneMute()};
}

std::vector<std::size_t> outputWriteOffsets(Output o) {
    using namespace settings_off;
    if (o == Output::Speaker) return {kSpeakerVolume, kSpeakerVolume + 1, kSpeakerMute};
    return {kHeadphoneVolume, kHeadphoneVolume + 1, kHeadphoneMute};
}

WritePolicy outputWritePolicy(bool speaker, bool headphone) {
    WritePolicy p;
    p.allowRoutingChanges = false;
    p.allowPersistentChanges = false;
    p.allowHardwareWrites = speaker || headphone;
    if (speaker)
        for (auto o : outputWriteOffsets(Output::Speaker)) p.hardwareWriteOffsets.push_back(o);
    if (headphone)
        for (auto o : outputWriteOffsets(Output::Headphone)) p.hardwareWriteOffsets.push_back(o);
    return p;
}

WritePolicy withFollowOutput(WritePolicy base) {
    base.allowHardwareWrites = true;
    base.hardwareWriteOffsets.push_back(settings_off::kNativeVolumeMapping);
    base.hardwareWriteOffsets.push_back(settings_off::kSelectedChannel);   // touch focus follows (owner wish)
    return base;
}

WritePolicy guiWritePolicy(bool speaker, bool headphone, bool ledBrightness) {
    UserConfig c;
    c.speakerControl = speaker;
    c.headphoneControl = headphone;
    c.ledBrightnessControl = ledBrightness;
    return guiWritePolicy(c);
}

WritePolicy serviceClientPolicy(const UserConfig& c) {
    WritePolicy p = guiWritePolicy(c);
    if (c.vendorFallback) {
        p.allowHardwareWrites = true;
        for (auto off : outputWriteOffsets(Output::Speaker)) p.hardwareWriteOffsets.push_back(off);
    }
    return p;
}

std::vector<std::size_t> permissionOffsets(const std::string& name) {
    using namespace settings_off;
    if (name == "speaker") return outputWriteOffsets(Output::Speaker);
    if (name == "headphone") return outputWriteOffsets(Output::Headphone);
    if (name == "ledBrightness") return {kLedBrightness};
    if (name == "inputs") return {kGain[0], kGain[0] + 1, kMute[0], kGain[1], kGain[1] + 1, kMute[1], kExternalGainLock};
    if (name == "processing") {
        std::vector<std::size_t> v;
        for (int i = 0; i < 2; ++i)
            for (std::size_t o : {kHighPass[i], kClipguard[i], kPreamp[i], kDenoise[i], kCompress[i]}) v.push_back(o);
        return v;
    }
    if (name == "monitoring") return {kLoopbackMute, kDim, kMono, kMonitorSource, kInputPlaybackMix, kInputPlaybackMix + 1};
    if (name == "appearance") {
        std::vector<std::size_t> v;
        for (std::size_t o = kLedColorIcons; o < kLedColorIcons + 15; ++o) v.push_back(o);
        v.push_back(kCustomButton);
        return v;
    }
    if (name == "phantom") return {kPhantom1};
    if (name == "outputModes") return {kHighVolumeSpeaker, kHighImpedanceHeadphone};
    if (name == "routing") return {kMixToSpeakers, kMobileStreaming, kNativeVolumeMapping};
    if (name == "followOutput") return {kNativeVolumeMapping, kSelectedChannel};
    return {};
}

std::vector<Trigger> permissionTriggers(const std::string& name) {
    if (name == "measurements")
        return {Trigger::BeginMeasurementSession, Trigger::AutoGainMicrophone, Trigger::AutoGainInstrument,
                Trigger::AutoGainBoth, Trigger::DenoiseMeasureMicrophone, Trigger::DenoiseMeasureInstrument,
                Trigger::EndMeasurementSession};
    return {};
}

WritePolicy guiWritePolicy(const UserConfig& c) {
    WritePolicy p;
    p.allowRoutingChanges = false;
    p.allowPersistentChanges = false;
    for (const auto& perm : devicePermissions()) {
        if (!(c.*(perm.field))) continue;
        p.allowHardwareWrites = true;
        for (auto o : permissionOffsets(perm.name)) p.hardwareWriteOffsets.push_back(o);
        for (auto t : permissionTriggers(perm.name)) p.hardwareTriggers.push_back(t);
    }
    // Mobile streaming changes what capture channels 1/2 carry (the device's routing gate in DeviceSession).
    p.allowRoutingChanges = c.routingControl;
    // Phase 12D: an extended custom-button mode chosen in the GUI authorizes the follow writes (59, 56) as well.
    if (!c.buttonFollow.empty() && !c.followActiveOutput) p = withFollowOutput(p);
    return p;
}

std::optional<double> normalizeOutputDb(double db) {
    if (!std::isfinite(db)) return std::nullopt;
    db = std::clamp(db, kVolumeMinDb, kVolumeMaxDb);
    return std::round(db * 256.0) / 256.0;
}

double limitIncrease(double currentDb, double requestedDb, double maxIncreaseDb) {
    if (!std::isfinite(maxIncreaseDb) || maxIncreaseDb < 0.0) maxIncreaseDb = 0.0;
    if (!std::isfinite(currentDb) || !std::isfinite(requestedDb)) return currentDb;
    if (requestedDb <= currentDb) return requestedDb;
    return std::min(requestedDb, currentDb + maxIncreaseDb);
}

Status stageOutput(SettingsMessage& m, Output o, std::optional<double> db, std::optional<bool> mute) {
    if (db && (!std::isfinite(*db) || *db < kVolumeMinDb || *db > kVolumeMaxDb))
        return makeError(Errc::InvalidArgument, "output volume must be a finite value within -60..0 dB");
    SettingsMessage copy = m;
    if (db) {
        auto s = o == Output::Speaker ? copy.setSpeakerVolumeDb(*db) : copy.setHeadphoneVolumeDb(*db);
        if (!s) return s;
    }
    if (mute) {
        auto s = o == Output::Speaker ? copy.setSpeakerMute(*mute) : copy.setHeadphoneMute(*mute);
        if (!s) return s;
    }
    m = copy;
    return okStatus();
}

}  // namespace lewitt
