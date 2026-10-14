// Typed views over the raw packed messages. Raw bytes are the source of truth:
// setters edit in place so reserved/unknown bytes survive read-modify-write.
#pragma once
#include "Types.hpp"

namespace lewitt {

enum class PreampType : std::uint8_t { Clean = 0, Warm = 1, Vivid = 2 };
enum class MonitorSource : std::uint8_t { InstrumentOnly = 0, MicrophoneOnly = 1, Both = 2 };
enum class LedElement { Icons, Mute, Parameter, Metering, GainReduction };
struct Rgb {
    std::uint8_t r{0}, g{0}, b{0};
    bool operator==(const Rgb&) const = default;
};

// Settings struct offsets (0x80 bytes). Verification: static analysis only.
namespace settings_off {
inline constexpr std::size_t kGain[2] = {0, 10};       // i16 Q8.8 dB, 0..85
inline constexpr std::size_t kMute[2] = {2, 12};
inline constexpr std::size_t kHighPass[2] = {3, 13};
inline constexpr std::size_t kClipguard[2] = {4, 14};
inline constexpr std::size_t kPhantom1 = 5;            // input 1 only (input 2 not located)
inline constexpr std::size_t kPreamp[2] = {6, 15};     // 0/1/2
inline constexpr std::size_t kDenoise[2] = {7, 16};
inline constexpr std::size_t kCompress[2] = {9, 18};
// Output groups: names are the app's own tree-group identifiers
// (Loopback / Speaker / Headphone / Monitor), confirmed by name in the binary.
inline constexpr std::size_t kLoopbackMute = 19;
inline constexpr std::size_t kSpeakerVolume = 20;      // i16 Q8.8, -60..0 dB
inline constexpr std::size_t kSpeakerMute = 22;
inline constexpr std::size_t kMixToSpeakers = 23;      // OutputSource enum (value set unknown)
inline constexpr std::size_t kHighVolumeSpeaker = 24;  // bool
inline constexpr std::size_t kHeadphoneVolume = 27;    // i16 Q8.8, -60..0 dB
inline constexpr std::size_t kHeadphoneMute = 29;
inline constexpr std::size_t kHighImpedanceHeadphone = 31;  // bool
inline constexpr std::size_t kDim = 32;
inline constexpr std::size_t kMono = 33;
inline constexpr std::size_t kMonitorSource = 34;      // 0 instrument, 1 microphone, 2 both
inline constexpr std::size_t kInputPlaybackMix = 35;   // i16 Q8.8, stored sign-inverted
inline constexpr std::size_t kLedBrightness = 37;      // 0..100
inline constexpr std::size_t kLedColorIcons = 38;      // RGB x3 bytes: 38..52
inline constexpr std::size_t kLedColorMute = 41;
inline constexpr std::size_t kLedColorParameter = 44;
inline constexpr std::size_t kLedColorMetering = 47;
inline constexpr std::size_t kLedColorGainReduction = 50;
inline constexpr std::size_t kCustomButton = 53;       // enum
inline constexpr std::size_t kExternalGainLock = 57;   // bool (ExternalGainControl)
inline constexpr std::size_t kNativeVolumeMapping = 59;// enum (MasterSliderMap)
inline constexpr std::size_t kMobileStreaming = 67;    // bool
}  // namespace settings_off

inline constexpr double kGainMinDb = 0.0;
inline constexpr double kGainMaxDb = 85.0;   // device-level setter clamp 0x5500 (binary)
// Control Center scale labels (official guide screenshot): mic 0..72 dB, instrument 0..60 dB.
// We enforce these narrower, documented ranges for writes.
constexpr double gainLimitDb(int input) { return input == 0 ? 72.0 : 60.0; }
inline constexpr double kVolumeMinDb = -60.0;
inline constexpr double kVolumeMaxDb = 0.0;

class SettingsMessage {
public:
    static constexpr std::size_t kSize = 0x80;
    SettingsMessage() = default;
    static Result<SettingsMessage> fromBytes(std::span<const std::uint8_t> b);

    std::span<const std::uint8_t> bytes() const { return raw_; }
    bool operator==(const SettingsMessage&) const = default;

    // getters (input: 0 or 1)
    double gainDb(int input) const;
    bool mute(int input) const;
    bool highPass(int input) const;
    bool clipguard(int input) const;
    bool phantom1() const;
    PreampType preamp(int input) const;
    std::uint8_t preampRaw(int input) const;
    bool denoise(int input) const;
    bool compress(int input) const;
    double speakerVolumeDb() const;
    double headphoneVolumeDb() const;
    bool speakerMute() const;
    bool headphoneMute() const;
    bool loopbackMute() const;
    bool dim() const;
    bool mono() const;
    // Input/playback mix control (app id "Balance"); raw is stored sign-inverted.
    double inputPlaybackMix() const;
    MonitorSource monitorSource() const;
    bool monitorMicrophone() const;
    bool monitorInstrument() const;
    std::uint8_t mixToSpeakersRaw() const;
    bool highVolumeSpeaker() const;
    bool highImpedanceHeadphone() const;
    bool externalGainLock() const;
    bool mobileStreaming() const;
    std::uint8_t customButton() const;
    std::uint8_t nativeVolumeMapping() const;
    std::uint8_t ledBrightness() const;
    Rgb ledColor(LedElement e) const;
    std::uint8_t byteAt(std::size_t off) const { return raw_[off]; }

