// GUI <-> backend integration (simulated device, offscreen). Not hardware tests.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>

#include "DeviceController.hpp"

namespace {
// Tiny QSignalSpy replacement (Qt6Test is not required).
struct Count {
    int n{0};
    template <class Obj, class Sig> Count(Obj* o, Sig s) { QObject::connect(o, s, o, [this] { ++n; }); }
    int count() const { return n; }
};
QGuiApplication* app() {
    static int argc = 1;
    static char name[] = "lewitt_gui_tests";
    static char* argv[] = {name, nullptr};
    // Intentionally leaked: avoids static-destruction order problems at process exit.
    static QGuiApplication* a = new QGuiApplication(argc, argv);
    return a;
}
// Pump events until the predicate holds (worker thread results are queued).
template <class F> bool waitFor(F pred, int ms = 3000) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (pred()) return true;
    }
    return pred();
}
}  // namespace

TEST(GuiBackend, MockConnectsAndPublishesState) {
    app();
    DeviceController c;
    EXPECT_EQ(c.connection(), "Disconnected");
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.connection() == "Connected"; }));
    EXPECT_TRUE(c.simulated());
    EXPECT_TRUE(c.writable());
    ASSERT_TRUE(waitFor([&] { return c.values().contains("in1.gain"); }));
    EXPECT_DOUBLE_EQ(c.values()["in1.gain"].toDouble(), 25.0);
    EXPECT_DOUBLE_EQ(c.values()["in1.gainMax"].toDouble(), 72.0);
    EXPECT_DOUBLE_EQ(c.values()["in2.gainMax"].toDouble(), 60.0);
}

TEST(GuiBackend, SetValueRoundTripsThroughSession) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("in1.gain"); }));
    c.setValue("in1.gain", 33.5);
    c.setValue("spk.mute", true);
    c.setValue("hp.volume", -12.0);
    ASSERT_TRUE(waitFor([&] {
        return c.values()["in1.gain"].toDouble() == 33.5 && c.values()["spk.mute"].toBool() &&
               c.values()["hp.volume"].toDouble() == -12.0;
    }));
    EXPECT_FALSE(c.values()["hp.mute"].toBool());   // independent from speaker mute
}

TEST(GuiBackend, InvalidValueReportsErrorAndKeepsState) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("in2.gain"); }));
    Count spy(&c, &DeviceController::errorChanged);
    c.setValue("in2.gain", 70.0);   // instrument limit is 60 dB
    ASSERT_TRUE(waitFor([&] { return spy.count() > 0; }));
    EXPECT_NE(c.lastError().indexOf("60"), -1);
    EXPECT_DOUBLE_EQ(c.values()["in2.gain"].toDouble(), 25.0);
}

TEST(GuiBackend, DspRestrictionAt96kHz) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("in1.denoise"); }));
    c.setSampleRate(96000);
    ASSERT_TRUE(waitFor([&] { return c.values()["dsp.available"] == false; }));
    Count spy(&c, &DeviceController::errorChanged);
    c.setValue("in1.denoise", true);
    ASSERT_TRUE(waitFor([&] { return spy.count() > 0; }));
    EXPECT_FALSE(c.values()["in1.denoise"].toBool());
    c.setSampleRate(48000);
    ASSERT_TRUE(waitFor([&] { return c.values()["dsp.available"] == true; }));
    c.setValue("in1.denoise", true);
    ASSERT_TRUE(waitFor([&] { return c.values()["in1.denoise"].toBool(); }));
}

TEST(GuiBackend, MonitorSelectionNeverEmpty) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("mon.mic"); }));
    // mock default: microphone only
    EXPECT_TRUE(c.values()["mon.mic"].toBool());
    EXPECT_FALSE(c.values()["mon.inst"].toBool());
    c.setValue("mon.mic", false);          // would leave nothing selected -> stays
    c.flush();
    c.setValue("mon.inst", true);          // mic only -> both
    ASSERT_TRUE(waitFor([&] { return c.values()["mon.inst"].toBool(); }));
    EXPECT_TRUE(c.values()["mon.mic"].toBool());
    c.setValue("mon.mic", false);          // both -> instrument only
    ASSERT_TRUE(waitFor([&] { return !c.values()["mon.mic"].toBool(); }));
    EXPECT_TRUE(c.values()["mon.inst"].toBool());
}

