// GUI <-> backend integration (simulated device, offscreen). Not hardware tests.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <filesystem>
#include <fstream>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cmath>
#include <functional>
#include <optional>

#include "DeviceController.hpp"
#include "IntegrationSettings.hpp"
#include "core/UserConfig.hpp"
#include "transport/ControlLease.hpp"

namespace {
// These offscreen tests must not open a real CONNECT 2 even when one is attached.
// A private test runtime directory and held lease make connectUsb() fail before USB I/O.
struct BlockRealUsb {
    QTemporaryDir runtime;
    QByteArray previous{qgetenv("XDG_RUNTIME_DIR")};
    bool hadPrevious{qEnvironmentVariableIsSet("XDG_RUNTIME_DIR")};
    std::optional<lewitt::ControlLease> lease;
    BlockRealUsb() {
        if (!runtime.isValid()) return;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
        auto acquired = lewitt::ControlLease::acquireForCurrentSession();
        if (acquired) lease.emplace(std::move(acquired.value()));
    }
    ~BlockRealUsb() {
        lease.reset();
        if (hadPrevious) qputenv("XDG_RUNTIME_DIR", previous);
        else qunsetenv("XDG_RUNTIME_DIR");
    }
    bool ready() const { return runtime.isValid() && lease.has_value(); }
};
// Tiny QSignalSpy replacement (Qt6Test is not required).
struct Count {
    int n{0};
    template <class Obj, class Sig> Count(Obj* o, Sig s) { QObject::connect(o, s, o, [this] { ++n; }); }
    int count() const { return n; }
};
QGuiApplication* app() {
    // Never read or write the developer's real OpenWitty settings from the tests.
    static QTemporaryDir* configHome = [] {
        auto* d = new QTemporaryDir();
        qputenv("XDG_CONFIG_HOME", d->path().toUtf8());
        return d;
    }();
    (void)configHome;
    static int argc = 1;
    static char name[] = "lewitt_gui_tests";
    static char* argv[] = {name, nullptr};
    // Intentionally leaked: avoids static-destruction order problems at process exit.
    static QGuiApplication* a = [] {
        // A held lease is reported at once in these tests; the hand-over grace has its own tests.
        DeviceController::setHandoverGrace(std::chrono::milliseconds(0));
        return new QGuiApplication(argc, argv);
    }();
    return a;
}
struct HandoverGrace {
    explicit HandoverGrace(std::chrono::milliseconds g) { DeviceController::setHandoverGrace(g); }
    ~HandoverGrace() { DeviceController::setHandoverGrace(std::chrono::milliseconds(0)); }
};
// Settings backend for QML tests: no systemctl, no real ALSA/device access.
IntegrationSettings* fakeIntegration() {
    static IntegrationSettings* s = new IntegrationSettings(
        nullptr, [](const QString&, const QStringList&, QString* out) { if (out) *out = "LoadState=not-found\n"; return 0; },
        [] { return std::optional<lewitt::AudioSetupStatus>(); });
    return s;
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

TEST(GuiBackend, NormalGuiUsesValidatedRatesAndMockRemainsUnchanged) {
    app();
    DeviceController normal;
    EXPECT_FALSE(normal.diagnosticLegacyPolling());
    EXPECT_EQ(normal.statusPollingHz(), 48);
    EXPECT_EQ(normal.settingsPollingHz(), 16);
    normal.connectMock();
    ASSERT_TRUE(waitFor([&] { return normal.connection() == "Connected"; }));
    EXPECT_TRUE(normal.simulated());
    EXPECT_TRUE(normal.writable()); // existing mock-only behavior
    ASSERT_TRUE(waitFor([&] { return normal.values().value("ui.state").toString() == "simulated"; }));
}

TEST(GuiBackend, LegacyCadenceIsDiagnosticOnly) {
    app();
    DeviceController legacy(nullptr, true);
    EXPECT_TRUE(legacy.diagnosticLegacyPolling());
    EXPECT_EQ(legacy.statusPollingHz(), 4);
    EXPECT_EQ(legacy.settingsPollingHz(), 2);
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
    BlockRealUsb block;
    ASSERT_TRUE(block.ready());
    DeviceController c; // production high-rate mode must still refuse writes
    c.connectUsb();   // held test lease refuses before any physical USB I/O
    ASSERT_TRUE(waitFor([&] { return !c.lastError().isEmpty() || c.connection() == "Connected"; }));
    EXPECT_NE(c.connection(), "Connected");
    EXPECT_TRUE(c.lastError().contains("already in use"));
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
    engine.rootContext()->setContextProperty("integration", fakeIntegration());
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/Main.qml")));
    for (const auto& e : comp.errors()) ADD_FAILURE() << e.toString().toStdString();
    ASSERT_EQ(comp.status(), QQmlComponent::Ready);
    std::unique_ptr<QObject> root(comp.create());
    ASSERT_TRUE(root != nullptr);
    EXPECT_FALSE(root->property("title").toString().contains("EXPERIMENTAL"));
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.connection() == "Connected"; }));
    QCoreApplication::processEvents();
    EXPECT_EQ(root->property("interactive").toBool(), true);
    auto* headerStatus = root->findChild<QObject*>("headerConnectionIndicator");
    auto* sampleRateLabel = root->findChild<QObject*>("sampleRateLabel");
    ASSERT_TRUE(headerStatus);
    ASSERT_TRUE(sampleRateLabel);
    // A real read-only connection has only the space between x=262 and the sample-rate
    // label at x=461. The longer Phase 7D detail previously overran that slot.
    headerStatus->setProperty("detail", "read-only");
    QCoreApplication::processEvents();
    EXPECT_LE(headerStatus->property("x").toDouble() + headerStatus->property("width").toDouble(),
              sampleRateLabel->property("x").toDouble());
    root.reset();
}

TEST(Qml, LegacyDiagnosticRateIsClearlyLabelled) {
    app();
    DeviceController c(nullptr, true);
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("device", &c);
    engine.rootContext()->setContextProperty("integration", fakeIntegration());
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/Main.qml")));
    ASSERT_EQ(comp.status(), QQmlComponent::Ready);
    std::unique_ptr<QObject> root(comp.create());
    ASSERT_TRUE(root);
    EXPECT_TRUE(root->property("title").toString().contains("DIAGNOSTIC 2/4 Hz"));
    EXPECT_TRUE(root->property("title").toString().contains("READ-ONLY"));
}

TEST(Qml, ProductionRateIsNotMarkedExperimental) {
    app();
    DeviceController c;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("device", &c);
    engine.rootContext()->setContextProperty("integration", fakeIntegration());
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/Main.qml")));
    ASSERT_EQ(comp.status(), QQmlComponent::Ready);
    std::unique_ptr<QObject> root(comp.create());
    ASSERT_TRUE(root);
    EXPECT_FALSE(root->property("title").toString().contains("EXPERIMENTAL"));
    EXPECT_FALSE(root->property("title").toString().contains("DIAGNOSTIC"));
    EXPECT_EQ(c.statusPollingHz(), 48);
    EXPECT_EQ(c.settingsPollingHz(), 16);
    EXPECT_FALSE(c.writable());
}

TEST(Qml, EightMetersBindToObservedPresentationAndClearOnDisconnect) {
    app();
    DeviceController c;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("device", &c);
    engine.rootContext()->setContextProperty("integration", fakeIntegration());
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/Main.qml")));
    ASSERT_EQ(comp.status(), QQmlComponent::Ready);
    std::unique_ptr<QObject> root(comp.create());
    ASSERT_TRUE(root);
    const char* names[] = {"meterMic", "meterInstrument", "meterLoopback", "meterSpeaker", "meterHeadphone"};
    const int indices[] = {0, 1, 2, 4, 6};
    for (int i = 0; i < 5; ++i) {
        auto* meter = root->findChild<QObject*>(names[i]);
        ASSERT_TRUE(meter) << names[i];
        EXPECT_EQ(meter->property("meterL").value<QObject*>(), c.meters()->channel(indices[i]));
        if (i >= 2) EXPECT_EQ(meter->property("meterR").value<QObject*>(), c.meters()->channel(indices[i] + 1));
    }
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.meters()->freshness() == "fresh" &&
        static_cast<MeterChannel*>(c.meters()->channel(7))->valid(); }));
    for (int i = 0; i < 8; ++i) EXPECT_TRUE(static_cast<MeterChannel*>(c.meters()->channel(i))->valid());
    c.disconnectDevice();
    ASSERT_TRUE(waitFor([&] { return c.meters()->freshness() == "unavailable"; }));
    for (int i = 0; i < 8; ++i) {
        auto* channel = static_cast<MeterChannel*>(c.meters()->channel(i));
        EXPECT_FALSE(channel->valid());
        EXPECT_DOUBLE_EQ(channel->displayDb(), -60);
    }
    EXPECT_FALSE(c.meters()->needsFrames());
}

TEST(Qml, StandardIconsLoadFromBundledVectorResources) {
    app();
    QQmlEngine engine;
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/GlyphIcon.qml")));
    ASSERT_EQ(comp.status(), QQmlComponent::Ready);
    std::unique_ptr<QObject> root(comp.create());
    ASSERT_TRUE(root);
    auto* glyph = qobject_cast<QQuickItem*>(root.get());
    ASSERT_TRUE(glyph);
    glyph->setWidth(24);
    glyph->setHeight(24);
    auto* image = root->findChild<QObject*>("glyphImage");
    ASSERT_TRUE(image);
    for (const char* kind : {"mic", "guitar", "loop", "speakers", "headphones", "shuffle",
                             "lowcut", "clipguard", "laptop", "settings"}) {
        root->setProperty("kind", kind);
        ASSERT_TRUE(waitFor([&] { return image->property("status").toInt() == 1; })) << kind;
        EXPECT_TRUE(image->property("source").toUrl().toString().endsWith(".svg"));
        EXPECT_GE(image->property("sourceSize").toSize().width(), 4 * root->property("width").toInt());
    }
    root->setProperty("kind", "mic");
    QQuickWindow window;
    window.setColor(Qt::black);
    window.resize(40, 40);
    glyph->setParentItem(window.contentItem());
    glyph->setPosition(QPointF(8, 8));
    window.show();
    QCoreApplication::processEvents();
    const QImage rendered = window.grabWindow();
    ASSERT_FALSE(rendered.isNull());
    int brightPixels = 0;
    for (int y = 0; y < rendered.height(); ++y)
        for (int x = 0; x < rendered.width(); ++x)
            if (rendered.pixelColor(x, y).red() > 160) ++brightPixels;
    EXPECT_GT(brightPixels, 10) << "icon must be visible with the software renderer";
}

TEST(Qml, PeakMarkerIsUniquePixelAlignedAndHiddenAtFloorOrLiveLevel) {
    app();
    QQmlEngine engine;
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/LevelMeter.qml")));
    ASSERT_EQ(comp.status(), QQmlComponent::Ready);
    std::unique_ptr<QObject> root(comp.create());
    ASSERT_TRUE(root);
    auto* meter = qobject_cast<QQuickItem*>(root.get());
    ASSERT_TRUE(meter);
    QQuickItem stage;
    stage.setTransformOrigin(QQuickItem::TopLeft);
    meter->setParentItem(&stage);
    MeterChannel sample;
    lewitt::MeterDynamics::Channel state;
    state.valid = true;
    state.displayDb = -40;
    state.peakDb = -10;
    sample.apply(state);
    meter->setProperty("meterL", QVariant::fromValue(static_cast<QObject*>(&sample)));
    meter->setProperty("sourceFreshness", "fresh");
    std::function<void(QQuickItem*, QList<QQuickItem*>&)> findMarkers =
        [&](QQuickItem* item, QList<QQuickItem*>& out) {
            if (item->objectName() == "sampledPeakMarker0") out.push_back(item);
            for (auto* child : item->childItems()) findMarkers(child, out);
        };
    QList<QQuickItem*> markers;
    findMarkers(meter, markers);
    ASSERT_EQ(markers.size(), 1);
    auto* marker = markers.front();
    ASSERT_TRUE(marker);
    const double dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
    for (double factor : {1.0, 1.25, 1.5, 2.0}) {
        stage.setScale(factor);
        meter->setProperty("stageScale", factor);
        QCoreApplication::processEvents();
        ASSERT_TRUE(marker->isVisible()) << factor;
        EXPECT_NEAR(marker->height() * factor * dpr, 1.0, 0.001) << factor;
        const double physicalY = marker->mapToScene(QPointF(0, 0)).y() * dpr;
        EXPECT_NEAR(physicalY, std::round(physicalY), 0.001) << factor;
    }
    state.displayDb = state.peakDb;
    sample.apply(state);
    QCoreApplication::processEvents();
    EXPECT_FALSE(marker->isVisible()) << "bar top is already the current peak";
    state.displayDb = state.peakDb = -60;
    sample.apply(state);
    QCoreApplication::processEvents();
    EXPECT_FALSE(marker->isVisible()) << "floor/silence is not a held peak";
    meter->setProperty("sourceFreshness", "stale");
    EXPECT_FALSE(marker->isVisible());
}

TEST(Qml, RenderedPeakIsOneRowAndDisappearsAtSilence) {
    app();
    QQmlEngine engine;
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/LevelMeter.qml")));
    ASSERT_EQ(comp.status(), QQmlComponent::Ready);
    std::unique_ptr<QObject> root(comp.create());
    ASSERT_TRUE(root);
    auto* meter = qobject_cast<QQuickItem*>(root.get());
    ASSERT_TRUE(meter);
    meter->setWidth(36);
    meter->setHeight(187);
    meter->setProperty("scale", true); // show the labelled 26-unit left gutter
    MeterChannel sample;
    lewitt::MeterDynamics::Channel state;
    state.valid = true;
    state.displayDb = -40;
    state.peakDb = -10;
    sample.apply(state);
    meter->setProperty("meterL", QVariant::fromValue(static_cast<QObject*>(&sample)));
    meter->setProperty("sourceFreshness", "fresh");
    QQuickWindow window;
    window.setColor(Qt::black);
    window.resize(60, 200);
    meter->setParentItem(window.contentItem());
    window.show();
    QCoreApplication::processEvents();
    int capture = 0;
    EXPECT_NEAR(meter->property("pixelRatio").toDouble(), window.devicePixelRatio(), 0.001);
    std::function<QQuickItem*(QQuickItem*)> markerIn = [&](QQuickItem* item) -> QQuickItem* {
        if (item->objectName() == "sampledPeakMarker0") return item;
        for (auto* child : item->childItems())
            if (auto* match = markerIn(child)) return match;
        return nullptr;
    };
    auto* marker = markerIn(meter);
    ASSERT_TRUE(marker);
    EXPECT_NEAR(marker->height() * window.devicePixelRatio(), 1.0, 0.001);
    const double physicalTop = marker->mapToScene(QPointF(0, 0)).y() * window.devicePixelRatio();
    EXPECT_NEAR(physicalTop, std::round(physicalTop), 0.001);
    auto amberRows = [&]() {
        const QImage image = window.grabWindow();
        if (image.isNull()) return -1;
        const QString outputDir = qEnvironmentVariable("OPENWITTY_VISUAL_OUTPUT_DIR");
        if (!outputDir.isEmpty()) image.save(QDir(outputDir).filePath(
            QStringLiteral("openwitty-peak-%1.png").arg(++capture)));
        const int x = static_cast<int>(30 * window.devicePixelRatio());
        int rows = 0;
        for (int y = 0; y < image.height(); ++y) {
            const QColor c = image.pixelColor(x, y);
            if (c.red() > 200 && c.green() > 100 && c.blue() < 100) ++rows;
        }
        return rows;
    };
    ASSERT_EQ(amberRows(), 1);
    state.displayDb = state.peakDb = -60;
    sample.apply(state);
    QCoreApplication::processEvents();
    EXPECT_EQ(amberRows(), 0);
}

