#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QQuickWindow>
#include <QTimer>

#include "DeviceController.hpp"
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
    QCommandLineOption shot("screenshot", "Save a screenshot to <file> and exit.", "file");
    p.addOption(shot);
    QCommandLineOption sizeOpt("size", "Window size WxH (for screenshots).", "WxH");
    p.addOption(sizeOpt);
    QCommandLineOption show("show", "Open a page at start: settings | wizard | onboarding.", "page");
    p.addOption(show);
    p.setApplicationDescription(QStringLiteral(LEWITT_NOTICE));
    p.addHelpOption();
    p.addVersionOption();
    p.process(app);

    DeviceController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("device", &controller);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/Lewitt/Main.qml")));
    if (!engine.rootObjects().isEmpty())
        if (auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
            QObject::connect(w, &QQuickWindow::frameSwapped, controller.meters(), &MeterPresentation::noteFrameSwapped);
            if (controller.traceEnabled())
                QObject::connect(w, &QQuickWindow::frameSwapped, &controller, &DeviceController::traceFrameSwapped);
        }
    if (p.isSet(show) && !engine.rootObjects().isEmpty()) {
        QObject* root = engine.rootObjects().first();
        if (p.value(show) == "settings") root->setProperty("showSettings", true);
        if (p.value(show) == "wizard") root->setProperty("showWizard", true);
        if (p.value(show) == "onboarding") root->setProperty("showOnboarding", true);
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
    return app.exec();
}