TEST(GuiBackend, UnplugReplugAndClean_shutdown) {
    app();
    {
        DeviceController c;
        c.connectMock();
        ASSERT_TRUE(waitFor([&] { return c.connection() == "Connected"; }));
        c.simulateUnplug();
        ASSERT_TRUE(waitFor([&] { return c.connection() == "Disconnected"; }));
        c.simulateReplug();
        ASSERT_TRUE(waitFor([&] { return c.connection() == "Connected"; }));
    }   // destructor must join the worker thread without hanging
    SUCCEED();
}

TEST(GuiBackend, AutoGainWorkflowProgresses) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("auto.state"); }));
    c.trigger("autogain1");
    ASSERT_TRUE(waitFor([&] { return c.values()["auto.state"] == "running"; }));
    ASSERT_TRUE(waitFor([&] { return c.values()["auto.state"] == "finished"; }, 6000));
    EXPECT_EQ(c.values()["auto.progress"].toInt(), 100);
}

TEST(GuiBackend, UsbBackendIsReadOnlyAndAbsentDeviceIsReported) {
    app();
    DeviceController c;
    c.connectUsb();   // no device in CI/dev machine without hardware
    ASSERT_TRUE(waitFor([&] { return !c.lastError().isEmpty() || c.connection() == "Connected"; }));
    if (c.connection() != "Connected") {
        EXPECT_FALSE(c.simulated());
        EXPECT_FALSE(c.writable());
    }
    c.setValue("in1.mute", true);   // must never reach hardware
    c.flush();
    EXPECT_FALSE(c.writable());
}

TEST(Qml, MainWindowLoadsWithoutErrors) {
    app();
    DeviceController c;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("device", &c);
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/Main.qml")));
    for (const auto& e : comp.errors()) ADD_FAILURE() << e.toString().toStdString();
    ASSERT_EQ(comp.status(), QQmlComponent::Ready);
    std::unique_ptr<QObject> root(comp.create());
    ASSERT_TRUE(root != nullptr);
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.connection() == "Connected"; }));
    QCoreApplication::processEvents();
    EXPECT_EQ(root->property("interactive").toBool(), true);
    root.reset();
}

TEST(GuiBackend, CustomButtonAndMixToSpeakers) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("set.customButton"); }));
    EXPECT_EQ(c.values()["set.customButton"].toInt(), 14);       // simulated default
    EXPECT_TRUE(c.values()["set.customButtonKnown"].toBool());
    c.setValue("set.customButton", 8);
    ASSERT_TRUE(waitFor([&] { return c.values()["set.customButton"].toInt() == 8; }));
    Count spy(&c, &DeviceController::errorChanged);
    c.setValue("set.customButton", 5);                           // not a UI-offered function
    ASSERT_TRUE(waitFor([&] { return spy.count() > 0; }));
    EXPECT_EQ(c.values()["set.customButton"].toInt(), 8);
    c.setValue("set.mixToSpeakers", true);
    ASSERT_TRUE(waitFor([&] { return c.values()["set.mixToSpeakers"].toBool(); }));
}

TEST(GuiSync, FailedWriteIsKeptAndFlaggedThenRetried) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("in1.gain"); }));
    Count err(&c, &DeviceController::errorChanged);
    c.injectFault("timeout");
    c.setValue("in1.gain", 44.0);
    ASSERT_TRUE(waitFor([&] { return err.count() > 0; }));
    // the user's value stays visible and is marked pending; it was not silently dropped
    ASSERT_TRUE(waitFor([&] { return c.values()["pending.in1.gain"].toBool(); }));
    EXPECT_DOUBLE_EQ(c.values()["in1.gain"].toDouble(), 44.0);
    c.retryPending();
    ASSERT_TRUE(waitFor([&] { return !c.values().contains("pending.in1.gain"); }));
    EXPECT_DOUBLE_EQ(c.values()["in1.gain"].toDouble(), 44.0);
}

