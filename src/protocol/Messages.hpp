// Typed views over the raw packed messages. Raw bytes are the source of truth:
// setters edit in place so reserved/unknown bytes survive read-modify-write.
#pragma once
#include <optional>
#include <vector>

#include "Types.hpp"

namespace lewitt {

enum class PreampType : std::uint8_t { Clean = 0, Warm = 1, Vivid = 2 };
enum class MonitorSource : std::uint8_t { InstrumentOnly = 0, MicrophoneOnly = 1, Both = 2 };
// Settings byte 56 is the physical touch-surface focus. Values 1..4 were confirmed on a
// CONNECT 2; all other values deliberately remain unknown rather than being guessed.
enum class SelectedChannel : std::uint8_t { Microphone = 1, Instrument = 2, Speaker = 3, Headphone = 4 };
enum class LedElement { Icons, Mute, Parameter, Metering, GainReduction };

// CustomButtonShortPress (settings byte 53). Values recovered from the Windows
// settings UI: each radio button is constructed with its enum value
// (FUN_140252390 / EnumCircleRadioButton ctor immediates); medium-high confidence.
// Values 3..7, 10, 11 are not offered by the CONNECT 2 UI.
enum class CustomButtonFunction : std::uint8_t {
    ShowControlCenter = 0,
    ToggleHeadphoneSpeakerMute = 1,       // mute headphone/unmute speakers <-> inverse
    ToggleHeadphoneMicSpeakerMute = 2,    // same, and the microphone
    DimOutputs = 8,
    MonoStereoOutputs = 9,
    TogglePreampMic = 0x0C,
    TogglePreampInstrument = 0x0D,
    ToggleBrightness = 0x0E,
};
constexpr bool isKnownCustomButton(std::uint8_t v) {
    return v <= 2 || v == 8 || v == 9 || v == 0x0C || v == 0x0D || v == 0x0E;
}

// USB device mode as the app derives it from the product id
// (Connect2Device::getCurrentMode: PID 0x0004 -> 0, PID 0x0002 -> 1; other PIDs throw).
// Mode 1 is entered by the "macOS streaming mode" toggle (PowerCycleWindow(Mode=1)).
enum class DeviceMode { Normal, MacOSStreaming, Unknown };
constexpr DeviceMode deviceModeFromPid(std::uint16_t pid) {
    return pid == 0x0004 ? DeviceMode::Normal
         : pid == 0x0002 ? DeviceMode::MacOSStreaming : DeviceMode::Unknown;
}

// The app's settings schema (Connect2Device::tick merge table, 62 records).
// Bytes 0..70 are fields; bytes 71..127 are never touched by the app.
struct SettingsField { std::uint8_t offset; std::uint8_t length; };
inline constexpr std::array<SettingsField, 62> kSettingsSchema{{
    {0,2},{2,1},{3,1},{4,1},{5,1},{6,1},{7,1},{8,1},{9,1},{10,2},{12,1},{13,1},{14,1},{15,1},
    {16,1},{17,1},{18,1},{19,1},{20,2},{22,1},{23,1},{24,1},{25,1},{26,1},{27,2},{29,1},{30,1},
    {31,1},{32,1},{33,1},{34,1},{35,2},{37,1},{38,1},{39,1},{40,1},{41,1},{42,1},{43,1},{44,1},
    {45,1},{46,1},{47,1},{48,1},{49,1},{50,1},{51,1},{52,1},{53,1},{54,1},{55,1},{56,1},{57,1},
    {58,1},{59,1},{60,1},{61,1},{62,1},{63,4},{67,1},{68,2},{70,1},
}};
inline constexpr std::size_t kSettingsDefinedBytes = 71;
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
// Set by the device after a Denoise measurement, together with switching Denoise on (session 5, 2026-10-30: mic
// 202 -> 196, instrument 211 -> 215). Most likely the measured noise level; scale unknown. OpenWitty never writes it
// on its own: a settings write carries the read-back value through.
inline constexpr std::size_t kDenoiseLevel[2] = {8, 17};
inline constexpr std::size_t kCompress[2] = {9, 18};
// Output groups: names are the app's own tree-group identifiers
// (Loopback / Speaker / Headphone / Monitor), confirmed by name in the binary.
inline constexpr std::size_t kLoopbackMute = 19;
inline constexpr std::size_t kSpeakerVolume = 20;      // i16 Q8.8, -60..0 dB
inline constexpr std::size_t kSpeakerMute = 22;
inline constexpr std::size_t kMixToSpeakers = 23;      // OutputSource enum; the official UI writes only 0/1 (switch)
inline constexpr std::size_t kHighVolumeSpeaker = 24;  // bool
inline constexpr std::size_t kHeadphoneVolume = 27;    // i16 Q8.8, -60..0 dB
inline constexpr std::size_t kHeadphoneMute = 29;
inline constexpr std::size_t kHighImpedanceHeadphone = 31;  // bool
inline constexpr std::size_t kDim = 32;
inline constexpr std::size_t kMono = 33;
// Device-maintained: the device sets it to the Dim value itself (HW 11B session 1, 2026-10-28: Dim 0->1 and
// 1->0 each changed byte 25 the same way, nothing else). OpenWitty never edits it.
inline constexpr std::size_t kSpeakerDimState = 25;
// Device-maintained in the same way for Mono (HW 11B session 1: Mono 0->1 changed byte 26 0->1, nothing else).
inline constexpr std::size_t kSpeakerMonoState = 26;
inline constexpr std::size_t kMonitorSource = 34;      // 0 instrument, 1 microphone, 2 both
inline constexpr std::size_t kInputPlaybackMix = 35;   // i16 Q8.8, stored sign-inverted
inline constexpr std::size_t kLedBrightness = 37;      // 0..100
inline constexpr std::size_t kLedColorIcons = 38;      // RGB x3 bytes: 38..52
inline constexpr std::size_t kLedColorMute = 41;
inline constexpr std::size_t kLedColorParameter = 44;
inline constexpr std::size_t kLedColorMetering = 47;
inline constexpr std::size_t kLedColorGainReduction = 50;
inline constexpr std::size_t kCustomButton = 53;       // enum
inline constexpr std::size_t kSelectedChannel = 56;    // physical touch-surface focus (HW-read)
inline constexpr std::size_t kExternalGainLock = 57;   // bool (ExternalGainControl)
inline constexpr std::size_t kNativeVolumeMapping = 59;// "Map native volume controls to": 0 None, 1 Speakers, 2 Headphones (Windows 5.2.2; 2026-10-31)
inline constexpr std::size_t kMobileStreaming = 67;    // bool; official text: "Microphone and instrument mix to software input 1/2"
}  // namespace settings_off