TEST(Qml, MockWindowRendersAtNarrowAndRegularWidths) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.connection() == "Connected"; }));
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("device", &c);
    engine.rootContext()->setContextProperty("integration", fakeIntegration());
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/Main.qml")));
    ASSERT_EQ(comp.status(), QQmlComponent::Ready);
    std::unique_ptr<QObject> root(comp.create());
    ASSERT_TRUE(root);
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    ASSERT_TRUE(window);
    const QString outputDir = qEnvironmentVariable("OPENWITTY_VISUAL_OUTPUT_DIR");
    for (const QSize size : {QSize(1008, 566), QSize(720, 440)}) {
        window->resize(size);
        QCoreApplication::processEvents();
        const QImage image = window->grabWindow();
        ASSERT_FALSE(image.isNull());
        EXPECT_EQ(image.size(), size * window->devicePixelRatio());
        if (!outputDir.isEmpty())
            EXPECT_TRUE(image.save(QDir(outputDir).filePath(QStringLiteral("openwitty-mock-%1x%2.png")
                .arg(size.width()).arg(size.height()))));
    }
}

TEST(Qml, PanicMuteIndicatorFollowsDerivedState) {
    app();
    DeviceController c;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("device", &c);
    engine.rootContext()->setContextProperty("integration", fakeIntegration());
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/Main.qml")));
    ASSERT_EQ(comp.status(), QQmlComponent::Ready);
    std::unique_ptr<QObject> root(comp.create());
    ASSERT_TRUE(root);
    auto* indicator = root->findChild<QObject*>("panicMuteIndicator");
    ASSERT_TRUE(indicator);
    auto* bannerText = root->findChild<QObject*>("connectionBannerText");
    ASSERT_TRUE(bannerText);
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("mute.allFour"); }));
    EXPECT_FALSE(indicator->property("visible").toBool());
    c.simulatePanicMute();
    ASSERT_TRUE(waitFor([&] { return c.values()["mute.allFour"].toBool(); }, 4000));
    EXPECT_TRUE(indicator->property("visible").toBool());
    EXPECT_GT(bannerText->property("width").toDouble(), 100.0);
    EXPECT_LE(bannerText->property("x").toDouble() + bannerText->property("width").toDouble(),
              indicator->property("x").toDouble());
    c.simulateUnplug();
    ASSERT_TRUE(waitFor([&] { return c.connection() != "Connected"; }));
    EXPECT_FALSE(indicator->property("visible").toBool());
}

TEST(Qml, FreshnessWarningTracksStaleRecoveryAndReconnect) {
    app();
    DeviceController c;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("device", &c);
    engine.rootContext()->setContextProperty("integration", fakeIntegration());
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/Main.qml")));
    ASSERT_EQ(comp.status(), QQmlComponent::Ready);
    std::unique_ptr<QObject> root(comp.create());
    ASSERT_TRUE(root);
    auto* banner = root->findChild<QObject*>("connectionBannerText");
    ASSERT_TRUE(banner);
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().value("fresh.settings") == "fresh" &&
                                  c.values().value("fresh.status") == "fresh"; }));
    c.injectFault("settings-stall-repeated");  // status GETs continue to succeed
    ASSERT_TRUE(waitFor([&] { return c.values().value("fresh.settings") == "stale" &&
                                  c.values().value("fresh.status") == "fresh"; }, 4000));
    EXPECT_TRUE(banner->property("text").toString().contains("SETTINGS STALE"));
    EXPECT_EQ(c.connection(), "Connected");
    c.injectFault("clear");
    ASSERT_TRUE(waitFor([&] { return c.values().value("fresh.settings") == "fresh" &&
                                  c.values().value("fresh.status") == "fresh"; }, 2000));
    EXPECT_FALSE(banner->property("text").toString().contains("STALE"));
    c.simulatePanicMute();
    ASSERT_TRUE(waitFor([&] { return c.values().value("mute.allFour").toBool(); }, 2000));
    c.injectFault("status-stall-repeated");  // settings GETs continue to succeed
    ASSERT_TRUE(waitFor([&] { return c.values().value("fresh.status") == "stale" &&
                                  c.values().value("fresh.settings") == "fresh"; }, 3000));
    EXPECT_TRUE(banner->property("text").toString().contains("METERS STALE"));
    auto* mutedNotice = root->findChild<QObject*>("panicMuteIndicator");
    ASSERT_TRUE(mutedNotice);
    ASSERT_TRUE(mutedNotice->property("visible").toBool());
    EXPECT_LE(banner->property("x").toDouble() + banner->property("width").toDouble(),
              mutedNotice->property("x").toDouble());
    c.injectFault("clear");
    ASSERT_TRUE(waitFor([&] { return c.values().value("fresh.settings") == "fresh" &&
                                  c.values().value("fresh.status") == "fresh"; }, 2000));
    c.injectFault("stall-repeated");  // neither stream can mask the other in the banner
    ASSERT_TRUE(waitFor([&] { return c.values().value("fresh.settings") == "stale" &&
                                  c.values().value("fresh.status") == "stale"; }, 3000));
    EXPECT_TRUE(banner->property("text").toString().contains("SETTINGS STALE"));
    EXPECT_TRUE(banner->property("text").toString().contains("METERS STALE"));
    c.injectFault("clear");
    ASSERT_TRUE(waitFor([&] { return c.values().value("fresh.settings") == "fresh" &&
                                  c.values().value("fresh.status") == "fresh"; }, 2000));
    c.simulateUnplug();
    ASSERT_TRUE(waitFor([&] { return c.connection() == "Disconnected" &&
                                  c.values().value("fresh.settings") == "unavailable" &&
                                  c.values().value("fresh.status") == "unavailable"; }));
    EXPECT_EQ(c.values().value("fresh.settings"), "unavailable");
    EXPECT_EQ(c.values().value("fresh.status"), "unavailable");
    c.simulateReplug();
    ASSERT_TRUE(waitFor([&] { return c.connection() == "Connected" &&
                                  c.values().value("fresh.settings") == "fresh" &&
                                  c.values().value("fresh.status") == "fresh"; }));
}

TEST(Qml, RealReadOnlyMixUsesObservedPhysicalScale) {
    app();
    QQmlEngine engine;
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/MonitorSection.qml")));
    for (const auto& e : comp.errors()) ADD_FAILURE() << e.toString().toStdString();
    ASSERT_EQ(comp.status(), QQmlComponent::Ready);
    auto fractionAt = [&](double mix) {
        engine.rootContext()->setContextProperty("device", QVariantMap{
            {"simulated", false}, {"values", QVariantMap{{"mon.mix", mix}}}});
        std::unique_ptr<QObject> section(comp.create());
        EXPECT_TRUE(section != nullptr);
        if (!section) return -1.0;
        auto* knob = section->findChild<QObject*>("monitorMixKnob");
        EXPECT_TRUE(knob != nullptr);
        if (!knob) return -1.0;
        EXPECT_FALSE(knob->property("enabled").toBool());
        EXPECT_DOUBLE_EQ(knob->property("from").toDouble(), -100.0);
        EXPECT_DOUBLE_EQ(knob->property("to").toDouble(), 0.0);
        EXPECT_EQ(knob->property("maxLabel").toString(), "0:100");
        return knob->property("frac").toDouble();
    };
    EXPECT_DOUBLE_EQ(fractionAt(-100.0), 0.0);
    EXPECT_NEAR(fractionAt(-18.57), 0.8143, 0.0001);
    EXPECT_DOUBLE_EQ(fractionAt(0.0), 1.0);
    engine.rootContext()->setContextProperty("device", QVariantMap{
        {"simulated", true}, {"values", QVariantMap{{"mon.mix", -25.0}}}});
    std::unique_ptr<QObject> mockSection(comp.create());
    ASSERT_TRUE(mockSection);
    auto* mockKnob = mockSection->findChild<QObject*>("monitorMixKnob");
    ASSERT_TRUE(mockKnob);
    EXPECT_DOUBLE_EQ(mockKnob->property("from").toDouble(), -100.0);   // one scale since Phase 11A
    EXPECT_DOUBLE_EQ(mockKnob->property("to").toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(mockKnob->property("frac").toDouble(), 0.75);
    EXPECT_EQ(mockKnob->property("maxLabel").toString(), "0:100");
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

TEST(GuiSync, PhysicalSelectedChannelIsPublishedAndUnknownIsSafe) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("selected.channel"); }));
    EXPECT_EQ(c.values()["selected.channel"].toString(), "microphone");
    for (const auto& entry : std::initializer_list<std::pair<int, const char*>>{
             {2, "instrument"}, {3, "speaker"}, {4, "headphone"}}) {
        c.simulateExternalChange("selected.channel", entry.first);
        ASSERT_TRUE(waitFor([&] { return c.values()["selected.channel"].toString() == entry.second; }, 4000));
        EXPECT_TRUE(c.values()["selected.channelKnown"].toBool());
    }
    c.simulateExternalChange("selected.channel", 0x7f);
    ASSERT_TRUE(waitFor([&] { return c.values()["selected.channelRaw"].toInt() == 0x7f; }, 4000));
    EXPECT_EQ(c.values()["selected.channel"].toString(), "unknown");
    EXPECT_FALSE(c.values()["selected.channelKnown"].toBool());
}

TEST(GuiSync, RapidPhysicalChangesConvergeOnTheNextSettingsRefresh) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("spk.volume"); }));
    // These are device-side changes only: no GUI setValue()/SET path is involved.
    c.simulateExternalChange("in1.gain", 45.0);
    c.simulateExternalChange("spk.volume", -12.0);
    c.simulateExternalChange("in1.mute", true);
    c.simulateExternalChange("selected.channel", 3);
    ASSERT_TRUE(waitFor([&] {
        return c.values()["in1.gain"].toDouble() == 45.0 && c.values()["spk.volume"].toDouble() == -12.0 &&
               c.values()["in1.mute"].toBool() && c.values()["selected.channel"].toString() == "speaker";
    }, 4000));
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
    BlockRealUsb block;
    ASSERT_TRUE(block.ready());
    DeviceController c;
    c.connectUsb();
    ASSERT_TRUE(waitFor([&] { return !c.lastError().isEmpty() || c.connection() == "Connected"; }));
    EXPECT_NE(c.connection(), "Connected");
    EXPECT_TRUE(c.lastError().contains("already in use"));
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
        {"mon.mix", -25.0}, {"mon.mic", true}, {"mon.inst", true}, {"out.dim", true}, {"out.mono", true},
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
    BlockRealUsb block;
    ASSERT_TRUE(block.ready());
    DeviceController c;
    c.connectUsb();
    c.flush();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("ui.state"); }));
    EXPECT_NE(c.connection(), "Connected");
    EXPECT_TRUE(c.lastError().contains("already in use"));
    const QString st = c.values().value("ui.state").toString();
    EXPECT_NE(st, "simulated");
    EXPECT_NE(st, "real-writable");
    EXPECT_FALSE(c.writable());
    EXPECT_FALSE(c.values().contains("sampleRate")) << "no fabricated sample rate on a non-simulated session";
}

// Phase 8D: the desktop volume link is never active in simulation, even with the opt-in set.
TEST(GuiBackend, DesktopVolumeLinkIsOffInSimulation) {
    app();
    ASSERT_TRUE(lewitt::saveUserConfig({lewitt::DesktopVolumeSync::Mirror, false, false}, lewitt::userConfigPath()).ok());
    {
        DeviceController c;
        c.connectMock();
        ASSERT_TRUE(waitFor([&] { return c.connection() == "Connected"; }));
        ASSERT_TRUE(waitFor([&] { return c.values().contains("desktop.volumeLink"); }));
        EXPECT_EQ(c.values()["desktop.volumeLink"].toString(), "off");
    }
    ASSERT_TRUE(lewitt::saveUserConfig({}, lewitt::userConfigPath()).ok());
}

// Phase 8G: Settings > Desktop integration backend.
namespace {
struct Calls {
    QStringList log;
    QString show = "LoadState=loaded\nUnitFileState=disabled\nActiveState=inactive\n";
    int enableRc{0}, disableRc{0};
    bool noEffect{false};                 // systemctl "succeeds" but nothing changes
    bool showFails{false};
};
std::unique_ptr<IntegrationSettings> makeSettings(Calls& calls, std::optional<lewitt::AudioSetupStatus> st = std::nullopt) {
    return std::make_unique<IntegrationSettings>(
        nullptr,
        [&calls](const QString& prog, const QStringList& args, QString* out) {
            calls.log << prog + " " + args.join(' ');
            if (args.contains("show")) {
                if (calls.showFails) return 1;
                if (out) *out = calls.show;
                return 0;
            }
            if (args.contains("enable")) {
                if (calls.enableRc == 0 && !calls.noEffect)
                    calls.show = "LoadState=loaded\nUnitFileState=enabled\nActiveState=active\n";
                return calls.enableRc;
            }
            if (args.contains("disable")) {
                if (calls.disableRc == 0 && !calls.noEffect)
                    calls.show = "LoadState=loaded\nUnitFileState=disabled\nActiveState=inactive\n";
                return calls.disableRc;
            }
            return 0;
        },
        [st] { return st; });
}
lewitt::UserConfig onDisk() { return lewitt::loadUserConfig(lewitt::userConfigPath()).config; }
}  // namespace

TEST(Integration, DefaultsAreOffAndNothingIsRunOnStartup) {
    app();
    ASSERT_TRUE(lewitt::saveUserConfig({}, lewitt::userConfigPath()).ok());
    Calls calls;
    auto s = makeSettings(calls);
    EXPECT_FALSE(s->desktopSync());
    EXPECT_FALSE(s->vendorFallback());
    EXPECT_FALSE(s->speakerControl());
    EXPECT_FALSE(s->headphoneControl());
    EXPECT_FALSE(s->ledBrightnessControl());
    EXPECT_EQ(s->serviceState(), "disabled");
    for (const auto& c : calls.log) EXPECT_TRUE(c.contains(" show ")) << c.toStdString();   // only a status query
}

