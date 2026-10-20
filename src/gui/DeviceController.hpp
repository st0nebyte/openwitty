// QObject facade between QML and the Qt-free core. All device I/O happens on a
// dedicated worker thread; the GUI thread only sees queued, diffed state.
#pragma once
#include <QObject>
#include <QThread>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <memory>

#include <atomic>
#include <mutex>

#include "core/DeviceManager.hpp"
#include "core/ReadFreshness.hpp"
#include "core/PollCadence.hpp"
#include "core/DeviceSession.hpp"
#include "transport/SerializedTransport.hpp"
#include "transport/MockTransport.hpp"
#include "MeterPresentation.hpp"

class DeviceWorker : public QObject {
    Q_OBJECT
public:
    explicit DeviceWorker(lewitt::RealPollCadence realCadence = lewitt::kStatus48Hz,
                          int settingsHz = 16);
public slots:
    void connectMock();
    void connectUsb();
    void disconnectDevice();
    void setValue(const QString& key, const QVariant& v);
    void trigger(const QString& name);
    void setSampleRate(int hz);
    void poll();
    void simulateUnplug();
    void simulateReplug();
    void rescan();                       // timer: discovery / reconnect (sysfs only, no USB I/O)
    void setDeviceMode(bool macosStreaming);   // simulation only
    void retryPending();
    void discardPending();
    void injectFault(const QString& kind);                       // simulation only
    void simulateExternalChange(const QString& key, const QVariant& v);  // simulation only
    void simulatePanicMute();                                     // simulation only
signals:
    void stateChanged(const QVariantMap& changed);  // only keys whose value changed
    void meterSample(const QVariantList& levels, qint64 monotonicNs); // observed status data only
    void pollTrace(const QVariantMap& stages);       // opt-in, monotonic timestamps only
    void pollTracePublished(qint64 sequence, qint64 durationUs);
    void connectionChanged(const QString& state, const QString& backend, bool writable, bool simulated);
    void errorOccurred(const QString& message);
public:
    // Thread-safe, non-blocking: abort the in-flight transport request (used on shutdown).
    void cancelAll();
private:
    void publish();
    void publishMeterSample();
    void armRescan();
    void adoptSession(std::unique_ptr<lewitt::IDeviceTransport> t);
    lewitt::Status refreshDeviceState(lewitt::SettingsRefreshTiming* timing = nullptr);
    void report(const lewitt::Status& s);
    void reportWrite(const lewitt::WriteResult& r);
    void startTimer(int ms);
    void startRealTimer();
    void armPreciseStatusTimer();
    std::unique_ptr<lewitt::DeviceSession> session_;
    lewitt::MockTransport* mock_{nullptr};
    QTimer* timer_{nullptr};
    QVariantMap last_;
    double phase_{0};
    int sampleRate_{48000};
    int pollCount_{0};
    lewitt::RealPollCadence realCadence_;
    lewitt::RealPollSchedule realSchedule_;
    lewitt::ReadFreshness freshness_;
    bool traceEnabled_{false};
    qint64 lastPollNs_{0};
    qint64 traceSequence_{0};
    QSet<QString> pendingKeys_;
    lewitt::SerializedTransport* serialized_{nullptr};
    // cancelAll() runs on the GUI thread; the pointer is only dereferenced under this lock and
    // is cleared (under the same lock) before the transport it points to is destroyed.
    std::mutex activeM_;
    lewitt::IDeviceTransport* active_{nullptr};
    void resetWorkflowState();
    void markInitialReads();
    void endMeasurementSessionBestEffort();
    lewitt::DeviceManager manager_;
    QTimer* scanTimer_{nullptr};
    bool usbMode_{false};
    int backoffMs_{1000};
    QString autoState_{"idle"};
    bool inMeasureSession_{false};
    bool restarting_{false};  // idle | running | finished
    // Verification state of the most recent settings write (see core/WriteVerification.hpp).
    // `verifying_` is true only while a commitAndVerify() call is actually in flight on this
    // (worker) thread - never on the GUI thread, so it can't freeze the UI.
    lewitt::WriteState lastWriteState_{lewitt::WriteState::Prepared};
    bool verifying_{false};
    // Phase 7A: connection presentation + persistent-error retry suppression.
    lewitt::ConnectionState prevState_{lewitt::ConnectionState::Disconnected};
    bool lostLink_{false};               // was Connected, then the link dropped (not user-initiated)
    bool intentionalDisconnect_{false};
    QString lastTransportError_;         // last open/read error text incl. next-step suggestion
    lewitt::Errc lastOpenError_{lewitt::Errc::Ok};
    QString scanSignature_;              // device list + node permissions at the last failed open
    QString deviceListSignature();
    void noteOpenFailure(const lewitt::Status& s);
};

