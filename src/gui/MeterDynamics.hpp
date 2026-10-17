// Presentation-only meter envelope. Inputs are observed status samples; no USB or Qt dependency.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>

namespace lewitt {

class MeterDynamics {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    enum class SourceState { Unavailable, Fresh, Delayed, Stale };
    static constexpr std::size_t kChannels = 8;
    static constexpr double kFloorDb = -60.0;
    static constexpr auto kAttack = std::chrono::milliseconds(20);
    static constexpr auto kRelease = std::chrono::milliseconds(450);
    static constexpr auto kPeakHold = std::chrono::milliseconds(750);
    static constexpr double kPeakDecayDbPerSecond = 24.0;

    struct Channel {
        double observedDb{kFloorDb};  // last real status sample, never an interpolated value
        double targetDb{kFloorDb};
        double displayDb{kFloorDb};   // presentation envelope, not a measurement
        double peakDb{kFloorDb};      // highest observed sample, held/decayed for presentation
        TimePoint peakHoldUntil{};
        bool valid{false};
    };

    SourceState sourceState() const { return sourceState_; }
    const Channel& channel(std::size_t i) const { return channels_.at(i); }

    void setSourceState(SourceState state, TimePoint now) {
        if (state == sourceState_) return;
        if (state == SourceState::Delayed && sourceState_ == SourceState::Fresh) {
            advance(now);  // freeze at the delayed boundary, not forever as a live reading
        } else {
            resetChannels();  // stale, unavailable, and recovery never reuse old envelopes
        }
        sourceState_ = state;
    }

    void observe(const std::array<double, kChannels>& samples, TimePoint now) {
        if (sourceState_ != SourceState::Fresh) return;
        advance(now);  // finish the previous sample's envelope before changing targets
        for (std::size_t i = 0; i < kChannels; ++i) {
            auto& c = channels_[i];
            if (!std::isfinite(samples[i])) { c = {}; continue; }
            const double db = std::clamp(samples[i], kFloorDb, 0.0);
            if (!c.valid) {
                c.valid = true;
                c.observedDb = c.targetDb = c.displayDb = c.peakDb = db;
                c.peakHoldUntil = now + kPeakHold;
                continue;
            }
            c.observedDb = c.targetDb = db;
            if (db >= c.peakDb) {
                c.peakDb = db;
                c.peakHoldUntil = now + kPeakHold;
            }
        }
        lastFrame_ = now;
    }

    void advance(TimePoint now) {
        if (sourceState_ != SourceState::Fresh) return;
        if (!lastFrame_) { lastFrame_ = now; return; }
        if (now <= *lastFrame_) return;  // monotonic regression/duplicate frame: fail closed
        const double elapsed = std::chrono::duration<double>(now - *lastFrame_).count();
        for (auto& c : channels_) {
            if (!c.valid) continue;
            const double tau = std::chrono::duration<double>(c.targetDb > c.displayDb ? kAttack : kRelease).count();
            c.displayDb = elapsed >= 1.0 ? c.targetDb
                        : c.displayDb + (c.targetDb - c.displayDb) * (1.0 - std::exp(-elapsed / tau));
            if (std::abs(c.displayDb - c.targetDb) < 0.03) c.displayDb = c.targetDb;
            c.displayDb = std::clamp(c.displayDb, kFloorDb, 0.0);
            const auto decayStart = std::max(*lastFrame_, c.peakHoldUntil);
            if (now > decayStart)
                c.peakDb -= kPeakDecayDbPerSecond * std::chrono::duration<double>(now - decayStart).count();
            c.peakDb = std::clamp(std::max(c.peakDb, c.displayDb), kFloorDb, 0.0);
        }
        lastFrame_ = now;
    }

    bool needsFrames() const {
        if (sourceState_ != SourceState::Fresh) return false;
        for (const auto& c : channels_)
            if (c.valid && (std::abs(c.displayDb - c.targetDb) >= 0.03 ||
                            c.peakDb > std::max(c.displayDb, c.targetDb) + 0.03)) return true;
        return false;
    }

private:
    void resetChannels() {
        channels_ = {};
        lastFrame_.reset();
    }
    std::array<Channel, kChannels> channels_{};
    SourceState sourceState_{SourceState::Unavailable};
    std::optional<TimePoint> lastFrame_;
};

}  // namespace lewitt