TEST(Integration, DeviceWritingOptionsNeedConfirmationAndFallbackNeedsSync) {
    app();
    ASSERT_TRUE(lewitt::saveUserConfig({}, lewitt::userConfigPath()).ok());
    Calls calls;
    auto s = makeSettings(calls);
    int applied = 0;
    QObject::connect(s.get(), &IntegrationSettings::configApplied, [&] { ++applied; });
    s->setSpeakerControl(true, /*confirmed=*/false);
    EXPECT_FALSE(onDisk().speakerControl);
    s->setSpeakerControl(true, true);
    EXPECT_TRUE(onDisk().speakerControl);
    s->setVendorFallback(true, true);                     // sync is off: refused
    EXPECT_FALSE(onDisk().vendorFallback);
    s->setDesktopSync(true);
    s->setVendorFallback(true, false);                    // not confirmed: refused
    EXPECT_FALSE(onDisk().vendorFallback);
    s->setVendorFallback(true, true);
    EXPECT_TRUE(onDisk().vendorFallback);
    EXPECT_EQ(onDisk().desktopVolumeSync, lewitt::DesktopVolumeSync::Mirror);
    s->setDesktopSync(false);                              // turning sync off also turns the fallback off
    EXPECT_FALSE(onDisk().vendorFallback);
    EXPECT_TRUE(onDisk().speakerControl);                  // separate setting, untouched
    EXPECT_EQ(applied, 4);
    for (const auto& c : calls.log) EXPECT_FALSE(c.contains("enable")) << c.toStdString();   // never starts the service
}

TEST(Integration, BackgroundServiceOnlyOnRequestAndResetTurnsEverythingOff) {
    app();
    ASSERT_TRUE(lewitt::saveUserConfig({lewitt::DesktopVolumeSync::Mirror, true, true}, lewitt::userConfigPath()).ok());
    Calls calls;
    auto s = makeSettings(calls);
    s->setBackgroundService(true);
    EXPECT_TRUE(calls.log.contains("systemctl --user enable --now openwitty-volumed.service"));
    EXPECT_EQ(s->serviceState(), "active");
    s->resetToDefaults();
    EXPECT_TRUE(calls.log.contains("systemctl --user disable --now openwitty-volumed.service"));
    EXPECT_EQ(s->serviceState(), "disabled");
    EXPECT_EQ(onDisk(), lewitt::UserConfig{});
}

TEST(Integration, MissingServiceIsReportedNotStarted) {
    app();
    Calls calls;
    calls.show = "LoadState=not-found\nUnitFileState=\nActiveState=inactive\n";
    auto s = makeSettings(calls);
    EXPECT_EQ(s->serviceState(), "not-installed");
    s->setBackgroundService(true);
    EXPECT_FALSE(s->lastError().isEmpty());
    for (const auto& c : calls.log) EXPECT_FALSE(c.contains("enable")) << c.toStdString();
}

TEST(Integration, SetupStatusAndQuirkExplanation) {
    app();
    Calls calls;
    lewitt::AudioSetupStatus st;
    st.card = 3;
    st.mixerLoadError = -6;
    auto s = makeSettings(calls, st);
    EXPECT_FALSE(s->setupAvailable());                     // not probed until the page is opened
    s->refresh();
    EXPECT_TRUE(s->setupAvailable());
    EXPECT_TRUE(s->cardFound());
    EXPECT_FALSE(s->mixerLoads());
    EXPECT_FALSE(s->findings().isEmpty());
    EXPECT_TRUE(s->quirkInstructions().contains("29c2:0004:ignore_ctl_error"));
    EXPECT_TRUE(s->quirkInstructions().contains("never installs it"));
    EXPECT_TRUE(s->quirkInstructions().contains("login screen"));          // Phase 9D: the side effect is explained
    EXPECT_TRUE(s->quirkInstructions().contains("louder or quieter"));
    auto none = makeSettings(calls, std::nullopt);
    none->refresh();
    EXPECT_FALSE(none->setupAvailable());
}

TEST(Integration, InvalidFileValuesAreShownAndFallBackToOff) {
    app();
    const QString path = QString::fromStdString(lewitt::userConfigPath());
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("vendor_fallback = yes please\ndesktop_volume_sync = mirror\n");
    f.close();
    Calls calls;
    auto s = makeSettings(calls);
    EXPECT_TRUE(s->desktopSync());
    EXPECT_FALSE(s->vendorFallback());
    EXPECT_EQ(s->configWarnings().size(), 1);
    ASSERT_TRUE(lewitt::saveUserConfig({}, lewitt::userConfigPath()).ok());
}

TEST(Integration, AnEnabledServiceCanAlwaysBeTurnedOffButOnlyStartedWithSync) {
    app();
    ASSERT_TRUE(lewitt::saveUserConfig({}, lewitt::userConfigPath()).ok());      // desktop sync off
    Calls calls;
    calls.show = "LoadState=loaded\nUnitFileState=enabled\nActiveState=active\n";
    auto s = makeSettings(calls);
    EXPECT_TRUE(s->serviceIdleOn());                                            // UI shows the hint
    EXPECT_TRUE(s->setBackgroundService(false));                                // allowed with sync off
    EXPECT_TRUE(calls.log.contains("systemctl --user disable --now openwitty-volumed.service"));
    EXPECT_EQ(s->serviceState(), "disabled");
    EXPECT_FALSE(s->serviceIdleOn());
    EXPECT_FALSE(s->setBackgroundService(true));                                // refused: would only idle
    EXPECT_TRUE(s->lastError().contains("first"));
    for (const auto& c : calls.log) EXPECT_FALSE(c.contains(" enable ")) << c.toStdString();
}

TEST(Integration, FailingOrIneffectiveSystemctlIsReported) {
    app();
    ASSERT_TRUE(lewitt::saveUserConfig({lewitt::DesktopVolumeSync::Mirror, false, false}, lewitt::userConfigPath()).ok());
    Calls calls;
    calls.enableRc = 1;
    auto s = makeSettings(calls);
    EXPECT_FALSE(s->setBackgroundService(true));
    EXPECT_TRUE(s->lastError().contains("exit 1"));
    EXPECT_EQ(s->serviceState(), "disabled");
    calls.enableRc = 0;
    calls.noEffect = true;                                                      // rc 0, nothing happened
    EXPECT_FALSE(s->setBackgroundService(true));
    EXPECT_TRUE(s->lastError().contains("no effect"));
}

TEST(Integration, ResetWithAServiceThatCannotBeDisabledIsReportedAsIncomplete) {
    app();
    ASSERT_TRUE(lewitt::saveUserConfig({lewitt::DesktopVolumeSync::Mirror, true, true}, lewitt::userConfigPath()).ok());
    Calls calls;
    calls.show = "LoadState=loaded\nUnitFileState=enabled\nActiveState=active\n";
    calls.disableRc = 1;
    auto s = makeSettings(calls);
    EXPECT_FALSE(s->resetToDefaults());
    EXPECT_EQ(onDisk(), lewitt::UserConfig{});                                  // settings ARE reset (service idles)
    EXPECT_TRUE(s->lastError().startsWith("Reset incomplete"));
    EXPECT_TRUE(s->lastError().contains("could not be disabled"));
    EXPECT_TRUE(s->lastError().contains("only idles"));
    EXPECT_EQ(s->serviceState(), "active");
    // the error survives: the save inside the reset must not have cleared it
    QCoreApplication::processEvents();
    EXPECT_FALSE(s->lastError().isEmpty());
}

TEST(Integration, ResetWhenTheServiceStateCannotBeCheckedIsNotClaimedComplete) {
    app();
    ASSERT_TRUE(lewitt::saveUserConfig({lewitt::DesktopVolumeSync::Mirror, false, false}, lewitt::userConfigPath()).ok());
    Calls calls;
    calls.showFails = true;
    auto s = makeSettings(calls);
    EXPECT_EQ(s->serviceState(), "unknown");
    EXPECT_FALSE(s->resetToDefaults());
    EXPECT_EQ(onDisk(), lewitt::UserConfig{});
    EXPECT_TRUE(s->lastError().contains("could not be checked"));
}

TEST(Integration, SuccessfulResetClearsEverything) {
    app();
    ASSERT_TRUE(lewitt::saveUserConfig({lewitt::DesktopVolumeSync::Mirror, true, false}, lewitt::userConfigPath()).ok());
    Calls calls;
    calls.show = "LoadState=loaded\nUnitFileState=enabled\nActiveState=active\n";
    auto s = makeSettings(calls);
    EXPECT_TRUE(s->resetToDefaults());
    EXPECT_TRUE(s->lastError().isEmpty());
    EXPECT_EQ(s->serviceState(), "disabled");
    EXPECT_EQ(onDisk(), lewitt::UserConfig{});
}

TEST(Qml, DesktopIntegrationPageLoadsWithoutErrors) {
    app();
    DeviceController c;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("device", &c);
    engine.rootContext()->setContextProperty("integration", fakeIntegration());
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/SettingsView.qml")));
    for (const auto& e : comp.errors()) ADD_FAILURE() << e.toString().toStdString();
    ASSERT_EQ(comp.status(), QQmlComponent::Ready);
    std::unique_ptr<QObject> root(comp.create());
    ASSERT_TRUE(root);
    root->setProperty("open", "desktop");
    QCoreApplication::processEvents();
    EXPECT_EQ(root->property("open").toString(), "desktop");
}

// Phase 9B acceptance finding: opening the window while openwitty-volumed holds the device showed a red
// "already in use" error for 1-2 s. During the hand-over grace it is "connecting", without an error.
TEST(GuiState, ServiceHandOverIsPresentedAsConnectingNotAsAnError) {
    app();
    BlockRealUsb block;          // the held lease plays the background service (no USB I/O)
    ASSERT_TRUE(block.ready());
    HandoverGrace grace(std::chrono::seconds(8));
    DeviceController c;
    c.connectUsb();
    c.flush();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("ui.state"); }));
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 1200) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);   // a few retries
    c.flush();
    EXPECT_EQ(c.values().value("ui.state").toString(), "connecting");
    EXPECT_EQ(c.values().value("ui.detail").toString(), "Taking over from the OpenWitty background service");
    EXPECT_TRUE(c.values().value("ui.error").toString().isEmpty());
    EXPECT_TRUE(c.lastError().isEmpty()) << c.lastError().toStdString();
    EXPECT_NE(c.connection(), "Connected");
    EXPECT_FALSE(c.writable());
}

TEST(GuiState, ALeaseThatStaysTakenIsReportedAfterTheHandOverGrace) {
    app();
    BlockRealUsb block;
    ASSERT_TRUE(block.ready());
    HandoverGrace grace(std::chrono::milliseconds(400));
    DeviceController c;
    c.connectUsb();
    ASSERT_TRUE(waitFor([&] { return c.lastError().contains("already in use"); }, 5000));
    // the error message is signalled just before the state is published
    ASSERT_TRUE(waitFor([&] { return c.values().value("ui.state").toString() == "error"; }, 3000))
        << c.values().value("ui.state").toString().toStdString();
    EXPECT_FALSE(c.values().value("ui.error").toString().isEmpty());
    EXPECT_FALSE(c.writable());
}

TEST(Qml, HeaderIndicatorShowsConnectingDuringTheServiceHandOver) {
    app();
    BlockRealUsb block;
    ASSERT_TRUE(block.ready());
    HandoverGrace grace(std::chrono::seconds(8));
    DeviceController c;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("device", &c);
    engine.rootContext()->setContextProperty("integration", fakeIntegration());
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/Main.qml")));
    ASSERT_EQ(comp.status(), QQmlComponent::Ready);
    std::unique_ptr<QObject> root(comp.create());
    ASSERT_TRUE(root != nullptr);
    c.connectUsb();
    ASSERT_TRUE(waitFor([&] { return c.values().value("ui.state").toString() == "connecting"; }));
    QCoreApplication::processEvents();
    auto* header = root->findChild<QObject*>("headerConnectionIndicator");
    ASSERT_TRUE(header);
    EXPECT_EQ(header->property("state").toString(), "Connecting");
    EXPECT_EQ(header->property("detail").toString(), "USB");
    EXPECT_NE(c.connection(), "Connecting");   // the raw transport state differs; the presented one counts
}

// ---- Phase 10A: per-control permissions on real hardware -----------------------------------------------------
#include "../cpp/FakeHardware.hpp"
#include "core/ControlCapabilities.hpp"
#include "core/OutputControl.hpp"
#include <QRegularExpression>

namespace {
// A controller connected to a simulated device that claims to be real hardware, with the given policy.
struct RealLike {
    lewitt::MockTransport mock;
    lewitt::testing::FakeHardware* hw{nullptr};
    std::unique_ptr<DeviceController> c;
    QTemporaryDir proc;                                    // fixture /proc/asound (empty: no sound card)
    explicit RealLike(lewitt::WritePolicy p, bool hwCheck = false) {
        c = std::make_unique<DeviceController>();
        if (hwCheck) c->enableHardwareCheck();               // as openwitty --hw-check: before connecting
        auto t = std::make_unique<lewitt::testing::FakeHardware>(&mock);
        hw = t.get();
        c->connectTransportForTesting(std::move(t), std::move(p), proc.path().toStdString());
        c->flush();
        EXPECT_TRUE(waitFor([&] { return c->connection() == "Connected" && c->values().contains("can.spk.mute"); }));
    }
    bool can(const char* k) const { return c->values().value(QString("can.") + k).toBool(); }
};
std::unique_ptr<QObject> loadMain(DeviceController& c, QQmlEngine& engine) {
    engine.rootContext()->setContextProperty("device", &c);
    engine.rootContext()->setContextProperty("integration", fakeIntegration());
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/Main.qml")));
    for (const auto& e : comp.errors()) ADD_FAILURE() << e.toString().toStdString();
    std::unique_ptr<QObject> root(comp.create());
    QCoreApplication::processEvents();
    return root;
}
bool enabledOf(QObject* root, const char* name) {
    QObject* o = root->findChild<QObject*>(name);
    EXPECT_TRUE(o) << name;
    return o && o->property("enabled").toBool();
}
}  // namespace

TEST(GuiPermissions, SpeakerHeadphoneControlEnablesOnlyTheFourOutputControls) {
    app();
    RealLike r(lewitt::outputWritePolicy(true, true));
    for (const char* k : {"spk.volume", "spk.mute", "hp.volume", "hp.mute"}) EXPECT_TRUE(r.can(k)) << k;
    for (const char* k : {"in1.gain", "in1.mute", "in1.phantom", "in1.highPass", "in2.preamp", "in1.denoise",
                          "loop.mute", "mon.mix", "mon.mic", "out.dim", "out.mono", "set.ledBrightness",
                          "set.customButton", "set.highVolume", "set.led.icons", "autogain1", "autosetup"})
        EXPECT_FALSE(r.can(k)) << k;
    QQmlEngine engine;
    auto root = loadMain(*r.c, engine);
    ASSERT_TRUE(root);
    auto* banner = root->findChild<QObject*>("connectionBannerText");
    ASSERT_TRUE(banner);
    EXPECT_EQ(banner->property("text").toString(),
              "REAL DEVICE — can change: speaker volume, speaker mute, headphone volume, headphone mute. "
              "All other controls are display-only.");
    EXPECT_TRUE(enabledOf(root.get(), "fader_spk"));
    EXPECT_TRUE(enabledOf(root.get(), "mute_spk"));
    EXPECT_TRUE(enabledOf(root.get(), "fader_hp"));
    EXPECT_TRUE(enabledOf(root.get(), "mute_hp"));
    for (const char* n : {"mute_loop", "gainKnob1", "gainKnob2", "phantomButton1", "autogainButton1", "inputMute1",
                          "dimButton", "monitorMixKnob", "autosetupButton", "gainLockSwitch", "ledBrightnessFader"})
        EXPECT_FALSE(enabledOf(root.get(), n)) << n;
}

