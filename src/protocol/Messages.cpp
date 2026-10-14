#include "Messages.hpp"

#include <algorithm>
#include <cmath>

namespace lewitt {

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
double SettingsMessage::volumeADb() const { return q88ToDouble(rdI16(raw_, settings_off::kVolumeA)); }
double SettingsMessage::volumeBDb() const { return q88ToDouble(rdI16(raw_, settings_off::kVolumeB)); }
bool SettingsMessage::dim() const { return raw_[settings_off::kDim] != 0; }
bool SettingsMessage::mono() const { return raw_[settings_off::kMono] != 0; }
double SettingsMessage::balance() const { return 0.0 - q88ToDouble(rdI16(raw_, settings_off::kBalance)); }
std::uint8_t SettingsMessage::customButton() const { return raw_[settings_off::kCustomButton]; }
std::uint8_t SettingsMessage::nativeVolumeMapping() const { return raw_[settings_off::kNativeVolumeMapping]; }

Status SettingsMessage::setGainDb(int i, double db) {
    if (!inputOk(i)) return Status{badInput()};
    if (!std::isfinite(db) || db < kGainMinDb || db > kGainMaxDb)
        return makeError(Errc::InvalidArgument, "gain must be within 0..85 dB");
    wrI16(raw_, settings_off::kGain[i], doubleToQ88(db));
    return okStatus();
}
#define LW_BOOL_SETTER(Name, Off)                                  \
    Status SettingsMessage::Name(int i, bool v) {                  \
        if (!inputOk(i)) return Status{badInput()};                \
        raw_[Off[i]] = v ? 1 : 0;                                  \
        return okStatus();                                         \
    }
LW_BOOL_SETTER(setMute, settings_off::kMute)
LW_BOOL_SETTER(setHighPass, settings_off::kHighPass)
LW_BOOL_SETTER(setClipguard, settings_off::kClipguard)
LW_BOOL_SETTER(setDenoise, settings_off::kDenoise)
LW_BOOL_SETTER(setCompress, settings_off::kCompress)
#undef LW_BOOL_SETTER

Status SettingsMessage::setPhantom1(bool v) {
    raw_[settings_off::kPhantom1] = v ? 1 : 0;
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
Status SettingsMessage::setVolumeADb(double db) { return setVol(raw_, settings_off::kVolumeA, db); }
Status SettingsMessage::setVolumeBDb(double db) { return setVol(raw_, settings_off::kVolumeB, db); }
Status SettingsMessage::setDim(bool v) { raw_[settings_off::kDim] = v; return okStatus(); }
Status SettingsMessage::setMono(bool v) { raw_[settings_off::kMono] = v; return okStatus(); }

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
std::uint8_t StatusMessage::autosetupResultRaw() const {
    std::uint8_t b = raw_[0x23];
    if (b & 0x40) return 0;
    return (b & 0x20) ? 2 : 1;
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
