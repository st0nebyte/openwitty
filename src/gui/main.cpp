#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QQuickWindow>
#include <QTimer>
#include <cstdio>
#include <memory>

#include "DeviceController.hpp"
#include "GuiActivation.hpp"
#include "InstanceChannel.hpp"
#include "IntegrationSettings.hpp"
#include "lewitt/version.hpp"

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("openwitty");
    app.setApplicationDisplayName(LEWITT_APP_NAME);
    app.setApplicationVersion(LEWITT_VERSION_STRING);
    app.setOrganizationName("openwitty");
    // Installed builds resolve the themed icon; development runs use the embedded PNG.
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("openwitty"), QIcon(QStringLiteral(":/openwitty.png"))));
    QGuiApplication::setDesktopFileName(LEWITT_APP_DESKTOP_ID);   // Wayland app-id / icon matching
    QCommandLineParser p;
    QCommandLineOption mock("mock", "Start with the simulated device.");
    p.addOption(mock);
    QCommandLineOption legacyPolling("diagnostic-legacy-polling",
        "DIAGNOSTIC: use the former read-only 2 Hz settings / 4 Hz status cadence (default 16/48 Hz).");
    p.addOption(legacyPolling);
    QCommandLineOption shot("screenshot", "Save a screenshot to <file> and exit.", "file");
    p.addOption(shot);
    QCommandLineOption sizeOpt("size", "Window size WxH (for screenshots).", "WxH");
    p.addOption(sizeOpt);
    QCommandLineOption show("show", "Open a page at start: settings | settings-desktop | wizard | onboarding.", "page");
    p.addOption(show);
    QCommandLineOption quitAfterSeconds("quit-after-seconds",
        "DIAGNOSTIC: quit normally after 1–1800 seconds.", "seconds");
    p.addOption(quitAfterSeconds);
    p.setApplicationDescription(QStringLiteral(LEWITT_NOTICE));
    p.addHelpOption();
    p.addVersionOption();
    p.process(app);
    if (p.isSet(mock) && p.isSet(legacyPolling)) {
        std::fputs("--diagnostic-legacy-polling requires a real read-only device, not --mock\n", stderr);
        return 64;
    }
    bool durationOk = false;
    const int quitDuration = p.value(quitAfterSeconds).toInt(&durationOk);
    if (p.isSet(quitAfterSeconds) && (!durationOk || quitDuration < 1 || quitDuration > 1800)) {
        std::fputs("--quit-after-seconds requires a duration of 1–1800 seconds\n", stderr);
        return 64;
    }

    // One OpenWitty window per user session: a second start (launcher, tray) brings the running window to
    // the front instead - before any device code runs, so there is never a second device session.
    // Screenshot runs (offscreen tests, packaging checks) are independent of a running window.
    std::unique_ptr<InstanceChannel> instance;
    if (!p.isSet(shot)) {
        const GuiActivation request{p.value(show), qEnvironmentVariable("XDG_ACTIVATION_TOKEN")};
        instance = std::make_unique<InstanceChannel>("gui");
        switch (instance->claimOrForward(encodeGuiActivation(request))) {
            case InstanceChannel::Role::Forwarded:
                std::puts("OpenWitty is already open: its window was brought to the front.");
                return 0;
            case InstanceChannel::Role::Failed:
                std::fprintf(stderr, "openwitty: not starting a second window: %s\n", qPrintable(instance->lastError()));
                return 3;
            case InstanceChannel::Role::Primary:
                if (!instance->listening())
                    std::fprintf(stderr, "openwitty: %s\n", qPrintable(instance->lastError()));
                break;
        }
    }

    DeviceController controller(nullptr, p.isSet(legacyPolling));
    if (p.isSet(quitAfterSeconds)) {
        QObject::connect(&controller, &DeviceController::errorChanged, &app, [&] {
            if (!controller.lastError().isEmpty()) {
                std::fputs("bounded run stopped: device/control error\n", stderr);
                app.exit(2);
            }
        });
    }
    IntegrationSettings integration;
    QObject::connect(&integration, &IntegrationSettings::configApplied, &controller, &DeviceController::applyUserConfig);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("device", &controller);
    engine.rootContext()->setContextProperty("integration", &integration);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/Lewitt/Main.qml")));
    if (!engine.rootObjects().isEmpty())
        if (auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
            QObject::connect(w, &QQuickWindow::frameSwapped, controller.meters(), &MeterPresentation::noteFrameSwapped);
            if (controller.traceEnabled())
                QObject::connect(w, &QQuickWindow::frameSwapped, &controller, &DeviceController::traceFrameSwapped);
        }
    auto showPage = [&](const QString& page) {
        if (engine.rootObjects().isEmpty() || page.isEmpty()) return;
        QObject* root = engine.rootObjects().first();
        if (page == "settings") root->setProperty("showSettings", true);
        if (page == "settings-desktop") {
            root->setProperty("showSettings", true);
            if (auto* sv = root->findChild<QObject*>("settingsView")) sv->setProperty("open", "desktop");
        }
        if (page == "wizard") root->setProperty("showWizard", true);
        if (page == "onboarding") root->setProperty("showOnboarding", true);
    };
    if (p.isSet(show)) showPage(p.value(show));
    if (instance) {
        QObject::connect(instance.get(), &InstanceChannel::messageReceived, &app, [&](const QString& m) {
            GuiActivation a;
            if (!decodeGuiActivation(m, &a)) return;
            auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
            if (!w) return;
            // Wayland: a token from the requester lets the compositor give us focus (Qt reads it from the
            // environment on activation); without one the compositor may only highlight the window.
            if (!a.token.isEmpty()) qputenv("XDG_ACTIVATION_TOKEN", a.token.toUtf8());
            if (w->windowStates() & Qt::WindowMinimized) w->setWindowStates(w->windowStates() & ~Qt::WindowMinimized);
            w->show();
            w->raise();
            w->requestActivate();
            showPage(a.page);
            std::puts("openwitty: activation request from another start (window brought to the front)");
            std::fflush(stdout);
        });
    }
    if (p.isSet(mock)) controller.connectMock();
    else controller.connectUsb();  // read-only; reports "not found" if absent
    if (p.isSet(sizeOpt) && !engine.rootObjects().isEmpty()) {
        auto parts = p.value(sizeOpt).split('x');
        if (parts.size() == 2)
            if (auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().first()))
                w->resize(parts[0].toInt(), parts[1].toInt());
    }
    if (p.isSet(shot)) {
        QTimer::singleShot(1200, &app, [&] {
            auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
            if (w) w->grabWindow().save(p.value(shot));
            app.quit();
        });
    }
    if (p.isSet(quitAfterSeconds))
        QTimer::singleShot(quitDuration * 1000, &app, &QCoreApplication::quit);
    return app.exec();
}
