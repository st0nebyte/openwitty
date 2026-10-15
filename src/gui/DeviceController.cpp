#include "DeviceController.hpp"

#include <QColor>
#include <algorithm>
#include <cmath>

#include "core/Policy.hpp"
#include <algorithm>

#include "core/RetryPolicy.hpp"
#include "transport/UsbDiagnostics.hpp"
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

void DeviceWorker::adoptSession(std::unique_ptr<IDeviceTransport> t) {
    {
        std::lock_guard lk(activeM_);
        active_ = nullptr;               // old transport is about to be destroyed
    }
    session_.reset();
    resetWorkflowState();
    auto ser = std::make_unique<SerializedTransport>(std::move(t));
    serialized_ = ser.get();
    session_ = std::make_unique<DeviceSession>(std::move(ser));
    std::lock_guard lk(activeM_);
    active_ = serialized_;
}

// Nothing belonging to a previous session may leak into the next one (stale "pending"
// flags, a measurement that can no longer finish, a restart that will never complete).
void DeviceWorker::resetWorkflowState() {
    pendingKeys_.clear();
    autoState_ = "idle";
    inMeasureSession_ = false;
    restarting_ = false;
    pollCount_ = 0;
    mock_ = nullptr;
    serialized_ = nullptr;
}

void DeviceWorker::cancelAll() {
    std::lock_guard lk(activeM_);
    if (active_) active_->requestCancel();
}

void DeviceWorker::connectMock() {
    intentionalDisconnect_ = true;
    lostLink_ = false;
    lastTransportError_.clear();
    lastOpenError_ = Errc::Ok;
    endMeasurementSessionBestEffort();
    if (session_) session_->disconnect();
    if (timer_) timer_->stop();
    usbMode_ = false;
    if (scanTimer_) scanTimer_->stop();
    auto m = std::make_unique<MockTransport>();
    auto* raw = m.get();
    adoptSession(std::move(m));      // resets workflow state (and mock_)
    mock_ = raw;
    session_->setSampleRateHint(static_cast<std::uint32_t>(sampleRate_));
    report(session_->connect());
    startTimer(40);  // ~25 Hz meters; stopped again on disconnect
    publish();
}

void DeviceWorker::connectUsb() {
    intentionalDisconnect_ = true;   // the old session ends by our choice, not by link loss
    endMeasurementSessionBestEffort();
    if (session_) session_->disconnect();
    if (timer_) timer_->stop();
    usbMode_ = true;
    backoffMs_ = 1000;
    // Real hardware: read-only options (descriptor-driven interface selection, no detach, no
    // claim, writes refused at transport level; the session policy keeps hardware writes off).
    adoptSession(std::make_unique<UsbTransport>(readOnlyHardwareOptions()));
    intentionalDisconnect_ = false;
    lostLink_ = false;
    lastOpenError_ = Errc::Ok;
    lastTransportError_.clear();
    auto s = session_->connect();
    if (!s) { report(s); noteOpenFailure(s); }
    if (session_->state() == ConnectionState::Connected) startTimer(250);
    else armRescan();                    // absent / not openable: keep looking, cheaply
    publish();
}

void DeviceWorker::armRescan() {
    if (!scanTimer_) {
        scanTimer_ = new QTimer(this);
        scanTimer_->setSingleShot(true);
        connect(scanTimer_, &QTimer::timeout, this, &DeviceWorker::rescan);
    }
    scanTimer_->start(backoffMs_);
}

// Discovery uses sysfs only (no control traffic). A device that appears, or a link that
// was declared dead, triggers one reconnect attempt; failures back off 1 s -> 8 s.
void DeviceWorker::rescan() {
    if (!usbMode_ || !session_) return;
    if (session_->state() == ConnectionState::Connected) return;
    bool present = false;
    for (const auto& c : manager_.scan())
        if (c.isConnect2 && c.appMode) present = true;
    if (!present) { backoffMs_ = 1000; lastOpenError_ = Errc::Ok; armRescan(); publish(); return; }
    // A persistent failure (permissions, kernel-owned, unsupported layout) is not retried until the
    // device list or node permissions change; this keeps a stuck device from being poked forever.
    const QString sig = deviceListSignature();
    if (!shouldRetryOpen(lastOpenError_, sig != scanSignature_)) {
        backoffMs_ = 8000;
        armRescan();
        publish();
        return;
    }
    auto s = session_->reconnect();
    if (s) {
        backoffMs_ = 1000;
        lastOpenError_ = Errc::Ok;
        lastTransportError_.clear();
        startTimer(250);
    } else {
        report(s);
        noteOpenFailure(s);
        backoffMs_ = std::min(backoffMs_ * 2, 8000);
        armRescan();
    }
    publish();
}