// Bytes the device itself changes as a consequence of a write to `written` (observed on hardware). A write
// verification accepts a change of these bytes only when their parent byte was part of the write.
inline std::vector<std::size_t> deviceFollowerOffsets(std::size_t written) {
    if (written == settings_off::kDim) return {settings_off::kSpeakerDimState};
    if (written == settings_off::kMono) return {settings_off::kSpeakerMonoState};
    return {};
}

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
    // Noise level the device stored at its last Denoise measurement (bytes 8 / 17, int8 dBFS, about the peak level of
    // the background noise; session 6). 0 = never measured on the simulated device.
    int denoiseLevelDb(int input) const;
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
    // nullopt if the byte holds a value the CONNECT 2 UI never offers.
    std::optional<CustomButtonFunction> customButtonFunction() const;
    // "Input/playback mix applies to speakers": only 0/1 are ever written by the UI
    // (bound to a boolean toggle); other raw values are preserved and read as "on".
    bool mixToSpeakers() const { return mixToSpeakersRaw() != 0; }
    std::uint8_t nativeVolumeMapping() const;
    std::uint8_t ledBrightness() const;
    std::uint8_t selectedChannelRaw() const;
    std::optional<SelectedChannel> selectedChannel() const;
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
    Status setMixToSpeakers(bool v);
    Status setMobileStreaming(bool v);
    Status setCustomButtonFunction(CustomButtonFunction f);
    // Field-wise merge used by the original app's sync loop: for every schema field
    // whose device value changed between `snapshot` and `device`, take the device
    // value; fields the user edited but the device did not change are kept.
    void mergeExternalChanges(const SettingsMessage& snapshot, const SettingsMessage& device);
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
    double loopbackLevelDb(int ch) const { return levelAt(0x12 + 2u * static_cast<unsigned>(ch & 1)); }
    double speakerLevelDb(int ch) const { return levelAt(0x16 + 2u * static_cast<unsigned>(ch & 1)); }
    double headphoneLevelDb(int ch) const { return levelAt(0x1A + 2u * static_cast<unsigned>(ch & 1)); }
    // Clip event counters (u16): the app compares each against its previous
    // snapshot; any change means "clipping detected" for that input.
    std::uint16_t clipCounter(int input) const { return rdU16(raw_, input == 0 ? 0x2E : 0x30); }
    // Measurement outcome derived from the same byte as autosetupResultRaw():
    // 0 = idle/in progress (bit 6), 1 = failed (bit 5 clear), 2 = succeeded (bit 5 set).
    // Failure reason: no clipping counter change -> "no signal"; counter change -> "clipping".
    enum class MeasureOutcome : std::uint8_t { InProgressOrIdle = 0, Failed = 1, Succeeded = 2 };
    MeasureOutcome measureOutcome() const { return static_cast<MeasureOutcome>(autosetupResultRaw()); }
    bool measurementFinished() const { return autosetupProgress() >= 100; }