TEST(GuiPermissions, ARefusedChangeIsNotShownAsAppliedAndNothingIsSent) {
    app();
    RealLike r(lewitt::outputWritePolicy(true, true));
    const int writes = r.hw->writeCalls;
    r.c->setValue("in1.phantom", true);
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return !r.c->lastError().isEmpty(); }));
    EXPECT_TRUE(r.c->lastError().contains("not available on the real device")) << r.c->lastError().toStdString();
    EXPECT_FALSE(r.c->values().value("in1.phantom").toBool());                 // still the device's value
    EXPECT_FALSE(r.c->values().value("pending.in1.phantom").toBool());         // nothing staged
    EXPECT_EQ(r.hw->writeCalls, writes);
    r.c->trigger("autogain1");
    r.c->flush();
    EXPECT_EQ(r.hw->writeCalls, writes);                                       // no trigger reached the device
    EXPECT_NE(r.c->values().value("auto.state").toString(), "running");
}

TEST(GuiPermissions, AnAuthorizedOutputChangeIsWrittenAndVerified) {
    app();
    RealLike r(lewitt::outputWritePolicy(true, true));
    const bool before = r.c->values().value("spk.mute").toBool();
    r.c->setValue("spk.mute", !before);
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("spk.mute").toBool() != before; }));
    EXPECT_GE(r.hw->writeCalls, 1);
    EXPECT_FALSE(r.c->values().value("pending.spk.mute").toBool());
}

TEST(GuiPermissions, ReadOnlySessionEnablesNothingAndMockEnablesEverything) {
    app();
    RealLike ro{lewitt::WritePolicy{}};
    for (const auto& k : lewitt::controlKeys()) EXPECT_FALSE(ro.can(k.c_str())) << k;
    DeviceController mock;
    mock.connectMock();
    ASSERT_TRUE(waitFor([&] { return mock.connection() == "Connected" && mock.values().contains("can.in1.phantom"); }));
    for (const auto& k : lewitt::controlKeys())
        EXPECT_TRUE(mock.values().value(QString("can.") + QString::fromStdString(k)).toBool()) << k;
}

// Every literal key the QML writes must have a capability entry (dynamic keys are listed explicitly).
TEST(GuiPermissions, EveryQmlControlKeyHasACapabilityEntry) {
    QStringList keys{"in1.gain", "in2.gain", "in1.mute", "in2.mute", "in1.highPass", "in2.highPass", "in1.clipguard",
                     "in2.clipguard", "in1.preamp", "in2.preamp", "in1.denoise", "in2.denoise", "in1.compress",
                     "in2.compress", "in1.phantom", "loop.mute", "spk.volume", "spk.mute", "hp.volume", "hp.mute",
                     "autogain1", "autogain2", "autogainBoth", "set.led.icons", "set.led.mute", "set.led.parameter",
                     "set.led.metering", "set.led.gainReduction", "autosetup", "set.macosStreaming"};
    const QRegularExpression lit(QString::fromLatin1(R"re(device\.(?:setValue|trigger)\("([A-Za-z0-9.]+)"\s*[,)])re"));
    for (const QString& f : QDir(QStringLiteral(LEWITT_QML_DIR)).entryList({"*.qml"})) {
        QFile file(QStringLiteral(LEWITT_QML_DIR) + "/" + f);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        auto it = lit.globalMatch(QString::fromUtf8(file.readAll()));
        while (it.hasNext()) keys << it.next().captured(1);
    }
    for (const QString& k : keys) EXPECT_NE(lewitt::controlFootprint(k.toStdString()), nullptr) << k.toStdString();
}

// ---- Phase 10B: independent write permissions ------------------------------------------------------------------
namespace {
void writeConfigText(const char* text) {
    const std::string path = lewitt::userConfigPath();
    QDir().mkpath(QFileInfo(QString::fromStdString(path)).absolutePath());
    QFile f(QString::fromStdString(path));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text);
}
lewitt::Bytes deviceBytes(RealLike& r) { return r.mock.settingsBytes(); }
std::vector<std::size_t> changedOffsets(const lewitt::Bytes& a, const lewitt::Bytes& b) {
    std::vector<std::size_t> d;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (a[i] != b[i]) d.push_back(i);
    return d;
}
}  // namespace

TEST(Permissions10B, EachPermissionIsIndependentAndUnrelatedSettingsArePreserved) {
    app();
    writeConfigText("[openwitty]\ndesktop_volume_sync = mirror\nvendor_fallback = false\n");
    Calls calls;
    auto s = makeSettings(calls);
    s->setLedBrightnessControl(true, true);
    EXPECT_TRUE(onDisk().ledBrightnessControl);
    EXPECT_FALSE(onDisk().speakerControl);
    EXPECT_FALSE(onDisk().headphoneControl);
    s->setHeadphoneControl(true, true);
    s->setSpeakerControl(true, false);                      // not confirmed: refused
    EXPECT_FALSE(onDisk().speakerControl);
    EXPECT_TRUE(onDisk().headphoneControl);
    s->setLedBrightnessControl(false, false);               // turning off needs no confirmation
    EXPECT_FALSE(onDisk().ledBrightnessControl);
    EXPECT_TRUE(onDisk().headphoneControl);                 // the others stay
    EXPECT_EQ(onDisk().desktopVolumeSync, lewitt::DesktopVolumeSync::Mirror);
    EXPECT_FALSE(onDisk().vendorFallback);
    for (const auto& c : calls.log) EXPECT_FALSE(c.contains("enable") || c.contains("disable")) << c.toStdString();
}

TEST(Permissions10B, LegacyOutputControlAsksForReconfirmationAndGrantsNothing) {
    app();
    writeConfigText("[openwitty]\ndesktop_volume_sync = mirror\nvendor_fallback = false\noutput_control = true\n");
    Calls calls;
    auto s = makeSettings(calls);
    EXPECT_TRUE(s->legacyOutputControlPending());
    EXPECT_FALSE(s->speakerControl());
    EXPECT_FALSE(s->headphoneControl());
    s->setLedBrightnessControl(true, true);                 // an unrelated permission keeps the notice
    EXPECT_TRUE(onDisk().legacyOutputControlPending);
    s->setHeadphoneControl(true, true);                     // reconfirming an output permission ends it
    EXPECT_FALSE(onDisk().legacyOutputControlPending);
    EXPECT_FALSE(onDisk().speakerControl);                  // only what was reconfirmed
    EXPECT_EQ(onDisk().desktopVolumeSync, lewitt::DesktopVolumeSync::Mirror);
    writeConfigText("output_control = true\n");
    auto t = makeSettings(calls);
    t->dismissLegacyOutputControl();
    EXPECT_FALSE(onDisk().legacyOutputControlPending);
    EXPECT_FALSE(onDisk().speakerControl || onDisk().headphoneControl || onDisk().ledBrightnessControl);
}

TEST(Permissions10B, ResetTurnsEveryWritePermissionOff) {
    app();
    writeConfigText("speaker_control = true\nheadphone_control = true\nled_brightness_control = true\noutput_control = true\n");
    Calls calls;
    auto s = makeSettings(calls);
    EXPECT_TRUE(s->resetToDefaults());
    EXPECT_EQ(onDisk(), lewitt::UserConfig{});
}

TEST(Permissions10B, ConfirmationDialogCancelGrantsNothingEnableGrantsOnlyThatField) {
    app();
    writeConfigText("[openwitty]\n");
    Calls calls;
    auto s = makeSettings(calls);
    DeviceController c;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("device", &c);
    engine.rootContext()->setContextProperty("integration", s.get());
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/Main.qml")));
    std::unique_ptr<QObject> root(comp.create());
    ASSERT_TRUE(root);
    root->setProperty("showSettings", true);                 // Settings > Desktop integration
    QObject* sv = root->findChild<QObject*>("settingsView");
    ASSERT_TRUE(sv);
    sv->setProperty("open", "device");                        // Settings > Device control
    QCoreApplication::processEvents();
    QObject* sw = root->findChild<QObject*>("ledControlSwitch");
    QObject* dialog = root->findChild<QObject*>("confirmWriteDialog");
    QObject* cancel = root->findChild<QObject*>("confirmWriteCancel");
    QObject* enable = root->findChild<QObject*>("confirmWriteEnable");
    ASSERT_TRUE(sw && dialog && cancel && enable);
    QMetaObject::invokeMethod(sw, "toggled", Q_ARG(bool, true));
    EXPECT_TRUE(dialog->property("visible").toBool());
    EXPECT_EQ(dialog->property("field").toString(), "led");
    QMetaObject::invokeMethod(cancel, "clicked");
    EXPECT_FALSE(dialog->property("visible").toBool());
    EXPECT_FALSE(s->ledBrightnessControl());
    EXPECT_FALSE(sw->property("checked").toBool());        // the switch shows the real (unchanged) state
    QMetaObject::invokeMethod(sw, "toggled", Q_ARG(bool, true));
    QMetaObject::invokeMethod(enable, "clicked");
    EXPECT_TRUE(s->ledBrightnessControl());
    EXPECT_FALSE(s->speakerControl());
    EXPECT_FALSE(s->headphoneControl());
    EXPECT_TRUE(sw->property("checked").toBool());
}

TEST(Permissions10B, EveryCombinationAuthorizesExactlyItsControls) {
    app();
    for (int m = 0; m < 8; ++m) {
        const bool sp = m & 1, hp = m & 2, led = m & 4;
        RealLike r(lewitt::guiWritePolicy(sp, hp, led));
        for (const auto& k : lewitt::controlKeys()) {
            const QString key = QString::fromStdString(k);
            const bool expected = (sp && (key == "spk.volume" || key == "spk.mute")) ||
                                  (hp && (key == "hp.volume" || key == "hp.mute")) || (led && key == "set.ledBrightness");
            EXPECT_EQ(r.can(k.c_str()), expected) << k << " combination " << m;
        }
    }
}

TEST(Permissions10B, LedBrightnessAloneWritesOnlyByte37AndRestoresTheOriginal) {
    app();
    RealLike r(lewitt::guiWritePolicy(false, false, true));
    EXPECT_FALSE(r.can("spk.volume"));
    EXPECT_FALSE(r.can("set.led.icons"));                    // LED colours are not part of it
    const int original = r.c->values().value("set.ledBrightness").toInt();
    const lewitt::Bytes before = deviceBytes(r);
    const int target = original == 60 ? 40 : 60;
    r.c->setValue("set.ledBrightness", target);
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("set.ledBrightness").toInt() == target; }));
    EXPECT_EQ(changedOffsets(before, deviceBytes(r)), (std::vector<std::size_t>{lewitt::settings_off::kLedBrightness}));
    EXPECT_EQ(r.c->values().value("write.state").toString(), "verified");
    r.c->setValue("set.ledBrightness", original);            // the later hardware test's restore step
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("set.ledBrightness").toInt() == original; }));
    EXPECT_EQ(deviceBytes(r), before);
    // speaker and headphone edits are refused without staging
    const int writes = r.hw->writeCalls;
    r.c->setValue("spk.mute", true);
    r.c->setValue("hp.volume", -40.0);
    r.c->flush();
    EXPECT_EQ(r.hw->writeCalls, writes);
    EXPECT_FALSE(r.c->values().value("pending.spk.mute").toBool());
}

TEST(Permissions10B, AnUnconfirmedLedWriteStopsAllFurtherWritesUntilANewSession) {
    app();
    RealLike r(lewitt::guiWritePolicy(false, false, true));
    r.hw->dropWrites = true;                                  // the device acknowledges but ignores the SET
    const int original = r.c->values().value("set.ledBrightness").toInt();
    r.c->setValue("set.ledBrightness", original == 60 ? 40 : 60);
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("write.locked").toBool(); }));
    EXPECT_EQ(r.c->values().value("write.outcome").toString(), "unconfirmed");
    EXPECT_EQ(r.hw->writeCalls, 1);
    EXPECT_FALSE(r.c->values().value("pending.set.ledBrightness").toBool());   // not kept for a later SET
    EXPECT_EQ(r.c->values().value("set.ledBrightness").toInt(), original);      // shows what the device reports
    EXPECT_FALSE(r.can("set.ledBrightness"));                                   // control disabled
    r.hw->dropWrites = false;
    for (int i = 0; i < 5; ++i) r.c->flush();                 // polls only
    r.c->setValue("set.ledBrightness", 10);                   // a further request (e.g. queued) is refused
    r.c->retryPending();
    r.c->flush();
    EXPECT_EQ(r.hw->writeCalls, 1);                           // never a second SET
    EXPECT_TRUE(waitFor([&] { return r.c->values().value("write.outcome").toString() == "refused"; }));
    EXPECT_EQ(lewitt::SettingsMessage::fromBytes(r.mock.settingsBytes()).value().ledBrightness(), original);
    // only a new session (the permission turned off and on reconnects) lifts the stop
    auto t2 = std::make_unique<lewitt::testing::FakeHardware>(&r.mock);
    auto* hw2 = t2.get();
    r.c->connectTransportForTesting(std::move(t2), lewitt::guiWritePolicy(false, false, true), r.proc.path().toStdString());
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return !r.c->values().value("write.locked").toBool() && r.can("set.ledBrightness"); }));
    EXPECT_EQ(hw2->writeCalls, 0);
}

TEST(Permissions10B, SpeakerIncreasesStayBoundedTo10dBPerStep) {
    app();
    RealLike r(lewitt::guiWritePolicy(true, false, false));
    {
        auto m = lewitt::SettingsMessage::fromBytes(r.mock.settingsBytes()).value();   // device at -40 dB
        ASSERT_TRUE(m.setSpeakerVolumeDb(-40.0).ok());
        auto b = m.bytes();
        r.mock.setSettingsBytes(lewitt::Bytes(b.begin(), b.end()));
    }
    ASSERT_TRUE(waitFor([&] {
        r.c->flush();                                         // settings are read on their own schedule
        return std::fabs(r.c->values().value("spk.volume").toDouble() + 40.0) < 0.01;
    }));
    const double start = -40.0;
    r.c->setValue("spk.volume", 0.0);                         // a jump to the maximum
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("spk.volume").toDouble() > start; }));
    EXPECT_LE(r.c->values().value("spk.volume").toDouble(), start + 10.0 + 1e-6);   // -30 dB, not 0 dB
    EXPECT_GE(r.c->values().value("spk.volume").toDouble(), start + 10.0 - 0.01);
}

