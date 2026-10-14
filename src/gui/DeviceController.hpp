// QObject facade between QML and the Qt-free core. All device I/O happens on a
// dedicated worker thread; the GUI thread only sees queued, diffed state.
#pragma once
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVariantMap>
#include <memory>

#include "core/DeviceSession.hpp"
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
signals:
    void stateChanged(const QVariantMap& changed);  // only keys whose value changed
    void connectionChanged(const QString& state, const QString& backend, bool writable, bool simulated);
    void errorOccurred(const QString& message);
private:
    void publish();
    void report(const lewitt::Status& s);
    void startTimer(int ms);
    std::unique_ptr<lewitt::DeviceSession> session_;
    lewitt::MockTransport* mock_{nullptr};
    QTimer* timer_{nullptr};
    QVariantMap last_;
    double phase_{0};
    int sampleRate_{48000};
    QString autoState_{"idle"};  // idle | running | finished
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
