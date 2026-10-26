#include "core/OutputControl.hpp"

#include <algorithm>
#include <cmath>

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

WritePolicy guiWritePolicy(bool speaker, bool headphone, bool ledBrightness) {
    WritePolicy p = outputWritePolicy(speaker, headphone);
    if (ledBrightness) {
        p.allowHardwareWrites = true;
        p.hardwareWriteOffsets.push_back(settings_off::kLedBrightness);
    }
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