private:
    double levelAt(std::size_t o) const;
    std::array<std::uint8_t, kSize> raw_{};
};

// lewitt_api_t (message 0x0A): the device's API version handshake. LewittDevice::open
// reads it and stores byte 0 / byte 1 as 16-bit major / minor; LewittDevice::read/write
// throw "Message API version not supported by the device." when a message's
// apiMajorVersion differs from the device major. Remaining bytes unknown.
class ApiVersionMessage {
public:
    static constexpr std::size_t kSize = 0x40;
    static Result<ApiVersionMessage> fromBytes(std::span<const std::uint8_t> b);
    std::uint8_t major() const { return raw_[0]; }
    std::uint8_t minor() const { return raw_[1]; }
    std::span<const std::uint8_t> bytes() const { return raw_; }
private:
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
    // Command words written to byte 0..1 of the trigger message. Assignments are
    // confirmed by lambda/identifier creation adjacency in buildConnect2Tree:
    // each lambda is constructed immediately before the identifier it serves.
    AutoGainMicrophone = 1,          // id AutoGain (Microphone group)
    AutoGainInstrument = 2,          // id AutoGain (Instrument group)
    AutoGainBoth = 3,                // id AutoGainMicPlusInst
    DenoiseMeasureMicrophone = 6,    // id DenoiseMeasurement (Microphone)
    DenoiseMeasureInstrument = 7,    // id DenoiseMeasurement (Instrument)
    DenoiseMeasureBoth = 8,          // id DenoiseMicPlusInst
    // Written by UI handlers (macOS InputChannelPanel::onAutoGainButtonClicked /
    // onDenoiseButtonClicked, CaptionInputChannel::onAutoSetupButtonClicked store 4;
    // autoGainFinished / denoiseFinished / ~AutoSetupWindow store 5; same set found on
    // Windows). Bracket a measurement session; exact device-side effect unverified.
    BeginMeasurementSession = 4,
    EndMeasurementSession = 5,
    // PowerCycleComponent::onOKButtonClicked (macOS) / FUN_140201510 (Windows): target
    // mode 0 -> 0x105, target mode 1 -> 0x104; device re-enumerates with the new PID.
    SwitchToNormalMode = 0x105,
    SwitchToMacOSStreamingMode = 0x104,
    ResetLedColors = 0x103,          // id ResetLedColors  (persistent LED change)
    FactoryReset = 0x106,            // id FactoryReset    (destructive; never sent)
};
// Backwards-compatible names used by earlier code.
inline constexpr Trigger kAutoGainInput1 = Trigger::AutoGainMicrophone;

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
static_assert(wValueFor(MessageId::ApiVersion) == 0x000A);
static_assert(wIndexFor(2) == 0x3302);
}  // namespace lewitt
