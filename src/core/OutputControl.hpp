// Speaker / headphone output control: scoped write policy, bounded increases, value validation.
// Qt-free; shared by the GUI, the CLI and openwitty-volumed so the safety rules cannot drift apart.
#pragma once
#include <optional>
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
// Phase 10B: the GUI's hardware write policy - exactly the union of the individually confirmed permissions:
// speaker volume+mute (bytes 20-22), headphone volume+mute (27-29), LED brightness (37). No triggers, no routing
// or persistent changes. All false = the read-only default (no hardware writes at all).
WritePolicy guiWritePolicy(bool speaker, bool headphone, bool ledBrightness);

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
