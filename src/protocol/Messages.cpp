#include "Messages.hpp"

#include <algorithm>
#include <cmath>

namespace lewitt {

// Boolean bytes: if the stored value already means the requested state, leave the raw
// byte alone (an unexpected non-0/1 value is never silently rewritten).
static void setBoolByte(std::uint8_t& b, bool v) {
    if ((b != 0) == v) return;
    b = v ? 1 : 0;
}

const MessageInfo* messageInfo(MessageId id) {
    for (const auto& m : kMessages)
        if (m.id == id) return &m;
    return nullptr;
}

namespace {
Status checkSize(std::span<const std::uint8_t> b, std::size_t want, const char* what) {
    if (b.size() == want) return okStatus();
    return makeError(b.size() < want ? Errc::ShortTransfer : Errc::Malformed,
                     std::string(what) + ": expected " + std::to_string(want) + " bytes, got " +
                         std::to_string(b.size()));
}
bool inputOk(int i) { return i == 0 || i == 1; }
Status badInput() { return makeError(Errc::InvalidArgument, "input index must be 0 or 1"); }
}  // namespace

Result<SettingsMessage> SettingsMessage::fromBytes(std::span<const std::uint8_t> b) {
    if (auto s = checkSize(b, kSize, "settings"); !s) return s.error();
    SettingsMessage m;
    std::copy(b.begin(), b.end(), m.raw_.begin());
    return m;
}

double SettingsMessage::gainDb(int i) const { return q88ToDouble(rdI16(raw_, settings_off::kGain[i & 1])); }
bool SettingsMessage::mute(int i) const { return raw_[settings_off::kMute[i & 1]] != 0; }
bool SettingsMessage::highPass(int i) const { return raw_[settings_off::kHighPass[i & 1]] != 0; }
bool SettingsMessage::clipguard(int i) const { return raw_[settings_off::kClipguard[i & 1]] != 0; }
bool SettingsMessage::phantom1() const { return raw_[settings_off::kPhantom1] != 0; }
std::uint8_t SettingsMessage::preampRaw(int i) const { return raw_[settings_off::kPreamp[i & 1]]; }
PreampType SettingsMessage::preamp(int i) const { return static_cast<PreampType>(preampRaw(i)); }
bool SettingsMessage::denoise(int i) const { return raw_[settings_off::kDenoise[i & 1]] != 0; }
bool SettingsMessage::compress(int i) const { return raw_[settings_off::kCompress[i & 1]] != 0; }
bool SettingsMessage::dim() const { return raw_[settings_off::kDim] != 0; }
bool SettingsMessage::mono() const { return raw_[settings_off::kMono] != 0; }
std::uint8_t SettingsMessage::customButton() const { return raw_[settings_off::kCustomButton]; }
std::optional<CustomButtonFunction> SettingsMessage::customButtonFunction() const {
    auto v = raw_[settings_off::kCustomButton];
    if (!isKnownCustomButton(v)) return std::nullopt;
    return static_cast<CustomButtonFunction>(v);
}
Status SettingsMessage::setMixToSpeakers(bool v) {
    setBoolByte(raw_[settings_off::kMixToSpeakers], v);
    return okStatus();
}
Status SettingsMessage::setCustomButtonFunction(CustomButtonFunction f) {
    if (!isKnownCustomButton(static_cast<std::uint8_t>(f)))
        return makeError(Errc::InvalidArgument, "unknown custom button function");
    raw_[settings_off::kCustomButton] = static_cast<std::uint8_t>(f);
    return okStatus();
}
void SettingsMessage::mergeExternalChanges(const SettingsMessage& snap, const SettingsMessage& dev) {
    for (const auto& f : kSettingsSchema) {
        bool changed = false;
        for (unsigned i = 0; i < f.length; ++i)
            changed = changed || snap.raw_[f.offset + i] != dev.raw_[f.offset + i];
        if (changed)
            for (unsigned i = 0; i < f.length; ++i) raw_[f.offset + i] = dev.raw_[f.offset + i];
    }
}
std::uint8_t SettingsMessage::nativeVolumeMapping() const { return raw_[settings_off::kNativeVolumeMapping]; }

Status SettingsMessage::setGainDb(int i, double db) {
    if (!inputOk(i)) return Status{badInput()};
    if (!std::isfinite(db) || db < kGainMinDb || db > gainLimitDb(i))
        return makeError(Errc::InvalidArgument, i == 0 ? "microphone gain must be within 0..72 dB"
                                                       : "instrument gain must be within 0..60 dB");
    wrI16(raw_, settings_off::kGain[i], doubleToQ88(db));
    return okStatus();
}
#define LW_BOOL_SETTER(Name, Off)                                  \
    Status SettingsMessage::Name(int i, bool v) {                  \
        if (!inputOk(i)) return Status{badInput()};                \
        setBoolByte(raw_[Off[i]], v);                              \
        return okStatus();                                         \
    }
LW_BOOL_SETTER(setMute, settings_off::kMute)
LW_BOOL_SETTER(setHighPass, settings_off::kHighPass)
LW_BOOL_SETTER(setClipguard, settings_off::kClipguard)
LW_BOOL_SETTER(setDenoise, settings_off::kDenoise)
LW_BOOL_SETTER(setCompress, settings_off::kCompress)
#undef LW_BOOL_SETTER

Status SettingsMessage::setPhantom1(bool v) {
    setBoolByte(raw_[settings_off::kPhantom1], v);
    return okStatus();
}
Status SettingsMessage::setPreamp(int i, PreampType t) {
    if (!inputOk(i)) return Status{badInput()};
    if (static_cast<std::uint8_t>(t) > 2) return makeError(Errc::InvalidArgument, "bad preamp type");
    raw_[settings_off::kPreamp[i]] = static_cast<std::uint8_t>(t);
    return okStatus();
}
static Status setVol(std::array<std::uint8_t, 0x80>& raw, std::size_t off, double db) {
    if (!std::isfinite(db) || db < kVolumeMinDb || db > kVolumeMaxDb)
        return makeError(Errc::InvalidArgument, "volume must be within -60..0 dB");
    wrI16(raw, off, doubleToQ88(db));
    return okStatus();
}
double SettingsMessage::speakerVolumeDb() const { return q88ToDouble(rdI16(raw_, settings_off::kSpeakerVolume)); }
double SettingsMessage::headphoneVolumeDb() const { return q88ToDouble(rdI16(raw_, settings_off::kHeadphoneVolume)); }
bool SettingsMessage::speakerMute() const { return raw_[settings_off::kSpeakerMute] != 0; }
bool SettingsMessage::headphoneMute() const { return raw_[settings_off::kHeadphoneMute] != 0; }
bool SettingsMessage::loopbackMute() const { return raw_[settings_off::kLoopbackMute] != 0; }
double SettingsMessage::inputPlaybackMix() const {
    return 0.0 - q88ToDouble(rdI16(raw_, settings_off::kInputPlaybackMix));
}
MonitorSource SettingsMessage::monitorSource() const {
    return static_cast<MonitorSource>(raw_[settings_off::kMonitorSource]);
}
bool SettingsMessage::monitorMicrophone() const {
    auto v = raw_[settings_off::kMonitorSource];
    return v == 1 || v == 2;
}
bool SettingsMessage::monitorInstrument() const {
    auto v = raw_[settings_off::kMonitorSource];
    return v == 0 || v == 2;
}
std::uint8_t SettingsMessage::mixToSpeakersRaw() const { return raw_[settings_off::kMixToSpeakers]; }
bool SettingsMessage::highVolumeSpeaker() const { return raw_[settings_off::kHighVolumeSpeaker] != 0; }
bool SettingsMessage::highImpedanceHeadphone() const { return raw_[settings_off::kHighImpedanceHeadphone] != 0; }
bool SettingsMessage::externalGainLock() const { return raw_[settings_off::kExternalGainLock] != 0; }
bool SettingsMessage::mobileStreaming() const { return raw_[settings_off::kMobileStreaming] != 0; }
std::uint8_t SettingsMessage::ledBrightness() const { return raw_[settings_off::kLedBrightness]; }
static std::size_t ledOff(LedElement e) {
    switch (e) {
        case LedElement::Icons: return settings_off::kLedColorIcons;
        case LedElement::Mute: return settings_off::kLedColorMute;
        case LedElement::Parameter: return settings_off::kLedColorParameter;
        case LedElement::Metering: return settings_off::kLedColorMetering;
        case LedElement::GainReduction: return settings_off::kLedColorGainReduction;
    }
    return settings_off::kLedColorIcons;
}
Rgb SettingsMessage::ledColor(LedElement e) const {
    auto o = ledOff(e);
    return Rgb{raw_[o], raw_[o + 1], raw_[o + 2]};
}
Status SettingsMessage::setSpeakerVolumeDb(double db) { return setVol(raw_, settings_off::kSpeakerVolume, db); }
Status SettingsMessage::setHeadphoneVolumeDb(double db) { return setVol(raw_, settings_off::kHeadphoneVolume, db); }
Status SettingsMessage::setSpeakerMute(bool v) { setBoolByte(raw_[settings_off::kSpeakerMute], v); return okStatus(); }
Status SettingsMessage::setHeadphoneMute(bool v) { setBoolByte(raw_[settings_off::kHeadphoneMute], v); return okStatus(); }
Status SettingsMessage::setLoopbackMute(bool v) { setBoolByte(raw_[settings_off::kLoopbackMute], v); return okStatus(); }
Status SettingsMessage::setDim(bool v) { setBoolByte(raw_[settings_off::kDim], v); return okStatus(); }
Status SettingsMessage::setMono(bool v) { setBoolByte(raw_[settings_off::kMono], v); return okStatus(); }
Status SettingsMessage::setInputPlaybackMix(double v) {
    if (!std::isfinite(v) || v < -1.0 || v > 1.0)
        return makeError(Errc::InvalidArgument, "mix must be within -1..+1 (range unverified)");
    wrI16(raw_, settings_off::kInputPlaybackMix, doubleToQ88(-v));
    return okStatus();
}
// Mirrors the app's pair tables: mic {(2,0),(1,0)}, instrument {(2,1),(0,1)}.
Status SettingsMessage::setMonitorMicrophone(bool on) {
    auto& b = raw_[settings_off::kMonitorSource];
    if (b > 2) return makeError(Errc::Malformed, "unknown monitor source value");
    if (on) { if (b == 0) b = 2; }          // instrument only -> both
    else    { if (b == 2) b = 0; }          // both -> instrument only (never "none")
    return okStatus();
}
Status SettingsMessage::setMonitorInstrument(bool on) {
    auto& b = raw_[settings_off::kMonitorSource];
    if (b > 2) return makeError(Errc::Malformed, "unknown monitor source value");
    if (on) { if (b == 1) b = 2; }          // microphone only -> both
    else    { if (b == 2) b = 1; }          // both -> microphone only
    return okStatus();
}
Status SettingsMessage::setHighVolumeSpeaker(bool v) { setBoolByte(raw_[settings_off::kHighVolumeSpeaker], v); return okStatus(); }
Status SettingsMessage::setHighImpedanceHeadphone(bool v) { setBoolByte(raw_[settings_off::kHighImpedanceHeadphone], v); return okStatus(); }
Status SettingsMessage::setExternalGainLock(bool v) { setBoolByte(raw_[settings_off::kExternalGainLock], v); return okStatus(); }
Status SettingsMessage::setLedBrightness(int p) {
    if (p < 0 || p > 100) return makeError(Errc::InvalidArgument, "brightness must be 0..100");
    raw_[settings_off::kLedBrightness] = static_cast<std::uint8_t>(p);
    return okStatus();
}
Status SettingsMessage::setLedColor(LedElement e, Rgb c) {
    auto o = ledOff(e);
    raw_[o] = c.r; raw_[o + 1] = c.g; raw_[o + 2] = c.b;
    return okStatus();
}

Result<StatusMessage> StatusMessage::fromBytes(std::span<const std::uint8_t> b) {
    if (auto s = checkSize(b, kSize, "status"); !s) return s.error();
    StatusMessage m;
    std::copy(b.begin(), b.end(), m.raw_.begin());
    return m;
}
static double meter(std::span<const std::uint8_t> b, std::size_t o) {
    double v = q88ToDouble(rdI16(b, o));
    return std::clamp(v, -60.0, 0.0);
}
double StatusMessage::input1LevelDb() const { return meter(raw_, 0x00); }
double StatusMessage::input2LevelDb() const { return meter(raw_, 0x09); }
double StatusMessage::levelAt(std::size_t o) const { return meter(raw_, o); }
std::uint8_t StatusMessage::autosetupResultRaw() const {
    std::uint8_t b = raw_[0x23];
    if (b & 0x40) return 0;
    return (b & 0x20) ? 2 : 1;
}

Result<ApiVersionMessage> ApiVersionMessage::fromBytes(std::span<const std::uint8_t> b) {
    if (auto s = checkSize(b, kSize, "api version"); !s) return s.error();
    ApiVersionMessage m;
    std::copy(b.begin(), b.end(), m.raw_.begin());
    return m;
}

Result<VersionMessage> VersionMessage::fromBytes(std::span<const std::uint8_t> b) {
    if (auto s = checkSize(b, kSize, "version"); !s) return s.error();
    VersionMessage m;
    std::copy(b.begin(), b.end(), m.raw_.begin());
    return m;
}

TriggerMessage::TriggerMessage(Trigger t) : t_(t) {}
Bytes TriggerMessage::bytes() const {
    Bytes out(kSize, 0);
    auto v = static_cast<std::uint16_t>(t_);
    out[0] = static_cast<std::uint8_t>(v & 0xFF);
    out[1] = static_cast<std::uint8_t>(v >> 8);
    return out;
}

}  // namespace lewitt