// If a measurement bracket is open, close it before the link goes away (only possible
// while connected and writable; a failure is deliberately ignored).
void DeviceWorker::endMeasurementSessionBestEffort() {
    if (inMeasureSession_ && session_ && session_->state() == ConnectionState::Connected)
        (void)session_->sendTrigger(Trigger::EndMeasurementSession);
    inMeasureSession_ = false;
}

void DeviceWorker::disconnectDevice() {
    if (timer_) timer_->stop();
    if (scanTimer_) scanTimer_->stop();
    endMeasurementSessionBestEffort();
    if (session_) session_->disconnect();
    pendingKeys_.clear();
    autoState_ = "idle";
    restarting_ = false;
    intentionalDisconnect_ = true;
    lostLink_ = false;
    lastTransportError_.clear();
    usbMode_ = false;                   // no automatic re-connection after an explicit disconnect
    publish();
}

void DeviceWorker::report(const Status& s) {
    if (!s) {
        emit errorOccurred(QString::fromStdString(s.error().message));
        lastTransportError_ = QString::fromStdString(s.error().message);
        const auto hint = suggestionFor(s.error());
        if (!hint.empty()) lastTransportError_ += "\n" + QString::fromStdString(hint);
    }
}

QString DeviceWorker::deviceListSignature() {
    QString sig;
    for (const auto& c : manager_.scan())
        sig += QString("%1:%2:%3:%4:%5;").arg(c.usb.bus).arg(c.usb.address).arg(c.usb.pid)
                   .arg(QString::fromStdString(c.usb.sysfsPath)).arg(c.usb.nodeWritable);
    return sig;
}

void DeviceWorker::noteOpenFailure(const Status& s) {
    if (s) return;
    lastOpenError_ = s.error().code;
    scanSignature_ = deviceListSignature();
}

// Translates a commitAndVerify() outcome into the existing error-reporting / pending-retry UX.
// A successful USB transfer alone (TransferCompleted/AwaitingConvergence) is never treated as
// success here - only Verified is. Ambiguous and VerificationTimedOut are reported as errors
// (so the existing pending/retry affordance picks them up) but are deliberately NOT retried
// automatically by this code.
void DeviceWorker::reportWrite(const WriteResult& r) {
    lastWriteState_ = r.state;
    switch (r.state) {
        case WriteState::Verified: return;
        case WriteState::Failed: emit errorOccurred(QString::fromStdString(r.error.message)); return;
        case WriteState::Ambiguous:
            emit errorOccurred("device state changed unexpectedly during the write; "
                               "not confirmed - please re-check this setting");
            return;
        case WriteState::VerificationTimedOut:
            emit errorOccurred("write sent, but the device didn't confirm it in time; "
                               "please re-check this setting");
            return;
        default: return;   // Prepared/Submitted/TransferCompleted/AwaitingConvergence: transient
    }
}