    // setters validate range; they never touch bytes outside the field
    Status setGainDb(int input, double db);
    Status setMute(int input, bool v);
    Status setHighPass(int input, bool v);
    Status setClipguard(int input, bool v);
    Status setPhantom1(bool v);
    Status setPreamp(int input, PreampType t);
    Status setDenoise(int input, bool v);
    Status setCompress(int input, bool v);
    Status setSpeakerVolumeDb(double db);
    Status setHeadphoneVolumeDb(double db);
    Status setSpeakerMute(bool v);
    Status setHeadphoneMute(bool v);
    Status setLoopbackMute(bool v);
    Status setDim(bool v);
    Status setMono(bool v);
    Status setInputPlaybackMix(double v);
    // Reproduces the app's pair-table state machine: at least one source stays
    // selected (instrument-only <-> both <-> microphone-only).
    Status setMonitorMicrophone(bool on);
    Status setMonitorInstrument(bool on);
    Status setHighVolumeSpeaker(bool v);
    Status setHighImpedanceHeadphone(bool v);
    Status setExternalGainLock(bool v);
    Status setLedBrightness(int percent);
    Status setLedColor(LedElement e, Rgb c);

private:
    std::array<std::uint8_t, kSize> raw_{};
};

class StatusMessage {
public:
    static constexpr std::size_t kSize = 0x40;
    static Result<StatusMessage> fromBytes(std::span<const std::uint8_t> b);
    std::span<const std::uint8_t> bytes() const { return raw_; }

    // Peak/level meters, dB in [-60, 0] (negative clamp per app). Offsets are
    // the app's reads; channel assignment beyond input 1/2 is unverified.
    double input1LevelDb() const;
    double input2LevelDb() const;
    std::uint8_t input1AutoGain() const { return raw_[0x02]; }
    std::uint8_t input2AutoGain() const { return raw_[0x0B]; }
    // Auto-setup result: 0 = none/in progress (bit6 set), 1 = bit5 clear, 2 = bit5 set.
    // Meaning of 1 vs 2 not established.
    std::uint8_t autosetupResultRaw() const;
    std::uint8_t autosetupProgress() const { return raw_[0x24]; }
    // Output meters: group order Loopback, Speaker, Headphone (group names
    // confirmed in the binary). Channel L/R per group, dB clamped [-60, 0].
    double loopbackLevelDb(int ch) const { return levelAt(0x12 + 2 * (ch & 1)); }
    double speakerLevelDb(int ch) const { return levelAt(0x16 + 2 * (ch & 1)); }
    double headphoneLevelDb(int ch) const { return levelAt(0x1A + 2 * (ch & 1)); }
    std::uint16_t clipFlags1() const { return rdU16(raw_, 0x2E); }
    std::uint16_t clipFlags2() const { return rdU16(raw_, 0x30); }
private:
    double levelAt(std::size_t o) const;
    std::array<std::uint8_t, kSize> raw_{};
};

// iface_version_t: field layout not yet recovered; kept opaque.
class VersionMessage {
public:
    static constexpr std::size_t kSize = 0x40;
    static Result<VersionMessage> fromBytes(std::span<const std::uint8_t> b);
    std::span<const std::uint8_t> bytes() const { return raw_; }
private:
    std::array<std::uint8_t, kSize> raw_{};
};

// iface_triggers_t: first u16 is the command word the app writes; remaining
// bytes are assumed zero (app's trigger buffer layout beyond word 0 unverified).
enum class Trigger : std::uint16_t {
    // Values observed as immediates in the UI callbacks. Their exact actions are
    // inferred from callback ordering and are UNVERIFIED.
    AutoGainInput1 = 1,
    AutoGainInput2 = 2,
    AutoGainBoth = 3,
    DenoiseMeasureInput1 = 6,
    DenoiseMeasureInput2 = 7,
    DenoiseMeasureBoth = 8,
    Unknown0x103 = 0x103,  // possibly LED related; do not send
    Unknown0x106 = 0x106,  // possibly LED related; do not send
};

class TriggerMessage {
public:
    static constexpr std::size_t kSize = 0x40;
    explicit TriggerMessage(Trigger t);
    Bytes bytes() const;
    Trigger trigger() const { return t_; }
private:
    Trigger t_;
};

}  // namespace lewitt

namespace lewitt {
static_assert(SettingsMessage::kSize == 0x80);
static_assert(StatusMessage::kSize == 0x40 && VersionMessage::kSize == 0x40 && TriggerMessage::kSize == 0x40);
static_assert(wValueFor(MessageId::Version) == 0x0103);
static_assert(wIndexFor(2) == 0x3302);
}  // namespace lewitt
