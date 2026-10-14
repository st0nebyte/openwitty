#include "DeviceController.hpp"

#include <cmath>

#include "transport/UsbTransport.hpp"

using namespace lewitt;

namespace {
QString stateName(ConnectionState s) {
    switch (s) {
        case ConnectionState::Disconnected: return "Disconnected";
        case ConnectionState::Connecting: return "Connecting";
        case ConnectionState::Connected: return "Connected";
        case ConnectionState::Error: return "Error";
    }
    return "Disconnected";
}
}  // namespace

DeviceWorker::DeviceWorker() = default;

void DeviceWorker::connectMock() {
    if (session_) session_->disconnect();
    auto m = std::make_unique<MockTransport>();
    mock_ = m.get();
    session_ = std::make_unique<DeviceSession>(std::move(m));
    report(session_->connect());
    if (!timer_) {
        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, &DeviceWorker::poll);
    }
    timer_->start(50);
    publish();
}

void DeviceWorker::connectUsb() {
    if (session_) session_->disconnect();
    mock_ = nullptr;
    // Real hardware: default options => no detach, no claim, writes refused.
    session_ = std::make_unique<DeviceSession>(std::make_unique<UsbTransport>());
    report(session_->connect());
    if (!timer_) {
        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, &DeviceWorker::poll);
    }
    timer_->start(250);
    publish();
}

void DeviceWorker::disconnectDevice() {
    if (timer_) timer_->stop();
    if (session_) session_->disconnect();
    publish();
}

void DeviceWorker::report(const Status& s) {
    if (!s) emit errorOccurred(QString::fromStdString(s.error().message));
}

void DeviceWorker::simulateUnplug() {
    if (mock_) mock_->injectFault(MockTransport::Fault::Disconnect);
}
void DeviceWorker::simulateReplug() {
    if (!mock_ || !session_) return;
    mock_->setPresent(true);
    report(session_->connect());
    publish();
}

void DeviceWorker::poll() {
    if (!session_) return;
    if (session_->state() != ConnectionState::Connected) {
        publish();
        return;
    }
    if (mock_) {  // animate simulated meters
        phase_ += 0.15;
        Bytes st(0x40, 0);
        auto put = [&](std::size_t o, double db) {
            wrI16(st, o, doubleToQ88(db));
        };
        double a = -30.0 + 20.0 * std::sin(phase_);
        double b = -35.0 + 15.0 * std::sin(phase_ * 0.7 + 1.0);
        put(0x00, a);
        put(0x09, b);
        mock_->setStatusBytes(st);
    }
    auto s = session_->refreshStatus();
    if (!s) report(s);
    publish();
}

void DeviceWorker::publish() {
    QVariantMap v;
    ConnectionState cs = session_ ? session_->state() : ConnectionState::Disconnected;
    emit connectionChanged(stateName(cs),
                           session_ ? QString::fromStdString(session_->info().backend.empty()
                                                                 ? "usb" : session_->info().backend)
                                    : QString(),
                           session_ && session_->writesEnabled(), session_ && session_->isSimulated());
    if (session_ && session_->settings()) {
        const auto& s = *session_->settings();
        for (int i = 0; i < 2; ++i) {
            QString p = QString("in%1.").arg(i + 1);
            v[p + "gain"] = s.gainDb(i);
            v[p + "mute"] = s.mute(i);
            v[p + "highPass"] = s.highPass(i);
            v[p + "clipguard"] = s.clipguard(i);
            v[p + "preamp"] = int(s.preampRaw(i));
            v[p + "denoise"] = s.denoise(i);
            v[p + "compress"] = s.compress(i);
        }
        v["in1.phantom"] = s.phantom1();
        v["out.volumeA"] = s.volumeADb();
        v["out.volumeB"] = s.volumeBDb();
        v["out.dim"] = s.dim();
        v["out.mono"] = s.mono();
        v["out.balance"] = s.balance();
    }
    if (session_ && session_->status()) {
        v["in1.level"] = session_->status()->input1LevelDb();
        v["in2.level"] = session_->status()->input2LevelDb();
    }
    emit stateChanged(v);
}

