#include "MeterPresentation.hpp"

#include <QByteArray>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
using Clock = lewitt::MeterDynamics::Clock;
qint64 nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}
lewitt::MeterDynamics::SourceState parseState(const QString& s) {
    using State = lewitt::MeterDynamics::SourceState;
    if (s == "fresh") return State::Fresh;
    if (s == "delayed") return State::Delayed;
    if (s == "stale") return State::Stale;
    return State::Unavailable;
}
}  // namespace

void MeterChannel::apply(const lewitt::MeterDynamics::Channel& state) {
    if (valid_ != state.valid) { valid_ = state.valid; emit validChanged(); }
    if (std::abs(displayDb_ - state.displayDb) >= 0.05) {
        displayDb_ = state.displayDb; emit displayDbChanged();
    }
    if (std::abs(peakDb_ - state.peakDb) >= 0.05) {
        peakDb_ = state.peakDb; emit peakDbChanged();
    }
    if (observedDb_ != state.observedDb) {
        observedDb_ = state.observedDb; emit observedDbChanged();
    }
}

MeterPresentation::MeterPresentation(QObject* parent) : QObject(parent) {
    traceFrames_ = qEnvironmentVariableIntValue("OPENWITTY_TRACE_METER_FRAMES") > 0;
    for (auto& channel : channels_) channel = new MeterChannel(this);
}

QObject* MeterPresentation::channel(int index) const {
    return index >= 0 && index < static_cast<int>(channels_.size()) ? channels_[index] : nullptr;
}

void MeterPresentation::setSourceState(const QString& freshness, bool connected) {
    const QString next = connected ? freshness : QStringLiteral("unavailable");
    const bool linkLost = connected_ && !connected;
    connected_ = connected;
    if (linkLost) lastSampleNs_ = 0;
    if (next == freshness_ && !linkLost) return;
    freshness_ = next;
    model_.setSourceState(parseState(freshness_), Clock::now());
    syncFromModel();
    emit freshnessChanged();
}

void MeterPresentation::observeSample(const QVariantList& levels, qint64 monotonicNs) {
    if (!connected_ || levels.size() != static_cast<int>(lewitt::MeterDynamics::kChannels) ||
        monotonicNs <= lastSampleNs_) return;
    lastSampleNs_ = monotonicNs;
    std::array<double, lewitt::MeterDynamics::kChannels> samples{};
    for (std::size_t i = 0; i < samples.size(); ++i) {
        bool valid = false;
        const double value = levels[static_cast<qsizetype>(i)].toDouble(&valid);
        samples[i] = valid ? value : std::numeric_limits<double>::quiet_NaN();
    }
    // Preserve the acquisition timestamp for ordering, but start the GUI envelope at
    // receipt time: a queued sample can arrive after a frame has already advanced.
    model_.observe(samples, Clock::now());
    syncFromModel();
}

void MeterPresentation::advanceFrame() {
    if (!needsFrames_) return;
    model_.advance(Clock::now());
    syncFromModel();
}

void MeterPresentation::syncFromModel() {
    for (std::size_t i = 0; i < channels_.size(); ++i) channels_[i]->apply(model_.channel(i));
    const bool next = model_.needsFrames();
    if (needsFrames_ != next) {
        needsFrames_ = next;
        if (!next) lastSwapNs_ = 0;
        emit needsFramesChanged();
    }
}

void MeterPresentation::noteFrameSwapped() {
    if (!traceFrames_ || !needsFrames_) { lastSwapNs_ = 0; return; }
    const qint64 now = nowNs();
    if (lastSwapNs_ && frameGapCount_ < frameGapsUs_.size())
        frameGapsUs_[frameGapCount_++] = (now - lastSwapNs_) / 1000;
    lastSwapNs_ = now;
    if (frameGapCount_ < 240) return;  // ~4 s at 60 FPS; no per-frame logging
    auto sorted = frameGapsUs_;
    std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(frameGapCount_));
    qint64 sum = 0;
    for (std::size_t i = 0; i < frameGapCount_; ++i) sum += frameGapsUs_[i];
    const double meanMs = static_cast<double>(sum) / (frameGapCount_ * 1000.0);
    const double medianMs = sorted[(frameGapCount_ - 1) / 2] / 1000.0;
    const double p95Ms = sorted[static_cast<std::size_t>(0.95 * (frameGapCount_ - 1))] / 1000.0;
    const double p99Ms = sorted[static_cast<std::size_t>(0.99 * (frameGapCount_ - 1))] / 1000.0;
    std::fprintf(stderr, "meter_frames samples=%zu mean_gap_ms=%.2f median_gap_ms=%.2f p95_gap_ms=%.2f p99_gap_ms=%.2f median_fps=%.1f\n",
                 frameGapCount_, meanMs, medianMs, p95Ms, p99Ms, medianMs > 0 ? 1000.0 / medianMs : 0.0);
    frameGapCount_ = 0;
}
