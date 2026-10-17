// Core protocol types for the LEWITT CONNECT 2 control protocol.
// Qt-free. Evidence levels are documented in PROTOCOL.md.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace lewitt {

using Bytes = std::vector<std::uint8_t>;

enum class Errc {
    Ok,
    Timeout,
    Disconnected,
    ShortTransfer,
    Stall,
    Busy,            // interface owned by another driver
    AccessDenied,
    NotSupported,
    WritesDisabled,
    InvalidArgument,
    Malformed,
    NotFound,
    Io,
    Cancelled,       // request aborted by the application (shutdown, user cancel)
    ControlInUse,    // cooperative OpenWitty process lease, distinct from kernel-driver Busy
    Internal,        // bug / impossible state inside our own code
};

// Stable, human-oriented name (used in logs and the diagnostic CLI).
constexpr const char* errcName(Errc c) {
    switch (c) {
        case Errc::Ok: return "ok";
        case Errc::Timeout: return "timeout";
        case Errc::Disconnected: return "disconnected";
        case Errc::ShortTransfer: return "short-transfer";
        case Errc::Stall: return "stall";
        case Errc::Busy: return "busy";
        case Errc::AccessDenied: return "access-denied";
        case Errc::NotSupported: return "not-supported";
        case Errc::WritesDisabled: return "writes-disabled";
        case Errc::InvalidArgument: return "invalid-argument";
        case Errc::Malformed: return "malformed-response";
        case Errc::NotFound: return "not-found";
        case Errc::Io: return "io-error";
        case Errc::Cancelled: return "cancelled";
        case Errc::ControlInUse: return "control-in-use";
        case Errc::Internal: return "internal-error";
    }
    return "unknown";
}

struct Error {
    Errc code{Errc::Io};
    std::string message;
};

template <class T>
class Result {
public:
    Result(T v) : v_(std::move(v)) {}
    Result(Error e) : v_(std::move(e)) {}
    bool ok() const { return std::holds_alternative<T>(v_); }
    explicit operator bool() const { return ok(); }
    T& value() { return std::get<T>(v_); }
    const T& value() const { return std::get<T>(v_); }
    const Error& error() const { return std::get<Error>(v_); }
private:
    std::variant<T, Error> v_;
};

struct Unit {};
using Status = Result<Unit>;
inline Status okStatus() { return Status{Unit{}}; }
inline Error makeError(Errc c, std::string m) { return Error{c, std::move(m)}; }

enum class MessageId : std::uint8_t {
    Settings = 0,  // R/W 0x80
    Status = 2,    // R   0x40
    Version = 3,   // R   0x40
    Triggers = 4,  // W   0x40
    ApiVersion = 0x0A,  // R   0x40 (lewitt_api_t; its own apiMajor is 0)
};

struct MessageInfo {
    MessageId id;
    const char* name;
    std::size_t size;
    bool readable;
    bool writable;
};

inline constexpr std::array<MessageInfo, 5> kMessages{{
    {MessageId::Settings, "iface_settings_t", 0x80, true, true},
    {MessageId::Status, "iface_status_t", 0x40, true, false},
    {MessageId::Version, "iface_version_t", 0x40, true, false},
    {MessageId::Triggers, "iface_triggers_t", 0x40, false, true},
    {MessageId::ApiVersion, "lewitt_api_t", 0x40, true, false},
}};

// apiMajorVersion() of each message (high byte of wValue). iface_* messages are
// API v1; the API-version message itself is 0 (it is read before the version is known).
constexpr std::uint8_t apiMajorOf(MessageId id) { return id == MessageId::ApiVersion ? 0 : 1; }
inline constexpr std::uint8_t kSupportedApiMajor = 1;

const MessageInfo* messageInfo(MessageId id);

// --- USB-level constants (static analysis of macOS app, unverified on HW) ---
inline constexpr std::uint16_t kLewittVendorId = 0x29C2;
inline constexpr std::array<std::uint16_t, 3> kConnect2ProductIds{0x0002, 0x0004, 0x000A};
inline constexpr std::uint8_t kUnitId = 0x33;
inline constexpr std::uint8_t kApiMajor = 1;
inline constexpr std::uint8_t kBmGet = 0xA1;
inline constexpr std::uint8_t kBmSet = 0x21;
inline constexpr std::uint8_t kReqUac2Mem = 0x03;
inline constexpr std::uint8_t kReqUac1GetMem = 0x85;
inline constexpr std::uint8_t kReqUac1SetMem = 0x05;
inline constexpr unsigned kTimeoutMs = 500;

constexpr std::uint16_t wValueFor(MessageId id) {
    return static_cast<std::uint16_t>((apiMajorOf(id) << 8) | static_cast<std::uint8_t>(id));
}
constexpr std::uint16_t wIndexFor(std::uint8_t interfaceNumber) {
    return static_cast<std::uint16_t>((kUnitId << 8) | interfaceNumber);
}

// --- Q8.8 fixed point ---
constexpr double q88ToDouble(std::int16_t raw) { return raw / 256.0; }
// Round-to-nearest, saturating to int16 range.
constexpr std::int16_t doubleToQ88(double v) {
    double s = v * 256.0;
    if (s >= 32767.0) return 32767;
    if (s <= -32768.0) return -32768;
    return static_cast<std::int16_t>(s >= 0 ? s + 0.5 : s - 0.5);
}

// --- explicit little-endian helpers ---
constexpr std::uint16_t rdU16(std::span<const std::uint8_t> b, std::size_t o) {
    return static_cast<std::uint16_t>(b[o] | (b[o + 1] << 8));
}
constexpr std::int16_t rdI16(std::span<const std::uint8_t> b, std::size_t o) {
    return static_cast<std::int16_t>(rdU16(b, o));
}
constexpr void wrI16(std::span<std::uint8_t> b, std::size_t o, std::int16_t v) {
    auto u = static_cast<std::uint16_t>(v);
    b[o] = static_cast<std::uint8_t>(u & 0xFF);
    b[o + 1] = static_cast<std::uint8_t>(u >> 8);
}

}  // namespace lewitt
