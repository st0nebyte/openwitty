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
#include "core/DeviceSession.hpp"
#include "transport/SerializedTransport.hpp"
#include "transport/MockTransport.hpp"

class DeviceWorker : public QObject {
    Q_OBJECT
public:
    DeviceWorker();
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
signals:
    void stateChanged(const QVariantMap& changed);  // only keys whose value changed
    void connectionChanged(const QString& state, const QString& backend, bool writable, bool simulated);
    void errorOccurred(const QString& message);
public:
    // Thread-safe, non-blocking: abort the in-flight transport request (used on shutdown).
    void cancelAll();
private:
    void publish();
    void armRescan();
    void adoptSession(std::unique_ptr<lewitt::IDeviceTransport> t);
    void refreshDeviceState();
    void report(const lewitt::Status& s);
    void startTimer(int ms);
    std::unique_ptr<lewitt::DeviceSession> session_;
    lewitt::MockTransport* mock_{nullptr};
    QTimer* timer_{nullptr};
    QVariantMap last_;
    double phase_{0};
    int sampleRate_{48000};
    int pollCount_{0};
    QSet<QString> pendingKeys_;
    lewitt::SerializedTransport* serialized_{nullptr};
    // cancelAll() runs on the GUI thread; the pointer is only dereferenced under this lock and
    // is cleared (under the same lock) before the transport it points to is destroyed.
    std::mutex activeM_;
    lewitt::IDeviceTransport* active_{nullptr};
    void resetWorkflowState();
    void endMeasurementSessionBestEffort();
    lewitt::DeviceManager manager_;
    QTimer* scanTimer_{nullptr};
    bool usbMode_{false};
    int backoffMs_{1000};
    QString autoState_{"idle"};
    bool inMeasureSession_{false};
    bool restarting_{false};  // idle | running | finished
};

class DeviceController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString connection READ connection NOTIFY connectionChanged)
    Q_PROPERTY(QString backend READ backend NOTIFY connectionChanged)
    Q_PROPERTY(bool writable READ writable NOTIFY connectionChanged)
    Q_PROPERTY(bool simulated READ simulated NOTIFY connectionChanged)
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit DeviceController(QObject* parent = nullptr);
    ~DeviceController() override;
    QString connection() const { return connection_; }
    QString backend() const { return backend_; }
    bool writable() const { return writable_; }
    bool simulated() const { return simulated_; }
    QVariantMap values() const { return values_; }
    QString lastError() const { return lastError_; }

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
    // Synchronous test hook: blocks until the worker has processed queued calls.
    Q_INVOKABLE void flush();
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
};
