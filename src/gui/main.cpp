#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QQuickWindow>
#include <QTimer>

#include "DeviceController.hpp"

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("lewitt-control-center");
    app.setOrganizationName("lewitt-linux");
    QCommandLineParser p;
    QCommandLineOption mock("mock", "Start with the simulated device.");
    p.addOption(mock);
    QCommandLineOption shot("screenshot", "Save a screenshot to <file> and exit.", "file");
    p.addOption(shot);
    QCommandLineOption sizeOpt("size", "Window size WxH (for screenshots).", "WxH");
    p.addOption(sizeOpt);
    QCommandLineOption show("show", "Open a page at start: settings | wizard | onboarding.", "page");
    p.addOption(show);
    p.addHelpOption();
    p.process(app);

    DeviceController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("device", &controller);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/Lewitt/Main.qml")));
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