// ---- Phase 10B.1: one deliberate LED change = at most one SET; stable snapshots; read-only hardware check ----
#include <QMouseEvent>
#include <atomic>
#include <thread>
namespace {
void setDeviceLed(RealLike& r, int v) {
    lewitt::Bytes b = r.mock.settingsBytes();
    b[lewitt::settings_off::kLedBrightness] = static_cast<std::uint8_t>(v);
    r.mock.setSettingsBytes(b);
    EXPECT_TRUE(waitFor([&] { r.c->flush(); return r.c->values().value("set.ledBrightness").toInt() == v; }));
}
int deviceLed(RealLike& r) { return r.mock.settingsBytes()[lewitt::settings_off::kLedBrightness]; }
// Main.qml in a shown offscreen window, Settings > LEDs open, the real brightness slider found.
struct LedPage {
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* w{nullptr};
    QQuickItem* fader{nullptr};
    QObject* status{nullptr};
    explicit LedPage(DeviceController& c) {
        root = loadMain(c, engine);
        w = qobject_cast<QQuickWindow*>(root.get());
        EXPECT_TRUE(w);
        if (!w) return;
        w->resize(1008, 566);
        w->show();
        root->setProperty("showSettings", true);
        QObject* sv = root->findChild<QObject*>("settingsView");
        EXPECT_TRUE(sv);
        if (sv) sv->setProperty("open", "leds");
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        fader = root->findChild<QQuickItem*>("ledBrightnessFader");
        status = root->findChild<QObject*>("ledWriteStatus");
        EXPECT_TRUE(fader && status);
    }
    void mouse(QEvent::Type type, double fraction) {
        const QPointF scene = fader->mapToScene(QPointF(fader->width() * fraction, fader->height() / 2));
        QMouseEvent e(type, scene, scene, w->mapToGlobal(scene.toPoint()), Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
        QGuiApplication::sendEvent(w, &e);
    }
    void click(double f) { mouse(QEvent::MouseButtonPress, f); mouse(QEvent::MouseButtonRelease, f); }
    QString text() const { return status->property("text").toString(); }
};
}  // namespace

TEST(HwSafety10B1, LongDragOnTheRealDeviceSendsExactlyOneSetOnRelease) {
    app();
    RealLike r(lewitt::guiWritePolicy(false, false, true));
    setDeviceLed(r, 20);
    LedPage page(*r.c);
    ASSERT_TRUE(page.fader);
    ASSERT_TRUE(page.fader->isEnabled());
    EXPECT_TRUE(page.fader->property("commitOnRelease").toBool());
    const lewitt::Bytes before = deviceBytes(r);
    const int w0 = r.hw->writeCalls;
    page.mouse(QEvent::MouseButtonPress, 0.20);
    int moves = 0;
    for (int i = 0; i <= 600; ++i) {                          // 0.2 -> 0.95 -> 0.5 -> 0.8, 1803 pointer moves
        page.mouse(QEvent::MouseMove, 0.20 + 0.75 * i / 600.0); ++moves;
        if (i % 50 == 0) QCoreApplication::processEvents();
    }
    for (int i = 0; i <= 600; ++i) { page.mouse(QEvent::MouseMove, 0.95 - 0.45 * i / 600.0); ++moves; }
    for (int i = 0; i <= 600; ++i) { page.mouse(QEvent::MouseMove, 0.50 + 0.30 * i / 600.0); ++moves; }
    for (int i = 0; i < 10; ++i) { QCoreApplication::processEvents(); r.c->flush(); }
    EXPECT_EQ(r.hw->writeCalls, w0) << "previewing must not touch the device";
    EXPECT_EQ(deviceLed(r), 20);
    EXPECT_TRUE(page.fader->property("dragging").toBool());
    EXPECT_EQ(page.fader->property("previewValue").toInt(), 80);
    page.mouse(QEvent::MouseButtonRelease, 0.80);
    ASSERT_TRUE(waitFor([&] { return page.text().startsWith("Verified"); })) << page.text().toStdString();
    for (int i = 0; i < 5; ++i) r.c->flush();
    EXPECT_EQ(r.hw->writeCalls - w0, 1) << moves << " pointer moves";
    EXPECT_EQ(deviceLed(r), 80);
    EXPECT_EQ(changedOffsets(before, deviceBytes(r)), (std::vector<std::size_t>{lewitt::settings_off::kLedBrightness}));
    std::printf("[ info ] long drag: %d pointer moves, %d settings SET(s)\n", moves, r.hw->writeCalls - w0);
}

TEST(HwSafety10B1, BindingUpdatesClicksAtTheCurrentValueAndReturningDragsSendNothing) {
    app();
    RealLike r(lewitt::guiWritePolicy(false, false, true));
    setDeviceLed(r, 30);
    const int w0 = r.hw->writeCalls;
    LedPage page(*r.c);
    ASSERT_TRUE(page.fader);
    for (int v : {45, 10, 30}) {                              // changes on the device itself rebind the slider
        setDeviceLed(r, v);
        ASSERT_TRUE(waitFor([&] { return std::lround(page.fader->property("value").toDouble()) == v; }));
    }
    page.click(0.30);                                         // the value it already has
    page.mouse(QEvent::MouseButtonPress, 0.30);               // away and back again
    for (int i = 0; i <= 100; ++i) page.mouse(QEvent::MouseMove, 0.30 + 0.4 * i / 100.0);
    for (int i = 0; i <= 100; ++i) page.mouse(QEvent::MouseMove, 0.70 - 0.4 * i / 100.0);
    page.mouse(QEvent::MouseButtonRelease, 0.30);
    for (int i = 0; i < 5; ++i) { QCoreApplication::processEvents(); r.c->flush(); }
    EXPECT_EQ(r.hw->writeCalls, w0);
    EXPECT_EQ(page.text(), "");
}

TEST(HwSafety10B1, RapidRepeatedClicksSendOneSetThenTheControlWaitsForTheResult) {
    app();
    RealLike r(lewitt::guiWritePolicy(false, false, true));
    setDeviceLed(r, 50);
    LedPage page(*r.c);
    ASSERT_TRUE(page.fader);
    const int w0 = r.hw->writeCalls;
    for (int i = 0; i < 30; ++i) page.click(i % 2 ? 0.30 : 0.70);   // no event processing in between
    EXPECT_FALSE(page.fader->isEnabled()) << "disabled while the change is being sent";
    EXPECT_EQ(page.text(), "Sending to the device…");
    ASSERT_TRUE(waitFor([&] { return page.text().startsWith("Verified"); })) << page.text().toStdString();
    for (int i = 0; i < 5; ++i) r.c->flush();
    EXPECT_EQ(r.hw->writeCalls - w0, 1);
    EXPECT_EQ(deviceLed(r), 70);
    EXPECT_TRUE(page.fader->isEnabled());
    page.click(0.70);                                          // duplicate: nothing to send
    for (int i = 0; i < 5; ++i) { QCoreApplication::processEvents(); r.c->flush(); }
    EXPECT_EQ(r.hw->writeCalls - w0, 1);
}

TEST(HwSafety10B1, ManyRequestsWhileAWriteIsInFlightBecomeOneFollowUp) {
    app();
    std::atomic<bool> hold{true}, entered{false};             // outlive the controller (declared first)
    RealLike r(lewitt::guiWritePolicy(false, false, true));
    setDeviceLed(r, 50);
    r.mock.setReadHook([&] {                                  // stays installed: a no-op once released
        entered = true;
        while (hold.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    const int w0 = r.hw->writeCalls;
    r.c->setValue("set.ledBrightness", 10);                    // starts; blocks in its pre-write read
    ASSERT_TRUE(waitFor([&] { return entered.load(); }));
    for (int v = 11; v <= 110; ++v) r.c->setValue("set.ledBrightness", std::min(v, 99));
    hold = false;
    ASSERT_TRUE(waitFor([&] { r.c->flush(); return deviceLed(r) == 99; }));
    for (int i = 0; i < 5; ++i) r.c->flush();
    EXPECT_EQ(r.hw->writeCalls - w0, 2) << "the first request, then only the latest of the 100 waiting ones";
}

TEST(HwSafety10B1, ADeviceChangeBetweenTheTwoPreWriteReadsSendsNothing) {
    app();
    RealLike r(lewitt::guiWritePolicy(false, false, true));
    setDeviceLed(r, 50);
    const int w0 = r.hw->writeCalls;
    r.mock.injectExternalSettingsChange(1, lewitt::settings_off::kSpeakerMute, 1);   // a press on the device
    r.c->setValue("set.ledBrightness", 20);
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("write.outcome").toString() == "not-sent"; }));
    EXPECT_EQ(r.hw->writeCalls, w0);
    EXPECT_EQ(deviceLed(r), 50);
    EXPECT_EQ(r.mock.settingsBytes()[lewitt::settings_off::kSpeakerMute], 1);       // not overwritten
    EXPECT_FALSE(r.c->values().value("write.locked").toBool());                    // nothing was sent
    EXPECT_FALSE(r.c->values().value("pending.set.ledBrightness").toBool());       // not kept for later
    EXPECT_TRUE(r.c->values().value("spk.mute").toBool());
    EXPECT_NE(r.c->values().value("write.message").toString().indexOf("changed while preparing"), -1);
}

TEST(HwSafety10B1, SimulatedSliderStaysLiveWhileDragging) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("set.ledBrightness"); }));
    LedPage page(c);
    ASSERT_TRUE(page.fader);
    EXPECT_FALSE(page.fader->property("commitOnRelease").toBool());
    page.mouse(QEvent::MouseButtonPress, 0.10);
    for (int i = 0; i <= 40; ++i) page.mouse(QEvent::MouseMove, 0.10 + 0.4 * i / 40.0);
    ASSERT_TRUE(waitFor([&] { return c.values().value("set.ledBrightness").toInt() == 50; }));   // before release
    page.mouse(QEvent::MouseButtonRelease, 0.50);
    EXPECT_EQ(page.text(), "");
}

TEST(HwSafety10B1, HardwareCheckUsesTheOpenSessionReadsOnlyAndReportsChangedOffsets) {
    app();
    RealLike r(lewitt::guiWritePolicy(false, false, true), /*hwCheck=*/true);
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("hwcheck.baseline").isValid(); }));
    const int original = deviceLed(r);
    EXPECT_EQ(r.c->values().value("hwcheck.baseline").toInt(), original);
    EXPECT_EQ(r.hw->openCalls, 1);
    const int target = original == 60 ? 40 : 60;
    r.c->setValue("set.ledBrightness", target);
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("write.outcome").toString() == "verified"; }));
    EXPECT_NE(r.c->values().value("hwcheck.report").toString().indexOf("changed: 37 (LED brightness); unexpected: none"), -1)
        << r.c->values().value("hwcheck.report").toString().toStdString();
    const int writes = r.hw->writeCalls, reads = r.hw->readCalls;
    r.c->hwCheckCompare();
    r.c->flush();
    auto report = [&] { return r.c->values().value("hwcheck.report").toString(); };
    ASSERT_TRUE(waitFor([&] { return report().startsWith("now vs baseline"); }));
    const QString after = report();
    EXPECT_EQ(after, QString("now vs baseline: LED brightness %1 -> %2; changed: 37 (LED brightness); unexpected: none; "
                             "values: 37 %1->%2").arg(original).arg(target));
    EXPECT_EQ(r.hw->writeCalls, writes);                      // read-only
    EXPECT_EQ(r.hw->openCalls, 1);                            // no second connection
    EXPECT_GE(r.hw->readCalls - reads, 1);                    // its own GET on the same transport (plus flush's poll)
    r.c->setValue("set.ledBrightness", original);             // restore
    r.c->hwCheckCompare();
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return report().endsWith("changed: none; unexpected: none"); })) << report().toStdString();
    lewitt::Bytes b = r.mock.settingsBytes();                 // something else changes on the device
    b[lewitt::settings_off::kSpeakerMute] ^= 1;
    r.mock.setSettingsBytes(b);
    r.c->hwCheckCompare();
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return report().indexOf("unexpected: 22 (speaker mute); values:") != -1; })) << report().toStdString();
    const QString unexpected = report();
    for (const char* s : {"29c2", "USB", "serial", "0x"}) EXPECT_EQ(unexpected.indexOf(s), -1) << s;
    EXPECT_EQ(r.hw->openCalls, 1);
}

TEST(HwSafety10B1, HardwareCheckIsOffUnlessRequested) {
    app();
    RealLike r(lewitt::guiWritePolicy(false, false, true));
    EXPECT_FALSE(r.c->values().value("hwcheck.enabled").toBool());
    const int reads = r.hw->readCalls;
    r.c->hwCheckRecordBaseline();
    r.c->hwCheckCompare();
    QMetaObject::invokeMethod(r.c.get(), [] {}, Qt::QueuedConnection);
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    EXPECT_EQ(r.hw->readCalls, reads);
    EXPECT_FALSE(r.c->values().value("hwcheck.baseline").isValid());
    QQmlEngine engine;
    auto root = loadMain(*r.c, engine);
    QObject* panel = root->findChild<QObject*>("hwCheckPanel");
    ASSERT_TRUE(panel);
    EXPECT_FALSE(panel->property("visible").toBool());
}

// ---- Phase 10B.2: the hw-check baseline comes from the first successful read, also after a hand-over ----------
namespace {
// A window in --hw-check mode whose fake device takes the real control lease (private runtime directory).
struct HwCheckRig {
    BlockRealUsb volumed;                       // private XDG_RUNTIME_DIR; its held lease = the service owns the device
    lewitt::MockTransport mock;
    lewitt::testing::FakeHardware* hw{nullptr};
    std::unique_ptr<DeviceController> c;
    QTemporaryDir proc;
    HwCheckRig() {
        EXPECT_TRUE(volumed.ready());
        c = std::make_unique<DeviceController>();
        c->enableHardwareCheck();
        auto t = std::make_unique<lewitt::testing::FakeHardware>(&mock);
        t->useLease = true;
        hw = t.get();
        c->connectTransportForTesting(std::move(t), lewitt::guiWritePolicy(false, false, true), proc.path().toStdString());
        c->flush();
    }
    QVariant val(const char* k) const { return c->values().value(k); }
    QString report() const { return val("hwcheck.report").toString(); }
    void setLed(int v) {
        lewitt::Bytes b = mock.settingsBytes();
        b[lewitt::settings_off::kLedBrightness] = static_cast<std::uint8_t>(v);
        mock.setSettingsBytes(b);
    }
};
}  // namespace

TEST(HwCheck10B2, ImmediateConnectRecordsTheBaselineWithoutAnySet) {
    app();
    BlockRealUsb isolate;
    ASSERT_TRUE(isolate.ready());
    isolate.lease.reset();                                     // nobody else owns the device
    RealLike r(lewitt::guiWritePolicy(false, false, true), /*hwCheck=*/true);
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("hwcheck.state").toString() == "recorded"; }));
    EXPECT_EQ(r.c->values().value("hwcheck.baseline").toInt(), deviceLed(r));
    EXPECT_TRUE(r.c->values().value("hwcheck.report").toString().startsWith("baseline recorded from the first successful settings read (connect)"));
    EXPECT_EQ(r.hw->writeCalls, 0);
    EXPECT_EQ(r.hw->openCalls, 1);
}

