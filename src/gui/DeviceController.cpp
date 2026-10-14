#include "DeviceController.hpp"

#include <QColor>
#include <algorithm>
#include <cmath>

#include "core/Policy.hpp"
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
QString hex(Rgb c) {
    return QString("#%1%2%3").arg(c.r, 2, 16, QChar('0')).arg(c.g, 2, 16, QChar('0')).arg(c.b, 2, 16, QChar('0'));
}
Rgb parseHex(const QString& s) {
    QColor c(s);
    return Rgb{std::uint8_t(c.red()), std::uint8_t(c.green()), std::uint8_t(c.blue())};
}
}  // namespace

DeviceWorker::DeviceWorker() = default;

void DeviceWorker::startTimer(int ms) {
    if (!timer_) {
        timer_ = new QTimer(this);
        timer_->setTimerType(Qt::CoarseTimer);
        connect(timer_, &QTimer::timeout, this, &DeviceWorker::poll);
    }
    timer_->start(ms);
}

void DeviceWorker::connectMock() {
    if (session_) session_->disconnect();
    auto m = std::make_unique<MockTransport>();
    mock_ = m.get();
    session_ = std::make_unique<DeviceSession>(std::move(m));
    session_->setSampleRateHint(static_cast<std::uint32_t>(sampleRate_));
    last_.clear();
    report(session_->connect());
    startTimer(40);  // ~25 Hz meters; stopped again on disconnect
    publish();
}

void DeviceWorker::connectUsb() {
    if (session_) session_->disconnect();
    mock_ = nullptr;
    // Real hardware: default options => no detach, no claim, writes refused.
    session_ = std::make_unique<DeviceSession>(std::make_unique<UsbTransport>());
    last_.clear();
    report(session_->connect());
    startTimer(250);
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
    if (session_->state() == ConnectionState::Connected) startTimer(40);
    publish();
}

void DeviceWorker::setSampleRate(int hz) {
    sampleRate_ = hz;
    if (session_) session_->setSampleRateHint(static_cast<std::uint32_t>(hz));
    publish();
}

void DeviceWorker::poll() {
    if (!session_) return;
    if (session_->state() != ConnectionState::Connected) {
        if (timer_) timer_->stop();   // no background activity while disconnected
        publish();
        return;
    }
    if (mock_ && !mock_->autoGainRunning()) {  // animate simulated meters
        phase_ += 0.12;
        Bytes st(0x40, 0);
        auto put = [&](std::size_t o, double db) { wrI16(st, o, doubleToQ88(std::clamp(db, -60.0, 0.0))); };
        put(0x00, -30 + 18 * std::sin(phase_));
        put(0x09, -34 + 14 * std::sin(phase_ * 0.7 + 1));
        put(0x12, -26 + 12 * std::sin(phase_ * 0.9)); put(0x14, -27 + 12 * std::sin(phase_ * 0.9 + 0.3));
        put(0x16, -20 + 10 * std::sin(phase_ * 0.8)); put(0x18, -21 + 10 * std::sin(phase_ * 0.8 + 0.2));
        put(0x1A, -22 + 10 * std::sin(phase_ * 0.8)); put(0x1C, -23 + 10 * std::sin(phase_ * 0.8 + 0.2));
        mock_->setStatusBytes(st);
    }
    auto s = session_->refreshStatus();
    if (!s) report(s);
    // Re-read settings periodically (the original does this every tick): device-side
    // changes (hardware knob, other software) are merged, pending user edits survive.
    if (++pollCount_ % (mock_ ? 10 : 4) == 0) refreshDeviceState();
    publish();
}

void DeviceWorker::refreshDeviceState() {
    if (!session_ || session_->state() != ConnectionState::Connected) return;
    auto s = session_->refreshSettings();
    if (!s) report(s);
}

void DeviceWorker::setDeviceMode(bool macos) {
    if (!session_ || !mock_) return;   // real hardware: never (also refused in DeviceSession)
    auto s = session_->sendTrigger(macos ? Trigger::SwitchToMacOSStreamingMode : Trigger::SwitchToNormalMode);
    if (!s) { report(s); return; }
    // simulate the re-enumeration: device disappears, then returns with the other PID
    restarting_ = true;
    mock_->setPresent(false);
    session_->disconnect();
    if (timer_) timer_->stop();
    publish();
    QTimer::singleShot(1500, this, [this] {
        restarting_ = false;
        if (mock_) { mock_->setPresent(true); report(session_->connect()); startTimer(40); }
        publish();
    });
}

