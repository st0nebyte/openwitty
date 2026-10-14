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