TEST(HwCheck10B2, LeaseContentionThenHandOverRecordsTheBaselineOnlyFromASuccessfulRead) {
    app();
    HwCheckRig r;
    r.setLed(35);
    // the service holds the device: the window's first attempt fails
    ASSERT_TRUE(waitFor([&] { return r.val("hwcheck.state").toString() == "waiting"; }));
    EXPECT_NE(r.c->connection(), "Connected");
    EXPECT_NE(r.report().indexOf("no baseline yet (connect failed"), -1) << r.report().toStdString();
    EXPECT_FALSE(r.val("hwcheck.baseline").isValid());
    // a retry during the hand-over also fails and asks the service to yield
    r.c->reconnectForTesting();
    const std::string dir = lewitt::ControlLease::currentSessionDirectory();
    EXPECT_TRUE(lewitt::ControlLease::yieldRequestedInDirectory(dir, std::chrono::seconds(30)));
    EXPECT_FALSE(r.val("hwcheck.baseline").isValid());
    // the service lets go, but the first settings read after opening fails: still no baseline, no false message
    r.volumed.lease.reset();
    r.mock.rejectReads(lewitt::MessageId::Settings, 1);
    r.c->reconnectForTesting();
    ASSERT_TRUE(waitFor([&] { return r.report().indexOf("reconnect failed") != -1; })) << r.report().toStdString();
    EXPECT_FALSE(r.val("hwcheck.baseline").isValid());
    EXPECT_EQ(r.report().indexOf("baseline recorded"), -1);
    // the next attempt succeeds: the baseline comes from that read
    r.c->reconnectForTesting();
    ASSERT_TRUE(waitFor([&] { return r.val("hwcheck.state").toString() == "recorded"; })) << r.report().toStdString();
    EXPECT_EQ(r.val("hwcheck.baseline").toInt(), 35);
    EXPECT_TRUE(r.report().startsWith("baseline recorded from the first successful settings read (reconnect)"));
    EXPECT_EQ(r.hw->writeCalls, 0);                            // acquiring the baseline sends nothing
    EXPECT_TRUE(r.hw->holdsLease());                           // the window is the one owner ...
    EXPECT_FALSE(lewitt::ControlLease::acquireForCurrentSession().ok());   // ... no competing session can open
    EXPECT_FALSE(lewitt::ControlLease::yieldRequestedInDirectory(dir, std::chrono::seconds(30)));   // hand-over done
    const int opens = r.hw->openCalls;
    r.c->hwCheckCompare();                                     // comparing reuses the open session
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.report().startsWith("now vs baseline"); }));
    EXPECT_EQ(r.hw->openCalls, opens);
}

TEST(HwCheck10B2, NoRealWriteInDiagnosticModeWhileTheBaselineIsMissing) {
    app();
    HwCheckRig r;                                              // stays without a baseline: the service keeps the device
    ASSERT_TRUE(waitFor([&] { return r.val("hwcheck.state").toString() == "waiting"; }));
    r.c->setValue("set.ledBrightness", 10);
    r.c->flush();
    EXPECT_EQ(r.hw->writeCalls, 0);
    // connected without a baseline (the check switched on after connecting): writes are refused until a read
    BlockRealUsb isolate;
    isolate.lease.reset();
    RealLike q(lewitt::guiWritePolicy(false, false, true));
    const int original = deviceLed(q);
    q.c->enableHardwareCheck();
    q.c->setValue("set.ledBrightness", original == 60 ? 40 : 60);
    q.c->flush();
    ASSERT_TRUE(waitFor([&] { return q.c->values().value("write.outcome").toString() == "refused"; }));
    EXPECT_NE(q.c->values().value("write.message").toString().indexOf("no baseline yet"), -1);
    EXPECT_EQ(q.hw->writeCalls, 0);
    EXPECT_EQ(deviceLed(q), original);
    ASSERT_TRUE(waitFor([&] { q.c->flush(); return q.c->values().value("hwcheck.state").toString() == "recorded"; }));
    q.c->setValue("set.ledBrightness", original == 60 ? 40 : 60);
    q.c->flush();
    ASSERT_TRUE(waitFor([&] { return q.c->values().value("write.outcome").toString() == "verified"; }));
    EXPECT_EQ(q.hw->writeCalls, 1);
    // ordinary (non-diagnostic) mode is unchanged: writes proceed at once
    RealLike n(lewitt::guiWritePolicy(false, false, true));
    n.c->setValue("set.ledBrightness", deviceLed(n) == 60 ? 40 : 60);
    n.c->flush();
    ASSERT_TRUE(waitFor([&] { return n.c->values().value("write.outcome").toString() == "verified"; }));
    EXPECT_EQ(n.hw->writeCalls, 1);
}

TEST(HwCheck10B2, BaselineSurvivesThePermissionReopenAndRepeatedReconnects) {
    app();
    HwCheckRig r;
    r.setLed(35);
    r.volumed.lease.reset();
    r.c->reconnectForTesting();
    ASSERT_TRUE(waitFor([&] { return r.val("hwcheck.state").toString() == "recorded"; }));
    ASSERT_EQ(r.val("hwcheck.baseline").toInt(), 35);
    r.setLed(55);                                              // the device differs from the baseline now
    // permission switch: a new session is opened (the old one closed first, so the lease passes over)
    auto t2 = std::make_unique<lewitt::testing::FakeHardware>(&r.mock);
    t2->useLease = true;
    auto* hw2 = t2.get();
    r.c->connectTransportForTesting(std::move(t2), lewitt::guiWritePolicy(false, false, true), r.proc.path().toStdString());
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.c->connection() == "Connected"; }));
    EXPECT_TRUE(hw2->holdsLease());
    EXPECT_EQ(r.val("hwcheck.baseline").toInt(), 35);
    ASSERT_TRUE(waitFor([&] { return r.report() == "connected again (connect); baseline kept: LED brightness 35"; }))
        << r.report().toStdString();
    for (int v : {60, 65, 70}) {                               // repeated reconnects with other device values
        r.setLed(v);
        r.c->reconnectForTesting();
        r.c->flush();
    }
    EXPECT_EQ(r.val("hwcheck.baseline").toInt(), 35);
    EXPECT_EQ(r.val("hwcheck.replacements").toInt(), 0);
    ASSERT_TRUE(waitFor([&] { return r.report() == "connected again (reconnect); baseline kept: LED brightness 35"; }))
        << r.report().toStdString();
    r.c->hwCheckCompare();
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.report().startsWith("now vs baseline: LED brightness 35 -> 70"); })) << r.report().toStdString();
    EXPECT_EQ(hw2->writeCalls, 0);
}

TEST(HwCheck10B2, ReplacingTheBaselineNeedsTheExplicitTwoStepActionAndIsReported) {
    app();
    HwCheckRig r;
    r.setLed(35);
    r.volumed.lease.reset();
    r.c->reconnectForTesting();
    ASSERT_TRUE(waitFor([&] { return r.val("hwcheck.state").toString() == "recorded"; }));
    r.setLed(80);
    r.c->hwCheckRecordBaseline();                              // "Record" never replaces an existing reference
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.report().startsWith("baseline kept"); }));
    EXPECT_EQ(r.val("hwcheck.baseline").toInt(), 35);
    QQmlEngine engine;
    auto root = loadMain(*r.c, engine);
    root->setProperty("showSettings", true);
    root->findChild<QObject*>("settingsView")->setProperty("open", "device");
    QCoreApplication::processEvents();
    QObject* button = root->findChild<QObject*>("hwCheckBaselineButton");
    QObject* confirm = root->findChild<QObject*>("hwCheckReplaceConfirm");
    QObject* cancel = root->findChild<QObject*>("hwCheckReplaceCancel");
    ASSERT_TRUE(button && confirm && cancel);
    EXPECT_EQ(button->property("text").toString(), "Replace baseline…");
    QMetaObject::invokeMethod(button, "clicked");              // step 1 only arms
    QMetaObject::invokeMethod(cancel, "clicked");
    for (int i = 0; i < 5; ++i) { QCoreApplication::processEvents(); r.c->flush(); }
    EXPECT_EQ(r.val("hwcheck.baseline").toInt(), 35);
    EXPECT_EQ(r.val("hwcheck.replacements").toInt(), 0);
    QMetaObject::invokeMethod(button, "clicked");
    QMetaObject::invokeMethod(confirm, "clicked");             // step 2: deliberate replacement
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.val("hwcheck.replacements").toInt() == 1; }));
    EXPECT_EQ(r.val("hwcheck.baseline").toInt(), 80);
    EXPECT_TRUE(r.report().startsWith("baseline REPLACED by owner action (#1): LED brightness 35 -> 80")) << r.report().toStdString();
    // a failed read or no connection never replaces it
    r.setLed(20);
    r.mock.rejectReads(lewitt::MessageId::Settings, 1);
    r.c->hwCheckReplaceBaseline();
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.report().startsWith("baseline NOT replaced"); }));
    EXPECT_EQ(r.val("hwcheck.baseline").toInt(), 80);
    r.c->disconnectDevice();
    r.c->hwCheckReplaceBaseline();
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.report().startsWith("baseline NOT replaced: not connected"); }));
    EXPECT_EQ(r.val("hwcheck.baseline").toInt(), 80);
    EXPECT_EQ(r.val("hwcheck.replacements").toInt(), 1);
    EXPECT_EQ(r.hw->writeCalls, 0);
}

// ---- Phase 11A: every continuous control sends at most one SET per deliberate change on the real device ------------
#include <QWheelEvent>
namespace {
struct MainView {
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* w{nullptr};
    explicit MainView(DeviceController& c) {
        root = loadMain(c, engine);
        w = qobject_cast<QQuickWindow*>(root.get());
        EXPECT_TRUE(w);
        if (!w) return;
        w->resize(1008, 566);
        w->show();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    }
    QQuickItem* item(const char* name) { return root->findChild<QQuickItem*>(name); }
    void mouse(QQuickItem* it, QEvent::Type type, double fx, double fy) {
        const QPointF scene = it->mapToScene(QPointF(it->width() * fx, it->height() * fy));
        QMouseEvent e(type, scene, scene, w->mapToGlobal(scene.toPoint()), Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
        QGuiApplication::sendEvent(w, &e);
    }
    void wheel(QQuickItem* it, int notches) {
        const QPointF scene = it->mapToScene(QPointF(it->width() / 2, it->height() / 2));
        QWheelEvent e(scene, w->mapToGlobal(scene.toPoint()), QPoint(), QPoint(0, notches > 0 ? 120 : -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(w, &e);
    }
};
lewitt::WritePolicy policyFor(std::initializer_list<std::size_t> offsets) {
    lewitt::WritePolicy p;
    p.allowHardwareWrites = true;
    p.hardwareWriteOffsets = offsets;
    return p;
}
void setDeviceSpeakerDb(RealLike& r, double db) {
    auto m = lewitt::SettingsMessage::fromBytes(r.mock.settingsBytes()).value();
    ASSERT_TRUE(m.setSpeakerVolumeDb(db).ok());
    auto b = m.bytes();
    r.mock.setSettingsBytes(lewitt::Bytes(b.begin(), b.end()));
    ASSERT_TRUE(waitFor([&] { r.c->flush(); return std::fabs(r.c->values().value("spk.volume").toDouble() - db) < 0.01; }));
}
}  // namespace

TEST(Controls11A, LongSpeakerFaderDragSendsOneSetAndStaysWithinTheIncreaseLimit) {
    app();
    RealLike r(lewitt::guiWritePolicy(true, false, false));
    setDeviceSpeakerDb(r, -40.0);
    MainView v(*r.c);
    QQuickItem* fader = v.item("fader_spk");
    ASSERT_TRUE(fader && fader->isEnabled());
    EXPECT_TRUE(fader->property("commitOnRelease").toBool());
    const int w0 = r.hw->writeCalls;
    v.mouse(fader, QEvent::MouseButtonPress, 0.5, 0.8);
    for (int i = 0; i <= 300; ++i) v.mouse(fader, QEvent::MouseMove, 0.5, 0.8 - 0.75 * i / 300.0);   // drag to the top
    for (int i = 0; i < 5; ++i) { QCoreApplication::processEvents(); r.c->flush(); }
    EXPECT_EQ(r.hw->writeCalls, w0) << "dragging previews only";
    v.mouse(fader, QEvent::MouseButtonRelease, 0.5, 0.05);
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("write.outcome").toString() == "verified"; }));
    for (int i = 0; i < 5; ++i) r.c->flush();
    EXPECT_EQ(r.hw->writeCalls - w0, 1);
    EXPECT_LE(r.c->values().value("spk.volume").toDouble(), -30.0 + 1e-6);   // +10 dB at most per SET
    EXPECT_FALSE(r.c->values().value("inflight.spk.volume").toBool());
}

TEST(Controls11A, GainKnobDragAndWheelBurstEachSendOneSet) {
    app();
    RealLike r(policyFor({lewitt::settings_off::kGain[0], lewitt::settings_off::kGain[0] + 1}));
    MainView v(*r.c);
    QQuickItem* knob = v.item("gainKnob1");
    ASSERT_TRUE(knob && knob->isEnabled());
    const double start = r.c->values().value("in1.gain").toDouble();
    int w0 = r.hw->writeCalls;
    v.mouse(knob, QEvent::MouseButtonPress, 0.5, 0.5);
    for (int i = 0; i <= 200; ++i) v.mouse(knob, QEvent::MouseMove, 0.5, 0.5 + 0.3 * i / 200.0);     // downwards = less gain
    for (int i = 0; i < 5; ++i) { QCoreApplication::processEvents(); r.c->flush(); }
    EXPECT_EQ(r.hw->writeCalls, w0);
    v.mouse(knob, QEvent::MouseButtonRelease, 0.5, 0.8);
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("write.outcome").toString() == "verified"; }));
    EXPECT_EQ(r.hw->writeCalls - w0, 1);
    const double afterDrag = r.c->values().value("in1.gain").toDouble();
    EXPECT_LT(afterDrag, start);
    // twelve wheel notches in a burst: one SET after the wheel rests
    w0 = r.hw->writeCalls;
    for (int i = 0; i < 12; ++i) v.wheel(knob, -1);
    for (int i = 0; i < 3; ++i) { QCoreApplication::processEvents(); r.c->flush(); }
    EXPECT_EQ(r.hw->writeCalls, w0) << "nothing before the wheel rests";
    ASSERT_TRUE(waitFor([&] { r.c->flush(); return r.hw->writeCalls > w0; }, 3000));
    for (int i = 0; i < 5; ++i) { QCoreApplication::processEvents(); r.c->flush(); }
    EXPECT_EQ(r.hw->writeCalls - w0, 1);
    EXPECT_NEAR(r.c->values().value("in1.gain").toDouble(), std::max(0.0, afterDrag - 12.0), 0.01);
}