void DeviceWorker::retryPending() {
    if (!session_) return;
    auto c = session_->commit();
    if (c) pendingKeys_.clear(); else report(c);
    publish();
}

void DeviceWorker::discardPending() {
    if (!session_) return;
    session_->discardPending();
    pendingKeys_.clear();
    publish();
}

void DeviceWorker::injectFault(const QString& kind) {
    if (!mock_) return;   // never on real hardware
    if (kind == "timeout") mock_->injectFault(MockTransport::Fault::Timeout);
    else if (kind == "stall") mock_->injectFault(MockTransport::Fault::Stall);
}

void DeviceWorker::simulateExternalChange(const QString& key, const QVariant& v) {
    if (!mock_) return;
    auto cur = SettingsMessage::fromBytes(mock_->settingsBytes());
    if (!cur) return;
    SettingsMessage m = cur.value();
    if (key == "spk.mute") (void)m.setSpeakerMute(v.toBool());
    else if (key == "hp.mute") (void)m.setHeadphoneMute(v.toBool());
    else if (key == "spk.volume") (void)m.setSpeakerVolumeDb(v.toDouble());
    else if (key == "hp.volume") (void)m.setHeadphoneVolumeDb(v.toDouble());
    else if (key == "in1.gain") (void)m.setGainDb(0, v.toDouble());
    else if (key == "in2.gain") (void)m.setGainDb(1, v.toDouble());
    else if (key == "in1.mute") (void)m.setMute(0, v.toBool());
    else return;
    auto b = m.bytes();
    mock_->setSettingsBytes(Bytes(b.begin(), b.end()));
}

