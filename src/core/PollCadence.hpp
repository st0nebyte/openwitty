// Read-only real-device polling schedules. Production uses 48 Hz status and
// an independent 16 Hz settings deadline; the 4/2 Hz schedule is diagnostic.
#pragma once
#include <chrono>
#include <optional>

#include "ReadFreshness.hpp"

namespace lewitt {

struct RealPollCadence {
    std::chrono::nanoseconds statusPeriod;
    // Legacy integral schedules use this divisor. A zero selects an independent
    // monotonic settings deadline for fractional status periods.
    int settingsEveryStatus;
    FreshnessWindow statusFreshness;
    bool preciseScheduling{false};
};

inline constexpr RealPollCadence kStatus4Hz{
    std::chrono::milliseconds(250), 2,
    {std::chrono::milliseconds(375), std::chrono::milliseconds(750)}};
inline constexpr RealPollCadence kStatus8HzExperimental{
    std::chrono::milliseconds(125), 4,
    {std::chrono::milliseconds(188), std::chrono::milliseconds(375)}};
inline constexpr RealPollCadence kStatus48Hz{
    // Ceiling to a whole nanosecond; absolute deadlines avoid cumulative drift.
    std::chrono::nanoseconds((1'000'000'000LL + 47) / 48), 0,
    // At this rate a single ordinary GUI scheduling delay should not announce stale
    // data. Delayed begins after ~3.6 periods; stale after ~9.6 periods.
    {std::chrono::milliseconds(75), std::chrono::milliseconds(200)}, true};

// Settings freshness at the validated 16 Hz production rate (and the retained
// 8 Hz test cadence): fresh/delayed windows allow two/four settings periods.
inline constexpr FreshnessWindow kSettings8HzFreshness{
    std::chrono::milliseconds(250), std::chrono::milliseconds(500)};
inline constexpr FreshnessWindow kSettings16HzFreshness{
    std::chrono::milliseconds(125), std::chrono::milliseconds(250)};

// The high-rate mode uses absolute monotonic deadlines, skipping any missed
// ticks rather than issuing a catch-up burst. No Qt or USB dependency.
class RealPollSchedule {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    explicit RealPollSchedule(RealPollCadence cadence,
                              std::chrono::nanoseconds settingsPeriod = std::chrono::microseconds(62500))
        : cadence_(cadence), settingsPeriod_(settingsPeriod) {}

    void reset(TimePoint now) {
        nextStatus_ = now;
        nextSettings_ = now + settingsPeriod_;
    }
    TimePoint nextStatusAfter(TimePoint now) {
        if (!nextStatus_) reset(now);
        if (now < *nextStatus_) return *nextStatus_;
        const auto missed = (now - *nextStatus_) / cadence_.statusPeriod + 1;
        *nextStatus_ += cadence_.statusPeriod * missed;
        return *nextStatus_;
    }
    bool settingsDue(TimePoint now) {
        if (!nextSettings_) reset(now);
        if (now < *nextSettings_) return false;
        const auto missed = (now - *nextSettings_) / settingsPeriod_ + 1;
        *nextSettings_ += settingsPeriod_ * missed;
        return true;
    }
private:
    RealPollCadence cadence_;
    std::chrono::nanoseconds settingsPeriod_;
    std::optional<TimePoint> nextStatus_, nextSettings_;
};

static_assert(kStatus4Hz.statusPeriod * kStatus4Hz.settingsEveryStatus == std::chrono::milliseconds(500));
static_assert(kStatus8HzExperimental.statusPeriod * kStatus8HzExperimental.settingsEveryStatus == std::chrono::milliseconds(500));
static_assert(kStatus48Hz.statusPeriod.count() == 20'833'334);
static_assert(kStatus48Hz.settingsEveryStatus == 0 && kStatus48Hz.preciseScheduling);

} // namespace lewitt
