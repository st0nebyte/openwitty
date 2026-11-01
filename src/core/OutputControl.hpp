// Speaker / headphone output control: scoped write policy, bounded increases, value validation.
// Qt-free; shared by the GUI, the CLI and openwitty-volumed so the safety rules cannot drift apart.
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/DeviceSession.hpp"
#include "protocol/Messages.hpp"

namespace lewitt {

enum class Output { Speaker, Headphone };

// One physical output as the device reports it (vendor settings message, Q8.8 dB, -60..0).
struct OutputLevel {
    double db{-60.0};
    bool muted{true};
};

OutputLevel outputLevel(const SettingsMessage& s, Output o);

// The settings bytes that belong to an output: volume (i16, two bytes) and mute.
//   speaker: 20, 21, 22     headphone: 27, 28, 29
std::vector<std::size_t> outputWriteOffsets(Output o);

// A write policy that authorizes hardware writes for exactly the given outputs' bytes and nothing
// else (no routing, no persistent changes, no other settings byte). Empty selection = read-only.
WritePolicy outputWritePolicy(bool speaker, bool headphone);
// Phase 12C: `base` plus byte 59 (native volume mapping) and 56 (touch focus) for "follow active output"; nothing else.
WritePolicy withFollowOutput(WritePolicy base);
// A write policy for exactly the given control groups (controlGroups(); unknown names add nothing): the union of their
// bytes and triggers, routing changes only with "routing". Never persistent changes, never a byte outside the groups.
WritePolicy groupsWritePolicy(const std::vector<std::string>& groups);
// Scoped policy for tests: speaker volume+mute (bytes 20-22), headphone volume+mute (27-29), LED brightness (37).
WritePolicy guiWritePolicy(bool speaker, bool headphone, bool ledBrightness);
// Every control group verified on the real CONNECT 2 (2026-10-26..11-01): speaker, headphone, ledBrightness, inputs,
// processing, monitoring, appearance, phantom, outputModes, routing, followOutput, measurements.
const std::vector<std::string>& controlGroups();
// The window's policy (product mode, no permission switches): all control groups. Everything else - unknown bytes,
// persistent changes, firmware update, factory reset, the device-restarting macOS streaming mode - stays refused.
WritePolicy appWritePolicy();
// Single device owner: what the background service lets its clients (window, CLI) write - the window's policy.
WritePolicy serviceClientPolicy();
// The bytes and triggers of one control group (empty for an unknown name).
std::vector<std::size_t> permissionOffsets(const std::string& name);
std::vector<Trigger> permissionTriggers(const std::string& name);

// Default per-step limit for input gain increases on real hardware (Phase 11A).
inline constexpr double kMaxGainIncreaseDb = 6.0;

// Clamp to the device range and round to the Q8.8 grid the device stores. Non-finite -> nullopt.
std::optional<double> normalizeOutputDb(double db);

// Default per-step limit for user/desktop-originated volume increases on real hardware.
inline constexpr double kMaxOutputIncreaseDb = 10.0;

// Bounded increase: a request may raise the level by at most maxIncreaseDb above `currentDb` in one
// step. Decreases are never limited. maxIncreaseDb must be finite and >= 0 (otherwise 0 is used).
double limitIncrease(double currentDb, double requestedDb, double maxIncreaseDb);

// Stage an output change into `m` (volume and/or mute). Rejects non-finite or out-of-range volumes
// before touching the message, so a rejected request never leaves a half-staged edit.
Status stageOutput(SettingsMessage& m, Output o, std::optional<double> db, std::optional<bool> mute);

}  // namespace lewitt