void DeviceWorker::publish() {
    QVariantMap v;
    ConnectionState cs = session_ ? session_->state() : ConnectionState::Disconnected;
    emit connectionChanged(stateName(cs),
                           session_ ? QString::fromStdString(session_->info().backend.empty()
                                                                 ? "usb" : session_->info().backend)
                                    : QString(),
                           session_ && session_->writesEnabled(), session_ && session_->isSimulated());
    v["sampleRate"] = sampleRate_;
    v["restarting"] = restarting_;
    if (session_) {
        auto mode = deviceModeFromPid(session_->info().pid);
        v["device.mode"] = mode == DeviceMode::MacOSStreaming ? "MacOSStreaming"
                         : mode == DeviceMode::Normal ? "Normal" : "Unknown";
        v["set.macosStreaming"] = mode == DeviceMode::MacOSStreaming;
    }
    v["dsp.available"] = dspAvailable(static_cast<std::uint32_t>(sampleRate_));
    if (session_ && session_->effectiveSettings()) {
        const auto s = *session_->effectiveSettings();   // user's unconfirmed edits shown on top
        for (int i = 0; i < 2; ++i) {
            QString p = QString("in%1.").arg(i + 1);
            v[p + "gain"] = s.gainDb(i);
            v[p + "gainMax"] = gainLimitDb(i);
            v[p + "mute"] = s.mute(i);
            v[p + "highPass"] = s.highPass(i);
            v[p + "clipguard"] = s.clipguard(i);
            v[p + "preamp"] = int(s.preampRaw(i));
            v[p + "denoise"] = s.denoise(i);
            v[p + "compress"] = s.compress(i);
        }
        v["in1.phantom"] = s.phantom1();
        v["loop.mute"] = s.loopbackMute();
        v["spk.volume"] = s.speakerVolumeDb();
        v["spk.mute"] = s.speakerMute();
        v["hp.volume"] = s.headphoneVolumeDb();
        v["hp.mute"] = s.headphoneMute();
        v["mon.mix"] = s.inputPlaybackMix();
        v["mon.mic"] = s.monitorMicrophone();
        v["mon.inst"] = s.monitorInstrument();
        v["out.dim"] = s.dim();
        v["out.mono"] = s.mono();
        v["set.gainLock"] = s.externalGainLock();
        v["set.mixToSpeakers"] = s.mixToSpeakers();
        v["set.highImpedance"] = s.highImpedanceHeadphone();
        v["set.highVolume"] = s.highVolumeSpeaker();
        v["set.mobileStreaming"] = s.mobileStreaming();
        v["set.ledBrightness"] = int(s.ledBrightness());
        v["set.led.icons"] = hex(s.ledColor(LedElement::Icons));
        v["set.led.mute"] = hex(s.ledColor(LedElement::Mute));
        v["set.led.parameter"] = hex(s.ledColor(LedElement::Parameter));
        v["set.led.metering"] = hex(s.ledColor(LedElement::Metering));
        v["set.led.gainReduction"] = hex(s.ledColor(LedElement::GainReduction));
        v["set.customButton"] = int(s.customButton());
        v["set.customButtonKnown"] = s.customButtonFunction().has_value();
    }
    if (session_ && session_->status()) {
        const auto& st = *session_->status();
        v["in1.level"] = st.input1LevelDb();
        v["in2.level"] = st.input2LevelDb();
        v["loop.levelL"] = st.loopbackLevelDb(0); v["loop.levelR"] = st.loopbackLevelDb(1);
        v["spk.levelL"] = st.speakerLevelDb(0);   v["spk.levelR"] = st.speakerLevelDb(1);
        v["hp.levelL"] = st.headphoneLevelDb(0);  v["hp.levelR"] = st.headphoneLevelDb(1);
        // Auto-gain workflow derived from the status progress / result bits.
        int prog = st.autosetupProgress();
        QString state = autoState_;
        if (autoState_ == "running" && mock_ && !mock_->autoGainRunning() && prog >= 100) state = "finished";
        if (autoState_ == "running" && !mock_ && prog >= 100) state = "finished";
        if (state == "finished" && autoState_ == "running" && inMeasureSession_) {
            report(session_->sendTrigger(Trigger::EndMeasurementSession));
            inMeasureSession_ = false;
        }
        autoState_ = state;
        v["auto.state"] = autoState_;
        v["auto.progress"] = prog;
        v["auto.resultCode"] = int(st.autosetupResultRaw());  // meaning of 1 vs 2 unverified
    }
    for (const auto& k : pendingKeys_) v["pending." + k] = true;
    // Emit only what changed (keeps QML bindings and redraws to the minimum).
    QVariantMap changed;
    for (auto it = v.begin(); it != v.end(); ++it)
        if (!last_.contains(it.key()) || last_[it.key()] != it.value()) changed[it.key()] = it.value();
    for (auto it = last_.begin(); it != last_.end(); ++it)
        if (!v.contains(it.key())) changed[it.key()] = QVariant();   // cleared on disconnect
    last_ = v;
    if (!changed.isEmpty()) emit stateChanged(changed);
}

void DeviceWorker::trigger(const QString& name) {
    if (!session_) return;
    static const QMap<QString, Trigger> map{
        {"autogain1", Trigger::AutoGainMicrophone}, {"autogain2", Trigger::AutoGainInstrument},
        {"autogainBoth", Trigger::AutoGainBoth}};
    if (!map.contains(name)) return;
    // Same bracket the original uses: begin-session word, then the measurement word;
    // the end-session word is sent when the measurement finishes.
    auto b = session_->sendTrigger(Trigger::BeginMeasurementSession);
    if (!b) { report(b); publish(); return; }
    inMeasureSession_ = true;
    auto s = session_->sendTrigger(map[name]);
    if (s) autoState_ = "running"; else { report(s); (void)session_->sendTrigger(Trigger::EndMeasurementSession); inMeasureSession_ = false; }
    publish();
}

