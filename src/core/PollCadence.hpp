// Read-only real-device polling schedules. Settings stay at 2 Hz in both modes.
#pragma once
#include <chrono>

#include "ReadFreshness.hpp"

namespace lewitt {

struct RealPollCadence {
    std::chrono::milliseconds statusPeriod;
    int settingsEveryStatus;
    FreshnessWindow statusFreshness;
};

inline constexpr RealPollCadence kStatus4Hz{
    std::chrono::milliseconds(250), 2,
    {std::chrono::milliseconds(375), std::chrono::milliseconds(750)}};
inline constexpr RealPollCadence kStatus8HzExperimental{
    std::chrono::milliseconds(125), 4,
    {std::chrono::milliseconds(188), std::chrono::milliseconds(375)}};

static_assert(kStatus4Hz.statusPeriod.count() * kStatus4Hz.settingsEveryStatus == 500);
static_assert(kStatus8HzExperimental.statusPeriod.count() * kStatus8HzExperimental.settingsEveryStatus == 500);

} // namespace lewitt
