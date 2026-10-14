// Backend-level write safety (independent of any GUI state). Every outgoing write is
// checked here; transports and the session both call it, so a bug in one layer cannot
// open a path to a destructive command.
#pragma once
#include "Messages.hpp"

namespace lewitt {

struct WriteGuardContext {
    bool simulated{false};            // simulated transports may exercise mode switching
    bool allowPersistentChanges{false};
};

// Returns ok or a WritesDisabled / NotSupported / InvalidArgument error.
// Never allowed (any transport): FactoryReset, writes to read-only messages, wrong sizes.
// Never allowed on real hardware: device-mode switching.
// Needs allowPersistentChanges: ResetLedColors.
Status checkWriteAllowed(MessageId id, std::span<const std::uint8_t> data, const WriteGuardContext& ctx);

// Trigger word from a trigger message (first two bytes, little endian).
constexpr std::uint16_t triggerWordOf(std::span<const std::uint8_t> msg) {
    return msg.size() >= 2 ? static_cast<std::uint16_t>(msg[0] | (msg[1] << 8)) : 0;
}

// The settings bytes the application is allowed to change (those with a verified setter).
// Used to guarantee that unknown/reserved bytes and unsupported enum values are never
// rewritten as a side effect of an edit.
bool isKnownWritableSettingsByte(std::size_t offset);

// ok iff `after` differs from `before` only inside known writable bytes.
Status verifyOnlyKnownFieldsChanged(const SettingsMessage& before, const SettingsMessage& after);

}  // namespace lewitt