void DeviceWorker::trigger(const QString& name) {
    if (!session_) return;
    static const QMap<QString, Trigger> map{
        {"autogain1", Trigger::AutoGainInput1}, {"autogain2", Trigger::AutoGainInput2},
        {"autogainBoth", Trigger::AutoGainBoth}};
    if (!map.contains(name)) return;
    report(session_->sendTrigger(map[name]));
}

void DeviceWorker::setValue(const QString& key, const QVariant& v) {
    if (!session_) return;
    auto parts = key.split('.');
    if (parts.size() != 2) return;
    int in = parts[0] == "in1" ? 0 : parts[0] == "in2" ? 1 : -1;
    const QString f = parts[1];
    auto fn = [&](SettingsMessage& s) -> Status {
        if (in >= 0) {
            if (f == "gain") return s.setGainDb(in, v.toDouble());
            if (f == "mute") return s.setMute(in, v.toBool());
            if (f == "highPass") return s.setHighPass(in, v.toBool());
            if (f == "clipguard") return s.setClipguard(in, v.toBool());
            if (f == "preamp") return s.setPreamp(in, static_cast<PreampType>(v.toInt()));
            if (f == "denoise") return s.setDenoise(in, v.toBool());
            if (f == "compress") return s.setCompress(in, v.toBool());
            if (f == "phantom" && in == 0) return s.setPhantom1(v.toBool());
        } else if (parts[0] == "out") {
            if (f == "volumeA") return s.setVolumeADb(v.toDouble());
            if (f == "volumeB") return s.setVolumeBDb(v.toDouble());
            if (f == "dim") return s.setDim(v.toBool());
            if (f == "mono") return s.setMono(v.toBool());
        }
        return makeError(Errc::NotSupported, "unknown control " + key.toStdString());
    };
    auto e = session_->edit(fn);
    if (!e) { report(e); return; }
    report(session_->commit());
    publish();
}

DeviceController::DeviceController(QObject* parent) : QObject(parent), worker_(new DeviceWorker) {
    qRegisterMetaType<QVariantMap>("QVariantMap");
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &DeviceWorker::stateChanged, this, [this](const QVariantMap& v) {
        for (auto it = v.begin(); it != v.end(); ++it) values_[it.key()] = it.value();
        emit valuesChanged();
    });
    connect(worker_, &DeviceWorker::connectionChanged, this,
            [this](const QString& s, const QString& b, bool w, bool sim) {
                connection_ = s; backend_ = b; writable_ = w; simulated_ = sim;
                emit connectionChanged();
            });
    connect(worker_, &DeviceWorker::errorOccurred, this, [this](const QString& m) {
        lastError_ = m;
        emit errorChanged();
    });
    thread_.start();
}

DeviceController::~DeviceController() {
    QMetaObject::invokeMethod(worker_, "disconnectDevice", Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
}

void DeviceController::connectMock() { QMetaObject::invokeMethod(worker_, "connectMock"); }
void DeviceController::connectUsb() { QMetaObject::invokeMethod(worker_, "connectUsb"); }
void DeviceController::disconnectDevice() { QMetaObject::invokeMethod(worker_, "disconnectDevice"); }
void DeviceController::simulateUnplug() { QMetaObject::invokeMethod(worker_, "simulateUnplug"); }
void DeviceController::simulateReplug() { QMetaObject::invokeMethod(worker_, "simulateReplug"); }
void DeviceController::trigger(const QString& n) {
    QMetaObject::invokeMethod(worker_, "trigger", Q_ARG(QString, n));
}
void DeviceController::setValue(const QString& k, const QVariant& v) {
    QMetaObject::invokeMethod(worker_, "setValue", Q_ARG(QString, k), Q_ARG(QVariant, v));
}
