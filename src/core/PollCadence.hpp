// Read-only real-device polling schedules. Production settings stay at 2 Hz;
// opt-in experiments may shorten their independent deadline.
#pragma once
#include <chrono>
#include <optional>

#include "ReadFreshness.hpp"

namespace lewitt {

struct RealPollCadence {
    std::chrono::nanoseconds statusPeriod;
    // Legacy integral schedules use this divisor. A zero selects independent
    // monotonic 500 ms settings deadlines for fractional status periods.
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
inline constexpr RealPollCadence kStatus48HzExperimental{
    // Ceiling to a whole nanosecond: the 24th tick lands just after 500 ms,
    // so the independent settings deadline is not deferred by one status tick.
    std::chrono::nanoseconds((1'000'000'000LL + 47) / 48), 0,
    // At this rate a single ordinary GUI scheduling delay should not announce stale
    // data. Delayed begins after ~3.6 periods; stale after ~9.6 periods.
    {std::chrono::milliseconds(75), std::chrono::milliseconds(200)}, true};

// Only used with the explicit 48 Hz status experiment. Fresh/delayed windows
// allow two/four settings periods, including ordinary GUI scheduling jitter.
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
                              std::chrono::nanoseconds settingsPeriod = std::chrono::milliseconds(500))
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
static_assert(kStatus48HzExperimental.statusPeriod.count() == 20'833'334);
static_assert(kStatus48HzExperimental.settingsEveryStatus == 0 && kStatus48HzExperimental.preciseScheduling);

} // namespace lewitt
