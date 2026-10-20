// GUI <-> backend integration (simulated device, offscreen). Not hardware tests.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <cmath>
#include <functional>
#include <optional>

#include "DeviceController.hpp"
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
        {"simulated", true}, {"values", QVariantMap{{"mon.mix", 0.5}}}});
    std::unique_ptr<QObject> mockSection(comp.create());
    ASSERT_TRUE(mockSection);
    auto* mockKnob = mockSection->findChild<QObject*>("monitorMixKnob");
    ASSERT_TRUE(mockKnob);
    EXPECT_DOUBLE_EQ(mockKnob->property("from").toDouble(), -1.0);
    EXPECT_DOUBLE_EQ(mockKnob->property("to").toDouble(), 1.0);
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
