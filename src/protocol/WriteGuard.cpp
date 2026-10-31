#include "WriteGuard.hpp"

namespace lewitt {

Status checkWriteAllowed(MessageId id, std::span<const std::uint8_t> data, const WriteGuardContext& ctx) {
    const MessageInfo* mi = messageInfo(id);
    if (!mi) return makeError(Errc::InvalidArgument, "unknown message id");
    if (!mi->writable) return makeError(Errc::NotSupported, std::string(mi->name) + " is read-only");
    if (data.size() != mi->size)
        return makeError(Errc::InvalidArgument, std::string(mi->name) + ": wrong size " +
                                                   std::to_string(data.size()));
    if (id == MessageId::Triggers) {
        auto w = static_cast<Trigger>(triggerWordOf(data));
        switch (w) {
            case Trigger::FactoryReset:
                return makeError(Errc::NotSupported, "factory reset is never sent by this build");
            case Trigger::SwitchToNormalMode:
            case Trigger::SwitchToMacOSStreamingMode:
                if (!ctx.simulated)
                    return makeError(Errc::NotSupported,
                                     "device mode switching restarts the device; refused on real hardware");
                break;
            case Trigger::ResetLedColors:
                if (!ctx.simulated && !ctx.allowPersistentChanges)
                    return makeError(Errc::WritesDisabled, "persistent LED reset not enabled");
                break;
            case Trigger::AutoGainMicrophone: case Trigger::AutoGainInstrument: case Trigger::AutoGainBoth:
            case Trigger::DenoiseMeasureMicrophone: case Trigger::DenoiseMeasureInstrument:
            case Trigger::DenoiseMeasureBoth: case Trigger::BeginMeasurementSession:
            case Trigger::EndMeasurementSession:
                break;
            default:
                return makeError(Errc::NotSupported, "unknown trigger word " + std::to_string(triggerWordOf(data)));
        }
        for (std::size_t i = 2; i < data.size(); ++i)
            if (data[i] != 0) return makeError(Errc::InvalidArgument, "trigger payload must be zero beyond word 0");
    }
    return okStatus();
}

bool isKnownWritableSettingsByte(std::size_t o) {
    using namespace settings_off;
    switch (o) {
        case 0: case 1: case 10: case 11:                        // input gains (Q8.8)
        case 20: case 21: case 27: case 28:                      // speaker / headphone volume
        case 35: case 36:                                        // input/playback mix
            return true;
        default: break;
    }
    if (o == kPhantom1 || o == kLoopbackMute || o == kSpeakerMute || o == kHeadphoneMute ||
        o == kMixToSpeakers || o == kHighVolumeSpeaker || o == kHighImpedanceHeadphone || o == kDim ||
        o == kMono || o == kMonitorSource || o == kLedBrightness || o == kCustomButton ||
        o == kExternalGainLock || o == kMobileStreaming || o == kNativeVolumeMapping)
        return true;
    for (int i = 0; i < 2; ++i)
        if (o == kMute[i] || o == kHighPass[i] || o == kClipguard[i] || o == kPreamp[i] ||
            o == kDenoise[i] || o == kCompress[i])
            return true;
    return o >= kLedColorIcons && o < kLedColorIcons + 15;       // five RGB triples
}

Status verifyOnlyKnownFieldsChanged(const SettingsMessage& before, const SettingsMessage& after) {
    auto a = before.bytes(), b = after.bytes();
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i] && !isKnownWritableSettingsByte(i))
            return makeError(Errc::InvalidArgument,
                             "edit would change unknown/reserved settings byte " + std::to_string(i));
    return okStatus();
}

}  // namespace lewitt