TEST(GuiSync, DiscardPendingRestoresDeviceValue) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("in2.gain"); }));
    Count err(&c, &DeviceController::errorChanged);
    c.injectFault("stall");
    c.setValue("in2.gain", 10.0);
    ASSERT_TRUE(waitFor([&] { return err.count() > 0; }));
    ASSERT_TRUE(waitFor([&] { return c.values()["pending.in2.gain"].toBool(); }));
    c.discardPending();
    ASSERT_TRUE(waitFor([&] { return !c.values().contains("pending.in2.gain"); }));
    ASSERT_TRUE(waitFor([&] { return c.values()["in2.gain"].toDouble() == 25.0; }));
}

TEST(GuiSync, ExternalHardwareChangeAppearsWithoutLosingPendingEdit) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("hp.volume"); }));
    Count err(&c, &DeviceController::errorChanged);
    c.injectFault("timeout");
    c.setValue("in1.gain", 50.0);                       // fails -> pending
    ASSERT_TRUE(waitFor([&] { return c.values()["pending.in1.gain"].toBool(); }));
    c.simulateExternalChange("hp.volume", -20.0);       // knob turned on the device
    ASSERT_TRUE(waitFor([&] { return c.values()["hp.volume"].toDouble() == -20.0; }, 4000));
    EXPECT_DOUBLE_EQ(c.values()["in1.gain"].toDouble(), 50.0);   // pending edit survived
    EXPECT_TRUE(c.values()["pending.in1.gain"].toBool());
}

// Panic Mute (MUTE hold), confirmed on real hardware 2026-10-16 (docs/HARDWARE_TEST_RESULTS.md):
// sets exactly the four primary mute fields simultaneously, nothing else. simulatePanicMute()
// mirrors that directly on the mock; the GUI must pick it up through the existing poll/merge path
// like any other device-side change, and must not lose an unrelated pending edit.
TEST(GuiSync, PanicMuteUpdatesAllFourMuteFieldsAndPreservesPendingEdit) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("hp.mute"); }));
    // Start from fully unmuted so the transition is unambiguous.
    c.setValue("in1.mute", false);
    c.setValue("in2.mute", false);
    c.setValue("spk.mute", false);
    c.setValue("hp.mute", false);
    ASSERT_TRUE(waitFor([&] { return c.values()["hp.mute"].toBool() == false; }));
    ASSERT_TRUE(waitFor([&] { return c.values()["mute.allFour"].toBool() == false; }));

    c.injectFault("timeout");
    c.setValue("in1.gain", 61.0);   // fails -> stays pending, must survive what follows
    ASSERT_TRUE(waitFor([&] { return c.values()["pending.in1.gain"].toBool(); }));

    c.simulatePanicMute();
    ASSERT_TRUE(waitFor([&] { return c.values()["mute.allFour"].toBool() == true; }, 4000));
    EXPECT_TRUE(c.values()["in1.mute"].toBool());
    EXPECT_TRUE(c.values()["in2.mute"].toBool());
    EXPECT_TRUE(c.values()["spk.mute"].toBool());
    EXPECT_TRUE(c.values()["hp.mute"].toBool());
    EXPECT_FALSE(c.values()["loop.mute"].toBool())
        << "loopback mute confirmed NOT affected by Panic Mute on real hardware";
    EXPECT_DOUBLE_EQ(c.values()["in1.gain"].toDouble(), 61.0);   // pending edit survived
    EXPECT_TRUE(c.values()["pending.in1.gain"].toBool());
}

TEST(GuiSync, IdenticalValueSendsNoCommand) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("hp.mute"); }));
    c.setValue("hp.mute", true);
    ASSERT_TRUE(waitFor([&] { return c.values()["hp.mute"].toBool(); }));
    Count err(&c, &DeviceController::errorChanged);
    c.setValue("hp.mute", true);                        // same value again
    c.flush();
    EXPECT_EQ(err.count(), 0);
    EXPECT_TRUE(c.values()["hp.mute"].toBool());
}

