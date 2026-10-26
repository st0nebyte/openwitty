// Read-only comparison of two settings snapshots, for owner-approved hardware tests (openwitty --hw-check).
// It works on snapshots only: it has no access to a session or a transport and cannot send anything.
// Reports name offsets and fields, never raw payloads; the only value it reports is the LED brightness
// (0..100), which identifies nothing.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "protocol/Messages.hpp"

namespace lewitt {

struct SettingsComparison {
    std::vector<std::size_t> changed;      // offsets that differ between the two snapshots
    std::vector<std::size_t> unexpected;   // changed offsets that are not in `expected`
    int ledBrightnessFirst{0};
    int ledBrightnessSecond{0};
};

// `expected`: offsets the caller intends to differ (e.g. {37} after an LED change, {} after a restore).
SettingsComparison compareSettings(const SettingsMessage& first, const SettingsMessage& second,
                                   const std::vector<std::size_t>& expected);
// Coarse field name for a settings offset ("LED brightness", "speaker volume", ...; "offset N" if unknown).
std::string settingsFieldName(std::size_t offset);
// One line, e.g. "LED brightness 60 -> 40; changed: 37 (LED brightness); unexpected: none".
std::string describeComparison(const SettingsComparison& c);

}  // namespace lewitt