TEST(Controls11A, MixKnobWritesTheDeviceScaleOnce) {
    app();
    RealLike r(policyFor({lewitt::settings_off::kInputPlaybackMix, lewitt::settings_off::kInputPlaybackMix + 1}));
    MainView v(*r.c);
    QQuickItem* knob = v.item("monitorMixKnob");
    ASSERT_TRUE(knob && knob->isEnabled());
    const int w0 = r.hw->writeCalls;
    v.mouse(knob, QEvent::MouseButtonPress, 0.5, 0.5);
    for (int i = 0; i <= 100; ++i) v.mouse(knob, QEvent::MouseMove, 0.5, 0.5 + 0.3 * i / 100.0);
    v.mouse(knob, QEvent::MouseButtonRelease, 0.5, 0.8);
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("write.outcome").toString() == "verified"; }));
    EXPECT_EQ(r.hw->writeCalls - w0, 1);
    const double mix = lewitt::SettingsMessage::fromBytes(r.mock.settingsBytes()).value().inputPlaybackMix();
    EXPECT_GE(mix, -100.0);
    EXPECT_LT(mix, -50.0);                                    // moved left from the simulated default -50
}

TEST(Controls11A, SimulatedControlsStayLive) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.values().contains("spk.volume"); }));
    MainView v(c);
    QQuickItem* fader = v.item("fader_spk");
    ASSERT_TRUE(fader);
    EXPECT_FALSE(fader->property("commitOnRelease").toBool());
    const double before = c.values().value("spk.volume").toDouble();
    v.mouse(fader, QEvent::MouseButtonPress, 0.5, 0.5);
    for (int i = 0; i <= 20; ++i) v.mouse(fader, QEvent::MouseMove, 0.5, 0.5 + 0.3 * i / 20.0);
    ASSERT_TRUE(waitFor([&] { return std::fabs(c.values().value("spk.volume").toDouble() - before) > 1.0; }));   // before release
    v.mouse(fader, QEvent::MouseButtonRelease, 0.5, 0.8);
}

// ---- Phase 11A: grouped permissions on (fake) real hardware ---------------------------------------------------------
namespace {
lewitt::UserConfig withPermission(const char* name) {
    lewitt::UserConfig c;
    c.*(lewitt::findDevicePermission(name)->field) = true;
    return c;
}
// Repeater delegates have no QObject parent: search the visual item tree.
QQuickItem* findItem(QQuickItem* from, const QString& name) {
    if (!from) return nullptr;
    if (from->objectName() == name) return from;
    for (QQuickItem* c : from->childItems())
        if (QQuickItem* f = findItem(c, name)) return f;
    return nullptr;
}
std::vector<std::size_t> footprintOf(const char* key) {
    auto v = lewitt::controlFootprint(key)->settingsOffsets;
    std::sort(v.begin(), v.end());
    return v;
}
}  // namespace

TEST(Permissions11AGui, EachGroupWritesOnlyItsControlsBytes) {
    app();
    struct Case { const char* perm; const char* key; QVariant value; };
    const Case cases[] = {
        {"inputs", "in1.mute", true}, {"inputs", "set.gainLock", true},
        {"processing", "in2.preamp", 2}, {"processing", "in1.highPass", true},
        {"monitoring", "out.dim", true}, {"monitoring", "mon.mix", -80.0},
        {"appearance", "set.led.icons", QString("#112233")}, {"appearance", "set.customButton", 8},
        {"phantom", "in1.phantom", true}, {"outputModes", "set.highImpedance", true},
    };
    for (const auto& c : cases) {
        RealLike r(lewitt::guiWritePolicy(withPermission(c.perm)));
        ASSERT_TRUE(r.can(c.key)) << c.key;
        const lewitt::Bytes before = deviceBytes(r);
        r.c->setValue(c.key, c.value);
        r.c->flush();
        ASSERT_TRUE(waitFor([&] { return r.c->values().value("write.outcome").toString() == "verified"; })) << c.key;
        const auto changed = changedOffsets(before, deviceBytes(r));
        EXPECT_FALSE(changed.empty()) << c.key;
        for (auto off : changed) {
            const auto fp = footprintOf(c.key);
            EXPECT_TRUE(std::find(fp.begin(), fp.end(), off) != fp.end()) << c.key << " changed byte " << off;
        }
        EXPECT_EQ(r.hw->writeCalls, 1) << c.key;
        // a control of another group stays refused
        r.c->setValue(std::string(c.perm) == "inputs" ? "out.mono" : "in2.mute", true);
        r.c->flush();
        ASSERT_TRUE(waitFor([&] { return r.c->values().value("write.outcome").toString() == "refused"; })) << c.key;
        EXPECT_EQ(r.hw->writeCalls, 1) << c.key;
    }
}

TEST(Permissions11AGui, RealDeviceGainRisesAtMostSixDbPerSet) {
    app();
    RealLike r(lewitt::guiWritePolicy(withPermission("inputs")));
    const double start = r.c->values().value("in1.gain").toDouble();
    r.c->setValue("in1.gain", start + 30.0);
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("write.outcome").toString() == "verified"; }));
    EXPECT_NEAR(r.c->values().value("in1.gain").toDouble(), start + 6.0, 0.01);
}

TEST(Permissions11AGui, AutogainSendsOnlyMeasurementTriggersAndNoSettingsWrite) {
    app();
    RealLike r(lewitt::guiWritePolicy(withPermission("measurements")));
    ASSERT_TRUE(r.can("autogain1"));
    EXPECT_FALSE(r.can("in1.gain"));
    r.c->trigger("autogain1");
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { r.c->flush(); return r.mock.triggers().size() >= 2; }));
    const auto tr = r.mock.triggers();
    EXPECT_EQ(tr[0], static_cast<std::uint16_t>(lewitt::Trigger::BeginMeasurementSession));
    EXPECT_EQ(tr[1], static_cast<std::uint16_t>(lewitt::Trigger::AutoGainMicrophone));
    EXPECT_EQ(lewitt::testing::settingsWrites(r.mock), 0);
    RealLike none(lewitt::guiWritePolicy(lewitt::UserConfig{}));
    none.c->trigger("autogain1");
    none.c->flush();
    EXPECT_TRUE(none.mock.triggers().empty());
}

TEST(Permissions11AGui, NewSwitchesNeedTheirConfirmationAndGrantOnlyThemselves) {
    app();
    writeConfigText("[openwitty]\n");
    Calls calls;
    auto s = makeSettings(calls);
    DeviceController c;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("device", &c);
    engine.rootContext()->setContextProperty("integration", s.get());
    QQmlComponent comp(&engine, QUrl::fromLocalFile(QStringLiteral(LEWITT_QML_DIR "/Main.qml")));
    std::unique_ptr<QObject> root(comp.create());
    ASSERT_TRUE(root);
    root->setProperty("showSettings", true);
    root->findChild<QObject*>("settingsView")->setProperty("open", "device");
    QCoreApplication::processEvents();
    QObject* dialog = root->findChild<QObject*>("confirmWriteDialog");
    QObject* cancel = root->findChild<QObject*>("confirmWriteCancel");
    QObject* enable = root->findChild<QObject*>("confirmWriteEnable");
    ASSERT_TRUE(dialog && cancel && enable);
    for (const char* n : {"inputs", "processing", "monitoring", "appearance", "phantom", "outputModes", "measurements"}) {
        auto* win = qobject_cast<QQuickWindow*>(root.get());
        ASSERT_TRUE(win);
        QObject* sw = findItem(win->contentItem(), QString("permSwitch_") + n);
        ASSERT_TRUE(sw) << n;
        QMetaObject::invokeMethod(sw, "toggled", Q_ARG(bool, true));
        EXPECT_TRUE(dialog->property("visible").toBool()) << n;
        EXPECT_EQ(dialog->property("field").toString(), n);
        QMetaObject::invokeMethod(cancel, "clicked");
        EXPECT_FALSE(s->permissions().value(n).toBool()) << n;
        QMetaObject::invokeMethod(sw, "toggled", Q_ARG(bool, true));
        QMetaObject::invokeMethod(enable, "clicked");
        EXPECT_TRUE(s->permissions().value(n).toBool()) << n;
        int on = 0;
        for (const auto& v : s->permissions()) on += v.toBool();
        EXPECT_EQ(on, 1) << n;
        QMetaObject::invokeMethod(sw, "toggled", Q_ARG(bool, false));                 // off without a dialog
        EXPECT_FALSE(s->permissions().value(n).toBool()) << n;
        EXPECT_FALSE(dialog->property("visible").toBool()) << n;
    }
    s->setPermission("inputs", true, false);                                         // API refuses unconfirmed
    EXPECT_FALSE(s->permissions().value("inputs").toBool());
    s->setPermission("everything", true, true);                                       // unknown: no-op
    EXPECT_FALSE(lewitt::anyDeviceWritePermission(onDisk()));
}

TEST(Permissions11AGui, HardwareCheckExpectsTheBytesOfWrittenControlsOnly) {
    app();
    BlockRealUsb isolate;
    isolate.lease.reset();
    RealLike r(lewitt::guiWritePolicy(withPermission("inputs")), /*hwCheck=*/true);
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("hwcheck.state").toString() == "recorded"; }));
    r.c->setValue("in1.mute", true);
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("write.outcome").toString() == "verified"; }));
    lewitt::Bytes b = r.mock.settingsBytes();                 // and a change on the device itself
    b[lewitt::settings_off::kHeadphoneMute] ^= 1;
    r.mock.setSettingsBytes(b);
    r.c->hwCheckCompare();
    r.c->flush();
    auto report = [&] { return r.c->values().value("hwcheck.report").toString(); };
    ASSERT_TRUE(waitFor([&] { return report().startsWith("now vs baseline"); }));
    EXPECT_NE(report().indexOf("changed: 2 (mic mute), 29 (headphone mute)"), -1) << report().toStdString();
    EXPECT_NE(report().indexOf("unexpected: 29 (headphone mute); values: 2 0->1 29 "), -1) << report().toStdString();
}

TEST(Permissions11AGui, TriggersStopAfterAnUnconfirmedWriteLikeSettingsWrites) {
    app();
    lewitt::UserConfig cfg = withPermission("measurements");
    cfg.ledBrightnessControl = true;
    RealLike r(lewitt::guiWritePolicy(cfg));
    r.hw->dropWrites = true;                                   // the next settings SET is acknowledged but ignored
    const int original = r.c->values().value("set.ledBrightness").toInt();
    r.c->setValue("set.ledBrightness", original == 60 ? 40 : 60);
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("write.locked").toBool(); }));
    r.c->trigger("autogain1");
    r.c->flush();
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("write.outcome").toString() == "refused"; }));
    EXPECT_TRUE(r.mock.triggers().empty());
}

// ---- Phase 10B: monitoring-mix display ---------------------------------------------------------------------------
namespace {
void setRawMix(lewitt::MockTransport& mock, double deviceValue) {   // the field is stored sign-inverted Q8.8
    lewitt::Bytes b = mock.settingsBytes();
    const auto q = static_cast<std::int16_t>(std::lround(-deviceValue * 256.0));
    b[lewitt::settings_off::kInputPlaybackMix] = static_cast<std::uint8_t>(q & 0xff);
    b[lewitt::settings_off::kInputPlaybackMix + 1] = static_cast<std::uint8_t>((q >> 8) & 0xff);
    mock.setSettingsBytes(b);
}
}  // namespace

TEST(MonitorMix, RealDeviceValuesAreShownOnTheObservedScaleAndClamped) {
    app();
    RealLike r{lewitt::WritePolicy{}};
    QQmlEngine engine;
    auto root = loadMain(*r.c, engine);
    ASSERT_TRUE(root);
    QObject* knob = root->findChild<QObject*>("monitorMixKnob");
    QObject* readout = root->findChild<QObject*>("monitorMixReadout");
    ASSERT_TRUE(knob && readout);
    struct Case { double device, frac; const char* text; };
    for (const Case& c : {Case{-100, 0.0, "device value -100 (of -100...0)"}, Case{-50, 0.5, "device value -50 (of -100...0)"},
                          Case{0, 1.0, "device value 0 (of -100...0)"}, Case{-120, 0.0, "device value -120 (of -100...0)"},
                          Case{5, 1.0, "device value 5 (of -100...0)"}}) {
        setRawMix(r.mock, c.device);
        ASSERT_TRUE(waitFor([&] {
            r.c->flush();
            return std::fabs(r.c->values().value("mon.mix").toDouble() - c.device) < 0.01;
        })) << c.device;
        QCoreApplication::processEvents();
        EXPECT_NEAR(knob->property("frac").toDouble(), c.frac, 1e-9) << c.device;   // out-of-range values are clamped
        EXPECT_TRUE(readout->property("visible").toBool());
        EXPECT_EQ(readout->property("text").toString(), c.text);
    }
    EXPECT_FALSE(knob->property("enabled").toBool());          // display only on the real device
    EXPECT_FALSE(r.can("mon.mix"));
}

TEST(MonitorMix, SimulatorUsesTheDeviceScaleAndHasNoReadout) {
    app();
    DeviceController c;
    QQmlEngine engine;
    auto root = loadMain(c, engine);
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.connection() == "Connected" && c.values().contains("mon.mix"); }));
    QCoreApplication::processEvents();
    QObject* knob = root->findChild<QObject*>("monitorMixKnob");
    QObject* readout = root->findChild<QObject*>("monitorMixReadout");
    ASSERT_TRUE(knob && readout);
    EXPECT_DOUBLE_EQ(knob->property("from").toDouble(), -100.0);       // device scale, also simulated (Phase 11A)
    EXPECT_DOUBLE_EQ(knob->property("to").toDouble(), 0.0);
    EXPECT_FALSE(readout->property("visible").toBool());
    c.setValue("mon.mix", 0.0);
    c.flush();
    ASSERT_TRUE(waitFor([&] { return std::fabs(c.values().value("mon.mix").toDouble() - 0.0) < 0.01; }));
    QCoreApplication::processEvents();
    EXPECT_NEAR(knob->property("frac").toDouble(), 1.0, 1e-9);
    lewitt::SettingsMessage m = lewitt::SettingsMessage::fromBytes(lewitt::Bytes(0x80, 0)).value();
    EXPECT_TRUE(m.setInputPlaybackMix(-100.0).ok());
    EXPECT_FALSE(m.setInputPlaybackMix(1.0).ok());
}

// ---- Phase 10B: real sample rate and DSP availability ------------------------------------------------------------
TEST(SampleRate10B, UnknownOnAnIdleOrMissingDeviceAndNeverTheSimulatedDefault) {
    app();
    RealLike r{lewitt::WritePolicy{}};                       // fixture /proc/asound without the card
    EXPECT_FALSE(r.c->values().value("sampleRate").isValid());
    EXPECT_EQ(r.c->values().value("sampleRate.source").toString(), "unknown");
    EXPECT_EQ(r.c->values().value("dsp.state").toString(), "unknown");
    r.c->setSampleRate(96000);                               // the simulator's selector cannot change a real session
    r.c->flush();
    EXPECT_FALSE(r.c->values().value("sampleRate").isValid());
    EXPECT_EQ(r.c->values().value("dsp.state").toString(), "unknown");
    QQmlEngine engine;
    auto root = loadMain(*r.c, engine);
    ASSERT_TRUE(root);
    bool unknownShown = false;
    for (QObject* o : root->findChildren<QObject*>())
        if (o->property("text").toString() == "Unknown") unknownShown = true;
    EXPECT_TRUE(unknownShown);
}