TEST(GuiBackend, SimulatedModeSwitchRestartsAndPreservesSettings) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("in1.gain"); }));
    c.setValue("in1.gain", 41.0);
    ASSERT_TRUE(waitFor([&] { return c.values()["in1.gain"].toDouble() == 41.0; }));
    EXPECT_EQ(c.values()["device.mode"].toString(), "Normal");
    c.setDeviceMode(true);
    ASSERT_TRUE(waitFor([&] { return c.values()["restarting"].toBool(); }));
    ASSERT_TRUE(waitFor([&] { return c.connection() == "Connected" && !c.values()["restarting"].toBool(); }, 5000));
    ASSERT_TRUE(waitFor([&] { return c.values()["device.mode"].toString() == "MacOSStreaming"; }));
    EXPECT_DOUBLE_EQ(c.values()["in1.gain"].toDouble(), 41.0);      // settings preserved
    EXPECT_TRUE(c.values()["set.macosStreaming"].toBool());
}

TEST(GuiBackend, AutoGainSendsSessionBracket) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("auto.state"); }));
    c.trigger("autogain2");
    ASSERT_TRUE(waitFor([&] { return c.values()["auto.state"] == "finished"; }, 6000));
    EXPECT_EQ(c.values()["auto.progress"].toInt(), 100);
}

TEST(GuiHardening, ShutdownWhileCommandsAreQueued) {
    app();
    QElapsedTimer t; t.start();
    {
        DeviceController c;
        c.connectMock();
        ASSERT_TRUE(waitFor([&] { return c.connection() == "Connected"; }));
        for (int i = 0; i < 300; ++i) c.setValue("in1.gain", double(i % 70));
        c.simulateUnplug();
        c.setSampleRate(96000);
        for (int i = 0; i < 100; ++i) c.setValue("hp.volume", -double(i % 60));
    }   // destructor: cancel, drain, join -- must be prompt and must not crash
    EXPECT_LT(t.elapsed(), 4000);
}

TEST(GuiHardening, BusyAndPermissionErrorsAreReportedAndDoNotDisconnect) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("in1.gain"); }));
    for (const char* kind : {"busy", "denied", "stall"}) {
        Count err(&c, &DeviceController::errorChanged);
        c.injectFault(kind);
        c.setValue("in1.mute", true);
        ASSERT_TRUE(waitFor([&] { return err.count() > 0; })) << kind;
        EXPECT_EQ(c.connection(), "Connected") << kind;
        c.discardPending();
        c.flush();
    }
}

TEST(GuiHardening, UsbModeWithoutDeviceStaysQuietAndSafe) {
    app();
    DeviceController c;
    c.connectUsb();
    ASSERT_TRUE(waitFor([&] { return !c.lastError().isEmpty() || c.connection() == "Connected"; }));
    // whatever the machine has, nothing may have become writable on real hardware
    EXPECT_FALSE(c.writable());
    c.trigger("autogain1");        // must be refused by the backend, not just hidden in the UI
    c.setDeviceMode(true);         // simulation only: ignored on real transports
    c.flush();
    EXPECT_FALSE(c.simulated());
}

// ---- Phase 5E audit: no state may leak between sessions --------------------------------
TEST(GuiAudit, PendingFlagsAndWorkflowResetWhenSessionIsReplaced) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("in1.gain"); }));
    c.injectFault("timeout");              // next write fails -> edit stays pending
    c.setValue("spk.mute", true);
    ASSERT_TRUE(waitFor([&] { return c.values().value("pending.spk.mute").toBool(); }));
    c.connectMock();                        // brand-new simulated device
    ASSERT_TRUE(waitFor([&] { return !c.values().value("pending.spk.mute").toBool(); }));
    EXPECT_FALSE(c.values().value("spk.mute").toBool());   // fresh device state, not the stale edit
    EXPECT_EQ(c.values().value("auto.state").toString(), "idle");
}