class DeviceController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString connection READ connection NOTIFY connectionChanged)
    Q_PROPERTY(QString backend READ backend NOTIFY connectionChanged)
    Q_PROPERTY(bool writable READ writable NOTIFY connectionChanged)
    Q_PROPERTY(bool simulated READ simulated NOTIFY connectionChanged)
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(bool traceEnabled READ traceEnabled CONSTANT)
    Q_PROPERTY(QObject* meters READ meters CONSTANT)
    Q_PROPERTY(bool diagnosticLegacyPolling READ diagnosticLegacyPolling CONSTANT)
    Q_PROPERTY(int settingsPollingHz READ settingsPollingHz CONSTANT)
    Q_PROPERTY(int statusPollingHz READ statusPollingHz CONSTANT)
public:
    explicit DeviceController(QObject* parent = nullptr, bool diagnosticLegacyPolling = false);
    ~DeviceController() override;
    QString connection() const { return connection_; }
    QString backend() const { return backend_; }
    bool writable() const { return writable_; }
    bool simulated() const { return simulated_; }
    QVariantMap values() const { return values_; }
    QString lastError() const { return lastError_; }
    bool traceEnabled() const { return traceEnabled_; }
    bool diagnosticLegacyPolling() const { return diagnosticLegacyPolling_; }
    int settingsPollingHz() const { return settingsPollingHz_; }
    int statusPollingHz() const { return diagnosticLegacyPolling_ ? 4 : 48; }
    MeterPresentation* meters() { return &meters_; }

    Q_INVOKABLE void connectMock();
    Q_INVOKABLE void connectUsb();
    Q_INVOKABLE void disconnectDevice();
    Q_INVOKABLE void setValue(const QString& key, const QVariant& v);
    Q_INVOKABLE void trigger(const QString& name);
    Q_INVOKABLE void setSampleRate(int hz);
    Q_INVOKABLE void simulateUnplug();
    Q_INVOKABLE void simulateReplug();
    Q_INVOKABLE void setDeviceMode(bool macosStreaming);
    Q_INVOKABLE void retryPending();
    Q_INVOKABLE void discardPending();
    Q_INVOKABLE void injectFault(const QString& kind);
    Q_INVOKABLE void simulateExternalChange(const QString& key, const QVariant& v);
    Q_INVOKABLE void simulatePanicMute();
    // Synchronous test hook: blocks until the worker has processed queued calls.
    Q_INVOKABLE void flush();
    Q_INVOKABLE void traceQmlObserved();  // opt-in measurement marker; never touches hardware
    void traceFrameSwapped();
signals:
    void connectionChanged();
    void valuesChanged();
    void errorChanged();
private:
    QThread thread_;
    DeviceWorker* worker_;
    QString connection_{"Disconnected"}, backend_, lastError_;
    bool writable_{false}, simulated_{false};
    QVariantMap values_;
    MeterPresentation meters_{this};
    bool traceEnabled_{false};
    bool diagnosticLegacyPolling_{false};
    int settingsPollingHz_{16};
    int traceLevel_{0};
    qint64 lastLoggedTraceSequence_{0};
    qint64 pendingTraceSequence_{0}, pendingWorkerNs_{0};
    qint64 activeTraceSequence_{0}, activeGuiNs_{0}, activeQmlNs_{0};
};