TEST(SampleRate10B, AConfirmed96kHzMakesDenoiseAndCompUnavailable) {
    app();
    RealLike r{lewitt::WritePolicy{}};
    const QString card = r.proc.path() + "/card3";
    QDir().mkpath(card + "/pcm0p/sub0");
    QFile id(card + "/usbid"); ASSERT_TRUE(id.open(QIODevice::WriteOnly)); id.write("29c2:0004\n"); id.close();
    QFile hw(card + "/pcm0p/sub0/hw_params"); ASSERT_TRUE(hw.open(QIODevice::WriteOnly));
    hw.write("access: MMAP_INTERLEAVED\nrate: 96000 (96000/1)\n"); hw.close();
    r.c->refreshSampleRateForTesting();
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("sampleRate").toInt() == 96000; }));
    EXPECT_EQ(r.c->values().value("sampleRate.source").toString(), "device");
    EXPECT_EQ(r.c->values().value("dsp.state").toString(), "unavailable");
    EXPECT_FALSE(r.c->values().value("dsp.available").toBool());
    QFile hw48(card + "/pcm0p/sub0/hw_params"); ASSERT_TRUE(hw48.open(QIODevice::WriteOnly | QIODevice::Truncate));
    hw48.write("rate: 48000 (48000/1)\n"); hw48.close();
    r.c->refreshSampleRateForTesting();
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("dsp.state").toString() == "available"; }));
}

TEST(SampleRate10B, MockModeKeepsItsSimulatedRate) {
    app();
    DeviceController c;
    c.connectMock();
    ASSERT_TRUE(waitFor([&] { return c.connection() == "Connected" && c.values().contains("dsp.state"); }));
    EXPECT_EQ(c.values().value("sampleRate.source").toString(), "simulated");
    EXPECT_EQ(c.values().value("sampleRate").toInt(), 48000);
    EXPECT_EQ(c.values().value("dsp.state").toString(), "available");
    c.setSampleRate(96000);
    ASSERT_TRUE(waitFor([&] { return c.values().value("dsp.state").toString() == "unavailable"; }));
}

// ---- Phase 10B: GUI preferences ----------------------------------------------------------------------------------
#include "GuiPreferences.hpp"

TEST(GuiPreferences10B, DefaultsPersistenceAndSafeFallback) {
    app();
    QTemporaryDir d;
    const QString path = d.path() + "/openwitty/gui.conf";
    {
        GuiPreferences p(path);
        EXPECT_FALSE(p.onboardingDone());                    // missing file: first run
        EXPECT_TRUE(p.showTooltips());
        p.setOnboardingDone(true);
        p.setShowTooltips(false);
    }
    GuiPreferences again(path);
    EXPECT_TRUE(again.onboardingDone());
    EXPECT_FALSE(again.showTooltips());
    for (const char* text : {"onboarding_done = maybe\nshow_tooltips = 0\n", "onboarding_done = true\nonboarding_done = true\n",
                             "\x01garbage\n", "show_tooltips = false\nshow_tooltips = false\n"}) {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
        f.close();
        GuiPreferences p(path);
        EXPECT_FALSE(p.onboardingDone()) << text;            // invalid or ambiguous: defaults
        EXPECT_TRUE(p.showTooltips()) << text;
    }
}

TEST(GuiPreferences10B, SeparateFromDeviceWritePermissions) {
    app();
    writeConfigText("[openwitty]\ndesktop_volume_sync = mirror\nled_brightness_control = true\n");
    const lewitt::UserConfig before = onDisk();
    GuiPreferences p;                                        // default path, next to openwitty.conf
    EXPECT_TRUE(p.path().endsWith("/openwitty/gui.conf"));
    p.setShowTooltips(!p.showTooltips());
    p.setOnboardingDone(!p.onboardingDone());
    EXPECT_EQ(onDisk(), before);                             // the device permissions file is untouched
}

TEST(GuiPreferences10B, OnboardingFinishAndTooltipSwitchAreRemembered) {
    app();
    QTemporaryDir d;
    GuiPreferences prefs(d.path() + "/gui.conf");
    DeviceController c;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("prefs", &prefs);
    auto root = loadMain(c, engine);
    ASSERT_TRUE(root);
    root->setProperty("showOnboarding", true);
    QObject* overlay = root->findChild<QObject*>("onboardingOverlay");
    ASSERT_TRUE(overlay);
    QMetaObject::invokeMethod(overlay, "finished");
    EXPECT_FALSE(root->property("showOnboarding").toBool());
    EXPECT_TRUE(prefs.onboardingDone());
    root->setProperty("showSettings", true);
    QCoreApplication::processEvents();
    QObject* tips = root->findChild<QObject*>("showTooltipsSwitch");
    ASSERT_TRUE(tips);
    QMetaObject::invokeMethod(tips, "toggled", Q_ARG(bool, false));
    EXPECT_FALSE(prefs.showTooltips());
    GuiPreferences reread(prefs.path());
    EXPECT_FALSE(reread.showTooltips());
    EXPECT_TRUE(reread.onboardingDone());
}

TEST(GuiPreferences10B, FirstRunDecisionIsDeterministic) {
    app();
    QTemporaryDir d;
    GuiPreferences p(d.path() + "/gui.conf");
    EXPECT_TRUE(p.showOnboardingAtStart(false, false));     // first interactive start
    EXPECT_FALSE(p.showOnboardingAtStart(true, false));     // screenshot runs never
    EXPECT_FALSE(p.showOnboardingAtStart(false, true));     // an explicit --show page wins
    p.setOnboardingDone(true);
    EXPECT_FALSE(p.showOnboardingAtStart(false, false));    // remembered
}

// Phase 11B session 1 finding: with Settings open, a wheel turn on an empty part of the page reached the
// instrument gain knob underneath and lowered the gain on the real device. The page must swallow it.
TEST(Overlay11B, SettingsPageBlocksWheelAndClicksToTheMainView) {
    app();
    namespace o = lewitt::settings_off;
    RealLike r(policyFor({o::kGain[1], o::kGain[1] + 1, o::kMute[0], o::kMute[1]}));
    MainView v(*r.c);
    QQuickItem* knob = v.item("gainKnob2");
    ASSERT_TRUE(knob && knob->isEnabled());
    const double gain = r.c->values().value("in2.gain").toDouble();
    v.root->setProperty("showSettings", true);
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    ASSERT_TRUE(v.item("settingsInputBlocker"));
    const int w0 = r.hw->writeCalls;
    for (int i = 0; i < 4; ++i) v.wheel(knob, -1);
    v.mouse(knob, QEvent::MouseButtonPress, 0.5, 0.5);
    v.mouse(knob, QEvent::MouseMove, 0.5, 0.9);
    v.mouse(knob, QEvent::MouseButtonRelease, 0.5, 0.9);
    QElapsedTimer t; t.start();
    while (t.elapsed() < 700) { QCoreApplication::processEvents(); r.c->flush(); }   // past the 400 ms wheel rest
    EXPECT_EQ(r.hw->writeCalls, w0) << "nothing reaches the knob under the settings page";
    EXPECT_DOUBLE_EQ(r.c->values().value("in2.gain").toDouble(), gain);
    // control check: with the page closed the same wheel does reach the knob
    v.root->setProperty("showSettings", false);
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    v.wheel(knob, -1);
    ASSERT_TRUE(waitFor([&] { r.c->flush(); return r.hw->writeCalls > w0; }, 3000));
}

// A device colour that is not one of the swatches stays selectable after a swatch was tried, so the original
// colour can be restored exactly.
TEST(Overlay11B, NonSwatchDeviceColourStaysRestorable) {
    app();
    namespace o = lewitt::settings_off;
    RealLike r(policyFor({o::kLedColorMute, o::kLedColorMute + 1, o::kLedColorMute + 2}));
    auto m = lewitt::SettingsMessage::fromBytes(r.mock.settingsBytes()).value();
    ASSERT_TRUE(m.setLedColor(lewitt::LedElement::Mute, lewitt::Rgb{0x12, 0x34, 0x56}).ok());
    auto b = m.bytes();
    r.mock.setSettingsBytes(lewitt::Bytes(b.begin(), b.end()));
    ASSERT_TRUE(waitFor([&] { r.c->flush(); return r.c->values().value("set.led.mute").toString() == "#123456"; }));
    MainView v(*r.c);
    QObject* sv = v.root->findChild<QObject*>("settingsView");
    v.root->setProperty("showSettings", true);
    sv->setProperty("open", "leds");
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    QQuickItem* custom = findItem(v.w->contentItem(), "swatch_mute_7");
    ASSERT_TRUE(custom) << "an eighth swatch shows the device's own colour";
    EXPECT_TRUE(custom->property("sel").toBool());
    QQuickItem* red = findItem(v.w->contentItem(), "swatch_mute_0");
    ASSERT_TRUE(red && red->isEnabled());
    v.mouse(red, QEvent::MouseButtonPress, 0.5, 0.5);
    v.mouse(red, QEvent::MouseButtonRelease, 0.5, 0.5);
    ASSERT_TRUE(waitFor([&] { r.c->flush(); return r.c->values().value("set.led.mute").toString() == "#ff0000"; }));
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    custom = findItem(v.w->contentItem(), "swatch_mute_7");
    ASSERT_TRUE(custom) << "the device colour is still offered after trying a swatch";
    v.mouse(custom, QEvent::MouseButtonPress, 0.5, 0.5);
    v.mouse(custom, QEvent::MouseButtonRelease, 0.5, 0.5);
    ASSERT_TRUE(waitFor([&] { r.c->flush(); return r.c->values().value("set.led.mute").toString() == "#123456"; }));
    const auto after = r.mock.settingsBytes();
    EXPECT_EQ(after[o::kLedColorMute], 0x12);
    EXPECT_EQ(after[o::kLedColorMute + 1], 0x34);
    EXPECT_EQ(after[o::kLedColorMute + 2], 0x56);
}

// Phase 11B session 1: after a wheel commit the knob showed the old value for a moment until the device confirmed
// the new one. The committed value now stays shown until the device reports it, or 1.5 s pass (refused write).
TEST(Controls11B, CommittedValueStaysShownUntilTheDeviceReportsIt) {
    app();
    for (const char* file : {"/GainKnob.qml", "/Fader.qml"}) {
        QQmlEngine engine;
        QQmlComponent comp(&engine, QUrl::fromLocalFile(QString(LEWITT_QML_DIR) + file));
        for (const auto& e : comp.errors()) ADD_FAILURE() << e.toString().toStdString();
        std::unique_ptr<QObject> k(comp.create());
        ASSERT_TRUE(k) << file;
        k->setProperty("from", -60); k->setProperty("to", 72);
        k->setProperty("commitOnRelease", true);
        k->setProperty("value", 27.0);
        k->setProperty("previewValue", 24.0);
        QMetaObject::invokeMethod(k.get(), "commit", Q_ARG(QVariant, 24.0));
        QCoreApplication::processEvents();
        EXPECT_DOUBLE_EQ(k->property("shownValue").toDouble(), 24.0) << file << ": no flash of the old value";
        k->setProperty("value", 24.0);    // device confirmed
        EXPECT_FALSE(k->property("awaiting").toBool()) << file;
        EXPECT_DOUBLE_EQ(k->property("shownValue").toDouble(), 24.0) << file;
        // refused / never confirmed: falls back to the device value
        k->setProperty("previewValue", 20.0);
        QMetaObject::invokeMethod(k.get(), "commit", Q_ARG(QVariant, 20.0));
        EXPECT_DOUBLE_EQ(k->property("shownValue").toDouble(), 20.0) << file;
        QElapsedTimer t; t.start();
        while (t.elapsed() < 1700) QCoreApplication::processEvents();
        EXPECT_DOUBLE_EQ(k->property("shownValue").toDouble(), 24.0) << file;
    }
}

// Phase 11B session 2: the output faders show their value in dB, including while a drag is previewed.
TEST(Controls11B, OutputFadersShowTheirValueInDb) {
    app();
    RealLike r(lewitt::guiWritePolicy(true, false, false));
    setDeviceSpeakerDb(r, -17.5);
    MainView v(*r.c);
    QQuickItem* label = findItem(v.w->contentItem(), "faderValue_spk");
    ASSERT_TRUE(label);
    EXPECT_EQ(label->property("text").toString(), "-17.5");
    QQuickItem* fader = v.item("fader_spk");
    v.mouse(fader, QEvent::MouseButtonPress, 0.5, 0.5);
    v.mouse(fader, QEvent::MouseMove, 0.5, 0.6);
    QCoreApplication::processEvents();
    const double preview = fader->property("previewValue").toDouble();
    EXPECT_NE(preview, -17.5);
    EXPECT_EQ(label->property("text").toString(), QString::number(preview, 'f', 1)) << "shows the previewed value";
    v.mouse(fader, QEvent::MouseButtonRelease, 0.5, 0.6);
    ASSERT_TRUE(waitFor([&] { return r.c->values().value("write.outcome").toString() == "verified"; }));
    for (int i = 0; i < 5; ++i) { QCoreApplication::processEvents(); r.c->flush(); }
    EXPECT_EQ(label->property("text").toString(), QString::number(r.c->values().value("spk.volume").toDouble(), 'f', 1));
}

// Phase 11B LB-1 / 11C: the desktop page warns that the desktop input mute does not silence the CONNECT 2, unless
// OpenWitty's software input mute is installed.
TEST(Controls11B, DesktopPageWarnsThatTheDesktopMicMuteIsNotEffective) {
    app();
    namespace fs = std::filesystem;
    const fs::path h = fs::temp_directory_path() / ("ow-gui-11c-" + std::to_string(QCoreApplication::applicationPid()));
    fs::remove_all(h);
    fs::create_directories(h);
    const QByteArray oldXdg = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", QByteArray::fromStdString(h.string()));
    RealLike r(lewitt::guiWritePolicy(false, false, false));
    MainView v(*r.c);
    v.root->setProperty("showSettings", true);
    v.root->findChild<QObject*>("settingsView")->setProperty("open", "desktop");
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    QQuickItem* w = findItem(v.w->contentItem(), "desktopMicMuteWarning");
    ASSERT_TRUE(w);
    EXPECT_TRUE(w->isVisible());
    EXPECT_TRUE(w->property("text").toString().contains("does not mute the CONNECT 2"));
    for (const char* f : {"wireplumber/wireplumber.conf.d/52-openwitty-connect2-input.conf",
                          "alsa-card-profile/mixer/profile-sets/openwitty-connect2.conf",
                          "alsa-card-profile/mixer/paths/openwitty-connect2-input.conf"}) {
        fs::create_directories((h / f).parent_path());
        std::ofstream(h / f) << "x";
    }
    fakeIntegration()->refresh();
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    EXPECT_TRUE(w->property("text").toString().contains("software mute installed"));
    if (oldXdg.isNull()) qunsetenv("XDG_CONFIG_HOME"); else qputenv("XDG_CONFIG_HOME", oldXdg);
    fakeIntegration()->refresh();
    fs::remove_all(h);
}