TEST(GuiAudit, ExplicitDisconnectClearsPendingAndDoesNotReconnect) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.connection() == "Connected"; }));
    c.disconnectDevice();
    ASSERT_TRUE(waitFor([&] { return c.connection() == "Disconnected"; }));
    c.flush();
    EXPECT_TRUE(waitFor([&] { return !c.values().contains("in1.gain"); }));   // no stale device values
    EXPECT_EQ(c.connection(), "Disconnected");
}

TEST(GuiAudit, EveryQmlWritableKeyIsAcceptedByTheBackend) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("in1.gain"); }));
    Count errors(&c, &DeviceController::errorChanged);
    // keys the QML layer sends (src/gui/qml), with a valid value for each
    const QList<QPair<QString, QVariant>> keys{
        {"in1.gain", 30.0}, {"in2.gain", 20.0}, {"in1.mute", true}, {"in2.mute", true},
        {"in1.highPass", true}, {"in2.highPass", true}, {"in1.clipguard", true}, {"in2.clipguard", true},
        {"in1.preamp", 1}, {"in2.preamp", 2}, {"in1.denoise", true}, {"in2.denoise", true},
        {"in1.compress", true}, {"in2.compress", true}, {"in1.phantom", true},
        {"loop.mute", true}, {"spk.volume", -10.0}, {"spk.mute", true}, {"hp.volume", -12.0}, {"hp.mute", true},
        {"mon.mix", 0.5}, {"mon.mic", true}, {"mon.inst", true}, {"out.dim", true}, {"out.mono", true},
        {"set.gainLock", true}, {"set.mixToSpeakers", true}, {"set.customButton", 8},
        {"set.highImpedance", true}, {"set.highVolume", true}, {"set.ledBrightness", 40},
        {"set.led.icons", "#112233"}, {"set.led.mute", "#112233"}, {"set.led.parameter", "#112233"},
        {"set.led.metering", "#112233"}, {"set.led.gainReduction", "#112233"}};
    for (const auto& [k, v] : keys) c.setValue(k, v);
    c.flush();
    EXPECT_TRUE(waitFor([&] { return c.values()["in1.preamp"].toInt() == 1 && c.values()["set.customButton"].toInt() == 8; }));
    EXPECT_EQ(errors.count(), 0) << "backend rejected a key the UI sends: " << c.lastError().toStdString();
}

// ---- Phase 7A: connection presentation state ------------------------------------------------
TEST(GuiState, SimulatedDeviceIsLabelledSimulated) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().value("ui.state").toString() == "simulated"; }));
    EXPECT_TRUE(c.values().value("ui.device").toString().contains("Simulated"));
    EXPECT_TRUE(c.values().contains("sampleRate"));     // only the simulator has a (fake) rate
}

TEST(GuiState, UnplugIsConnectionLostNotDisconnected) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().value("ui.state").toString() == "simulated"; }));
    c.simulateUnplug();
    ASSERT_TRUE(waitFor([&] { return c.values().value("ui.state").toString() == "connection-lost"; }, 5000));
    EXPECT_FALSE(c.values().contains("ui.device"));
    EXPECT_FALSE(c.values().value("ui.error").toString().isEmpty());   // visible reason
}

TEST(GuiState, ExplicitDisconnectIsPlainDisconnected) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().value("ui.state").toString() == "simulated"; }));
    c.disconnectDevice();
    ASSERT_TRUE(waitFor([&] { return c.values().value("ui.state").toString() == "disconnected"; }));
    EXPECT_FALSE(c.values().contains("sampleRate"));
}

TEST(GuiState, UsbModeNeverClaimsSimulationOrWritability) {
    app();
    DeviceController c;
    c.connectUsb();
    c.flush();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("ui.state"); }));
    const QString st = c.values().value("ui.state").toString();
    EXPECT_NE(st, "simulated");
    EXPECT_NE(st, "real-writable");
    EXPECT_FALSE(c.writable());
    EXPECT_FALSE(c.values().contains("sampleRate")) << "no fabricated sample rate on a non-simulated session";
}
