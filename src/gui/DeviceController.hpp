// QObject facade between QML and the Qt-free core. All device I/O happens on a
// dedicated worker thread; the GUI thread only sees queued, diffed state.
#pragma once
#include <QObject>
#include <QThread>
#include <QSet>
#include <QTimer>
#include <QHash>
#include <QVariantMap>
#include <memory>
#include <optional>
#include <chrono>

#include <atomic>
#include <mutex>

#include "core/DeviceManager.hpp"
#include "core/ReadFreshness.hpp"
#include "core/PollCadence.hpp"
#include "core/DeviceSession.hpp"
#include "desktop/DesktopMixer.hpp"
#include "desktop/DesktopVolumeLink.hpp"
#include "transport/SerializedTransport.hpp"
#include "transport/MockTransport.hpp"
#include "MeterPresentation.hpp"

class DeviceWorker : public QObject {
    Q_OBJECT
public:
    explicit DeviceWorker(lewitt::RealPollCadence realCadence = lewitt::kStatus48Hz,
                          int settingsHz = 16);
    ~DeviceWorker() override;
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
    void applyUserConfig();                                       // Settings > Desktop integration changed
    void applyQueuedValue(const QString& key);                    // takes the latest value queued for `key`
    // Phase 10B.1, opt-in (openwitty --hw-check): read-only snapshots through this window's own session, for
    // owner-approved hardware tests. Each does one ordinary settings GET on the session already open (same
    // serialized transport, same control lease); nothing here can stage or send a write.
    void enableHardwareCheck();
    void hwCheckRecordBaseline();        // only while none exists: one read; the baseline comes from it
    void hwCheckReplaceBaseline();       // deliberate owner action: replaces the reference, reported as such
    void hwCheckCompare();
    void reconnectForTesting();          // tests only: the reconnect step of rescan() without sysfs discovery
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
    // Thread-safe (GUI thread): remember the latest requested value for `key`. Returns true if no request for
    // that key was waiting, i.e. the caller must schedule applyQueuedValue(key); otherwise the waiting request
    // simply takes the newer value. Many requests for one control while a write is in flight become one.
    bool queueValue(const QString& key, const QVariant& v);
    void connectTransportForTesting(std::unique_ptr<lewitt::IDeviceTransport> t, lewitt::WritePolicy p,
                                    std::string procRoot = "/proc/asound");
    void refreshRateForTesting() { refreshDeviceRate(true); publish(); }
private:
    void publish();
    void publishMeterSample();
    void armRescan();
    bool refuseUnauthorized(const QString& key);
    void refreshDeviceRate(bool force);
    std::optional<unsigned> deviceRate_;
    int ratePolls_{0};
public:
    std::string rateProcRoot_{"/proc/asound"};   // tests point this at a fixture directory
private:
    void adoptSession(std::unique_ptr<lewitt::IDeviceTransport> t, lewitt::WritePolicy policy = {});
    lewitt::Status refreshDeviceState(lewitt::SettingsRefreshTiming* timing = nullptr);
    void report(const lewitt::Status& s);
    void reportWrite(const lewitt::WriteResult& r);
    // Phase 10B.1: the outcome of one write request, for the control that asked (write.seq/key/outcome/message):
    // verified | unchanged (nothing needed sending) | not-sent | unconfirmed | refused.
    void noteWriteOutcome(const QString& key, const QString& outcome, const QString& message);
    void finishRealWrite(const QString& key, const lewitt::WriteResult& r);
    lewitt::PollPolicy guiPollPolicy() const;
    quint64 writeSeq_{0};
    QString writeKey_, writeOutcome_, writeMessage_;
    // A real-device write that may have been sent but was not confirmed stops all further GUI writes in this
    // session (no automatic or queued follow-up SET); only a new session (permission off/on) clears it.
    bool writeLocked_{false};
    QString writeLockReason_;
    std::mutex queuedM_;
    QHash<QString, QVariant> queued_;
    bool hwCheck_{false};
    std::optional<lewitt::SettingsMessage> hwBaseline_;
    QString hwReport_;
    int hwBaselineReplacements_{0};
    QString hwLastWait_;
    void hwCheckLog(const QString& line);
    // Phase 10B.2: called with the result of a read that just happened (connect, reconnect, settings refresh).
    // Records the baseline only from a successful read and only if none exists; never replaces one.
    void hwCheckAfterRead(const lewitt::Status& read, const char* where);
    void attemptReconnect();             // rescan()'s reconnect step (also after a hand-over)
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
    // Opening the window while openwitty-volumed holds the device: the first attempts find the control
    // lease taken while the service is handing it over. For a short grace period that is presented as
    // "connecting (taking over from the background service)" and retried quickly, not as an error.
    bool inHandover(const lewitt::Status& s);
    std::optional<std::chrono::steady_clock::time_point> handoverSince_;
    bool handingOver_{false};
    // Desktop volume mirroring hosted in the GUI while it owns the device (the background
    // openwitty-volumed yields to the GUI). Native mode only (never a device write); on a real device
    // when the user setting "desktop sync" is on. The link refers to session_, so it is always
    // dropped before session_ changes.
    void serviceDesktopLink();
    void dropDesktopLink();
    std::unique_ptr<lewitt::IDesktopMixer> mixer_;
    std::unique_ptr<lewitt::DesktopVolumeLink> link_;
    bool desktopLinkWanted_{false};
    std::chrono::steady_clock::time_point nextMixerOpen_{};
    QString desktopLinkState_{"off"};   // off | waiting | active
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
    // How long "control in use" counts as a hand-over in progress (default 8 s; tests use 0 or short values).
    static void setHandoverGrace(std::chrono::milliseconds grace);
    static std::chrono::milliseconds handoverGrace();
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
    Q_INVOKABLE void applyUserConfig();
    // Opt-in hardware-test snapshots (openwitty --hw-check); read-only, see DeviceWorker.
    void enableHardwareCheck();
    Q_INVOKABLE void hwCheckRecordBaseline();
    Q_INVOKABLE void hwCheckReplaceBaseline();
    Q_INVOKABLE void hwCheckCompare();
    void reconnectForTesting();          // tests only
    // Synchronous test hook: blocks until the worker has processed queued calls.
    Q_INVOKABLE void flush();
    // Tests only, never from QML: see DeviceWorker::connectTransportForTesting().
    void connectTransportForTesting(std::unique_ptr<lewitt::IDeviceTransport> t, lewitt::WritePolicy p,
                                    std::string procRoot = "/proc/asound");
    void refreshSampleRateForTesting();
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