void DeviceWorker::simulateUnplug() {
    if (mock_) mock_->injectFault(MockTransport::Fault::Disconnect);
}
void DeviceWorker::simulateReplug() {
    if (!mock_ || !session_) return;
    mock_->setPresent(true);
    report(session_->reconnect());
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
        if (usbMode_) armRescan();    // link lost: look again with backoff
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
    if (session_->state() != ConnectionState::Connected) { publish(); return; }   // handled next tick
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
    // Deliberately a single publish() at the end (matching the pre-6D pattern): commitAndVerify()
    // runs synchronously on the worker thread, so a publish() before it would emit a stateChanged
    // ahead of the eventual errorOccurred on failure/ambiguity, reordering what callers observe
    // first (this broke GuiBackend.DspRestrictionAt96kHz during development). verifying_ is still
    // tracked accurately for its duration, but - because nothing else can run on this thread while
    // commitAndVerify() blocks - it's never externally observable as true with the current
    // synchronous wiring; "Pending verification" becoming genuinely live would need an async
    // (QTimer-driven) PollPolicy instead of a blocking sleeper, which is future work, not done here.
    verifying_ = true;
    auto r = session_->commitAndVerify();
    verifying_ = false;
    if (r.state == WriteState::Verified) pendingKeys_.clear();
    reportWrite(r);
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
    else if (kind == "busy") mock_->injectFault(MockTransport::Fault::Busy);
    else if (kind == "denied") mock_->injectFault(MockTransport::Fault::AccessDenied);
    else if (kind == "short") mock_->injectFault(MockTransport::Fault::ShortRead);
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

// Simulates the physical MUTE-hold gesture (confirmed on real hardware 2026-10-16). Directly
// mutates the mock's backing settings, same as a real hardware button would - the next
// refreshSettings()/poll() picks it up through the existing device-wins merge, same code path as
// any other device-side change. No DeviceSession write is involved.
void DeviceWorker::simulatePanicMute() {
    if (!mock_) return;   // never on real hardware
    mock_->simulatePanicMute();
}

void DeviceWorker::publish() {
    QVariantMap v;
    ConnectionState cs = session_ ? session_->state() : ConnectionState::Disconnected;
    emit connectionChanged(stateName(cs),
                           session_ ? QString::fromStdString(session_->info().backend.empty()
                                                                 ? "usb" : session_->info().backend)
                                    : QString(),
                           session_ && session_->writesEnabled(), session_ && session_->isSimulated());
    // The sample rate comes from the audio stack, which this backend does not read yet: only the
    // simulator has a value. On real hardware the key is absent (the UI shows "Unknown").
    if (session_ && session_->isSimulated() && cs == ConnectionState::Connected) v["sampleRate"] = sampleRate_;
    v["restarting"] = restarting_;
    // Phase 7A connection presentation. One state, derived only from facts the worker knows.
    if (cs == ConnectionState::Connected) { lostLink_ = false; intentionalDisconnect_ = false; }
    else if (prevState_ == ConnectionState::Connected && !intentionalDisconnect_) lostLink_ = true;
    prevState_ = cs;
    QString uiState;
    switch (cs) {
        case ConnectionState::Connected:
            uiState = session_->isSimulated() ? "simulated" : session_->writesEnabled() ? "real-writable" : "real-readonly";
            break;
        case ConnectionState::Connecting: uiState = "connecting"; break;
        case ConnectionState::Error: uiState = lostLink_ ? "connection-lost" : "error"; break;
        case ConnectionState::Disconnected: uiState = lostLink_ ? "connection-lost" : "disconnected"; break;
    }
    v["ui.state"] = uiState;
    v["ui.error"] = (cs == ConnectionState::Connected) ? QString() : lastTransportError_;
    if (session_ && cs == ConnectionState::Connected) {
        const auto& caps = session_->capabilities();
        v["ui.device"] = session_->isSimulated()
            ? QString("Simulated CONNECT 2 (not a real device)")
            : QString("CONNECT 2 · USB %1:%2 · API %3.%4")
                  .arg(session_->info().vid, 4, 16, QChar('0')).arg(session_->info().pid, 4, 16, QChar('0'))
                  .arg(caps.apiMajor).arg(caps.apiMinor);
    }
    v["write.state"] = QString::fromUtf8(writeStateName(lastWriteState_));
    v["write.verifying"] = verifying_;
    // One label distinguishing the states the GUI needs to react to: whether there's a device at
    // all, whether it's simulated, whether writes are actually possible, and whether a write is
    // currently being confirmed. Writable real hardware never appears here while GUI policy keeps
    // allowHardwareWrites off (see DeviceSession's WritePolicy) - that's intentional: this label
    // reflects reality, it doesn't grant anything by itself.
    QString uiStatus;
    if (verifying_) uiStatus = "Pending verification";
    else if (!session_ || cs == ConnectionState::Disconnected) uiStatus = "Disconnected";
    else if (cs == ConnectionState::Connecting) uiStatus = "Connecting";
    else if (cs == ConnectionState::Error) uiStatus = "Hardware error";
    else if (session_->isSimulated()) uiStatus = "Mock mode";
    else if (session_->writesEnabled()) uiStatus = "Connected with authorized controls";
    else uiStatus = "Connected read-only";
    v["ui.status"] = uiStatus;
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
        // Purely derived display state, not a protocol concept: real hardware has no dedicated
        // "Panic Mute" flag (confirmed 2026-10-16, docs/HARDWARE_TEST_RESULTS.md) - holding MUTE
        // just sets these same four fields. True exactly when all four happen to be muted, however
        // that came about (Panic Mute or four individual presses) - this never writes anything.
        v["mute.allFour"] = s.mute(0) && s.mute(1) && s.speakerMute() && s.headphoneMute();
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
    // Single publish() at the end, same reasoning as retryPending() above.
    verifying_ = true;
    auto r = session_->commitAndVerify();
    verifying_ = false;
    if (r.state == WriteState::Verified) {
        pendingKeys_.remove(key);
    } else {
        reportWrite(r);                      // keep the edit, flag it, let the user retry/discard
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
    worker_->cancelAll();    // wake/abort any in-flight request before asking the worker to stop
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
void DeviceController::simulatePanicMute() { QMetaObject::invokeMethod(worker_, "simulatePanicMute"); }
void DeviceController::trigger(const QString& n) {
    QMetaObject::invokeMethod(worker_, "trigger", Q_ARG(QString, n));
}
void DeviceController::setValue(const QString& k, const QVariant& v) {
    QMetaObject::invokeMethod(worker_, "setValue", Q_ARG(QString, k), Q_ARG(QVariant, v));
}