void DeviceWorker::setValue(const QString& key, const QVariant& v) {
    if (!session_) return;
    auto parts = key.split('.');
    if (parts.size() < 2) return;
    int in = parts[0] == "in1" ? 0 : parts[0] == "in2" ? 1 : -1;
    const QString g = parts[0], f = parts[1];
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
        } else if (g == "loop" && f == "mute") return s.setLoopbackMute(v.toBool());
        else if (g == "spk") {
            if (f == "volume") return s.setSpeakerVolumeDb(v.toDouble());
            if (f == "mute") return s.setSpeakerMute(v.toBool());
        } else if (g == "hp") {
            if (f == "volume") return s.setHeadphoneVolumeDb(v.toDouble());
            if (f == "mute") return s.setHeadphoneMute(v.toBool());
        } else if (g == "mon") {
            if (f == "mix") return s.setInputPlaybackMix(v.toDouble());
            if (f == "mic") return s.setMonitorMicrophone(v.toBool());
            if (f == "inst") return s.setMonitorInstrument(v.toBool());
        } else if (g == "out") {
            if (f == "dim") return s.setDim(v.toBool());
            if (f == "mono") return s.setMono(v.toBool());
        } else if (g == "set") {
            if (f == "gainLock") return s.setExternalGainLock(v.toBool());
            if (f == "mixToSpeakers") return s.setMixToSpeakers(v.toBool());
            if (f == "customButton") {
                int n = v.toInt();
                if (!isKnownCustomButton(static_cast<std::uint8_t>(n)))
                    return makeError(Errc::InvalidArgument, "unknown custom button function");
                return s.setCustomButtonFunction(static_cast<CustomButtonFunction>(n));
            }
            if (f == "highImpedance") return s.setHighImpedanceHeadphone(v.toBool());
            if (f == "highVolume") return s.setHighVolumeSpeaker(v.toBool());
            if (f == "ledBrightness") return s.setLedBrightness(v.toInt());
            if (f == "led" && parts.size() == 3) {
                static const QMap<QString, LedElement> m{{"icons", LedElement::Icons},
                    {"mute", LedElement::Mute}, {"parameter", LedElement::Parameter},
                    {"metering", LedElement::Metering}, {"gainReduction", LedElement::GainReduction}};
                if (m.contains(parts[2])) return s.setLedColor(m[parts[2]], parseHex(v.toString()));
            }
        }
        return makeError(Errc::NotSupported, "unknown or unsupported control " + key.toStdString());
    };
    auto e = session_->edit(fn);
    if (!e) { report(e); return; }         // invalid value: nothing staged, nothing sent
    if (!session_->dirty()) { session_->discardPending(); publish(); return; }   // no-op edit
    auto c = session_->commit();
    if (c) {
        pendingKeys_.remove(key);
    } else {
        report(c);                           // keep the edit, flag it, let the user retry/discard
        pendingKeys_.insert(key);
    }
    publish();
}

DeviceController::DeviceController(QObject* parent) : QObject(parent), worker_(new DeviceWorker) {
    qRegisterMetaType<QVariantMap>("QVariantMap");
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &DeviceWorker::stateChanged, this, [this](const QVariantMap& v) {
        for (auto it = v.begin(); it != v.end(); ++it) {
            if (it.value().isValid()) values_[it.key()] = it.value(); else values_.remove(it.key());
        }
        emit valuesChanged();
    });
    connect(worker_, &DeviceWorker::connectionChanged, this,
            [this](const QString& s, const QString& b, bool w, bool sim) {
                if (s == connection_ && b == backend_ && w == writable_ && sim == simulated_) return;
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
void DeviceController::setSampleRate(int hz) { QMetaObject::invokeMethod(worker_, "setSampleRate", Q_ARG(int, hz)); }
void DeviceController::flush() { QMetaObject::invokeMethod(worker_, "poll", Qt::BlockingQueuedConnection); }
void DeviceController::setDeviceMode(bool m) { QMetaObject::invokeMethod(worker_, "setDeviceMode", Q_ARG(bool, m)); }
void DeviceController::retryPending() { QMetaObject::invokeMethod(worker_, "retryPending"); }
void DeviceController::discardPending() { QMetaObject::invokeMethod(worker_, "discardPending"); }
void DeviceController::injectFault(const QString& k) { QMetaObject::invokeMethod(worker_, "injectFault", Q_ARG(QString, k)); }
void DeviceController::simulateExternalChange(const QString& k, const QVariant& v) {
    QMetaObject::invokeMethod(worker_, "simulateExternalChange", Q_ARG(QString, k), Q_ARG(QVariant, v));
}
void DeviceController::trigger(const QString& n) {
    QMetaObject::invokeMethod(worker_, "trigger", Q_ARG(QString, n));
}
void DeviceController::setValue(const QString& k, const QVariant& v) {
    QMetaObject::invokeMethod(worker_, "setValue", Q_ARG(QString, k), Q_ARG(QVariant, v));
}
