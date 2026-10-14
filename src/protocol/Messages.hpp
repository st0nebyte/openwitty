// Typed views over the raw packed messages. Raw bytes are the source of truth:
// setters edit in place so reserved/unknown bytes survive read-modify-write.
#pragma once
#include "Types.hpp"

namespace lewitt {

enum class PreampType : std::uint8_t { Clean = 0, Warm = 1, Vivid = 2 };

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
inline constexpr std::size_t kVolumeA = 20;            // i16 Q8.8, grouping unverified
inline constexpr std::size_t kVolumeB = 27;            // i16 Q8.8, clamp -60..0 dB
inline constexpr std::size_t kMuteA = 22;
inline constexpr std::size_t kMuteB = 29;
inline constexpr std::size_t kMuteC = 19;
inline constexpr std::size_t kOutputSource = 23;
inline constexpr std::size_t kDim = 32;
inline constexpr std::size_t kMono = 33;
inline constexpr std::size_t kMonitorFlag = 34;
inline constexpr std::size_t kBalance = 35;            // i16 Q8.8, sign inverted
inline constexpr std::size_t kCustomButton = 53;
inline constexpr std::size_t kNativeVolumeMapping = 59;
}  // namespace settings_off

inline constexpr double kGainMinDb = 0.0;
inline constexpr double kGainMaxDb = 85.0;   // setter clamp 0x5500
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
    double volumeADb() const;
    double volumeBDb() const;
    bool dim() const;
    bool mono() const;
    double balance() const;
    std::uint8_t customButton() const;
    std::uint8_t nativeVolumeMapping() const;
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
    Status setVolumeADb(double db);
    Status setVolumeBDb(double db);
    Status setDim(bool v);
    Status setMono(bool v);

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
    std::uint16_t clipFlags1() const { return rdU16(raw_, 0x2E); }
    std::uint16_t clipFlags2() const { return rdU16(raw_, 0x30); }
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
