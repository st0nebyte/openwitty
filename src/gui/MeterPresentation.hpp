#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <array>

#include "MeterDynamics.hpp"

// Eight persistent QObject channels: QML bindings observe only the bar that changed.
class MeterChannel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double displayDb READ displayDb NOTIFY displayDbChanged)
    Q_PROPERTY(double peakDb READ peakDb NOTIFY peakDbChanged)
    Q_PROPERTY(double observedDb READ observedDb NOTIFY observedDbChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY validChanged)
public:
    explicit MeterChannel(QObject* parent = nullptr) : QObject(parent) {}
    double displayDb() const { return displayDb_; }
    double peakDb() const { return peakDb_; }
    double observedDb() const { return observedDb_; }
    bool valid() const { return valid_; }
    void apply(const lewitt::MeterDynamics::Channel& state);
signals:
    void displayDbChanged();
    void peakDbChanged();
    void observedDbChanged();
    void validChanged();
private:
    double displayDb_{lewitt::MeterDynamics::kFloorDb};
    double peakDb_{lewitt::MeterDynamics::kFloorDb};
    double observedDb_{lewitt::MeterDynamics::kFloorDb};
    bool valid_{false};
};

class MeterPresentation final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString freshness READ freshness NOTIFY freshnessChanged)
    Q_PROPERTY(bool needsFrames READ needsFrames NOTIFY needsFramesChanged)
public:
    explicit MeterPresentation(QObject* parent = nullptr);
    QString freshness() const { return freshness_; }
    bool needsFrames() const { return needsFrames_; }
    Q_INVOKABLE QObject* channel(int index) const;
    Q_INVOKABLE void advanceFrame();  // one shared Qt Quick frame callback, never performs I/O
    void observeSample(const QVariantList& levels, qint64 monotonicNs);
    void setSourceState(const QString& freshness, bool connected);
    void noteFrameSwapped();  // opt-in, payload-free performance summary
signals:
    void freshnessChanged();
    void needsFramesChanged();
private:
    void syncFromModel();
    std::array<MeterChannel*, lewitt::MeterDynamics::kChannels> channels_{};
    lewitt::MeterDynamics model_;
    QString freshness_{"unavailable"};
    bool needsFrames_{false};
    bool connected_{false};
    qint64 lastSampleNs_{0};
    bool traceFrames_{false};
    qint64 lastSwapNs_{0};
    std::array<qint64, 512> frameGapsUs_{};
    std::size_t frameGapCount_{0};
};
