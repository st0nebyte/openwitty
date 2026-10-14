// Documented product restrictions and risk classification (Qt-free).
// Sources: official online guide (documented behaviour) and binary analysis
// (field offsets). Neither is hardware-verified.
#pragma once
#include <cstdint>

#include "protocol/Messages.hpp"

namespace lewitt {

// Guide: "Denoise & Comp are not available when recording in 96 kHz."
// Rates above 96 kHz are treated the same (the device is not documented beyond 96 kHz).
constexpr bool dspAvailable(std::uint32_t sampleRateHz) { return sampleRateHz < 96000; }

// True when a settings change alters USB channel routing or restarts the
// device according to the guide (mobile / macOS streaming modes). The macOS
// streaming flag's offset is not yet located, so only mobile streaming is
// detectable; callers must treat this as potentially disruptive.
inline bool changesRouting(const SettingsMessage& before, const SettingsMessage& after) {
    return before.mobileStreaming() != after.mobileStreaming();
}

// Safety classification of changes for confirmation UI / policy.
struct ChangeRisk {
    bool phantomPowerEnabled{false};  // 48 V switched on: confirm first
    bool routing{false};              // see changesRouting
    bool highVolumeSpeaker{false};    // louder output: confirm first
};

inline ChangeRisk assessChange(const SettingsMessage& before, const SettingsMessage& after) {
    ChangeRisk r;
    r.phantomPowerEnabled = !before.phantom1() && after.phantom1();
    r.routing = changesRouting(before, after);
    r.highVolumeSpeaker = !before.highVolumeSpeaker() && after.highVolumeSpeaker();
    return r;
}

// Documented denoise/compress restriction check on a pending change.
inline bool violatesDspRestriction(const SettingsMessage& before, const SettingsMessage& after,
                                   std::uint32_t sampleRateHz) {
    if (dspAvailable(sampleRateHz)) return false;
    for (int i = 0; i < 2; ++i) {
        if (!before.denoise(i) && after.denoise(i)) return true;
        if (!before.compress(i) && after.compress(i)) return true;
    }
    return false;
}

}  // namespace lewitt
