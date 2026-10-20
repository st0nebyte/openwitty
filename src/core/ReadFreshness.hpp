// Read-only data age, independent for settings and status. No USB or Qt dependency.
#pragma once
#include <chrono>
#include <optional>

namespace lewitt {

enum class Freshness { Unavailable, Fresh, Delayed, Stale };

constexpr const char* freshnessName(Freshness state) {
    switch (state) {
        case Freshness::Unavailable: return "unavailable";
        case Freshness::Fresh: return "fresh";
        case Freshness::Delayed: return "delayed";
        case Freshness::Stale: return "stale";
    }
    return "unavailable";
}

// The legacy diagnostic schedule uses 500 ms settings / 250 ms status.
// Production 16/48 Hz uses independent faster freshness windows supplied by
// DeviceWorker; successful reads of one type never refresh the other.
struct FreshnessWindow {
    std::chrono::milliseconds fresh;
    std::chrono::milliseconds stale;
};
inline constexpr FreshnessWindow kSettingsFreshness{std::chrono::milliseconds(750),
                                                    std::chrono::milliseconds(1500)};
inline constexpr FreshnessWindow kStatusFreshness{std::chrono::milliseconds(375),
                                                  std::chrono::milliseconds(750)};

class ReadFreshness {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit ReadFreshness(FreshnessWindow statusWindow = kStatusFreshness,
                           FreshnessWindow settingsWindow = kSettingsFreshness)
        : statusWindow_(statusWindow), settingsWindow_(settingsWindow) {}

    void reset() { settings_.reset(); status_.reset(); }
    void markSettings(TimePoint when = Clock::now()) { settings_ = when; }
    void markStatus(TimePoint when = Clock::now()) { status_ = when; }
    Freshness settings(TimePoint now, bool connected) const {
        return classify(settings_, now, connected, settingsWindow_);
    }
    Freshness status(TimePoint now, bool connected) const {
        return classify(status_, now, connected, statusWindow_);
    }

private:
    static Freshness classify(const std::optional<TimePoint>& last, TimePoint now,
                              bool connected, FreshnessWindow window) {
        if (!connected || !last || now < *last) return Freshness::Unavailable;
        const auto age = now - *last;
        if (age <= window.fresh) return Freshness::Fresh;
        if (age <= window.stale) return Freshness::Delayed;
        return Freshness::Stale;
    }
    std::optional<TimePoint> settings_, status_;
    FreshnessWindow statusWindow_, settingsWindow_;
};

}  // namespace lewitt
