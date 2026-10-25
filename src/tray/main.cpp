// openwitty-tray: the optional OpenWitty status icon for the desktop's system tray (KDE Plasma: Status
// Notifier Item over D-Bus, provided by Qt itself). It shows whether the volume knob is shown in the desktop
// volume and whether the background service runs, and opens OpenWitty. It never opens the CONNECT 2, never
// claims the control lease, never reads ALSA and never starts, stops or changes the background service.
#include <QApplication>
#include <QCommandLineParser>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QSystemTrayIcon>
#include <QTimer>
#include <cstdio>

#include "GuiActivation.hpp"
#include "InstanceChannel.hpp"
#include "TrayController.hpp"
#include "TrayModel.hpp"
#include "lewitt/version.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("openwitty-tray");
    app.setApplicationDisplayName("OpenWitty tray icon");
    app.setApplicationVersion(LEWITT_VERSION_STRING);
    app.setQuitOnLastWindowClosed(false);
    QGuiApplication::setDesktopFileName(QStringLiteral(LEWITT_APP_DESKTOP_ID));
    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("OpenWitty status icon for the system tray. Never uses the device. ") +
                                QStringLiteral(LEWITT_NOTICE));
    QCommandLineOption quit("quit", "Ask a running tray icon to quit, then exit.");
    p.addOption(quit);
    QCommandLineOption wait("wait-for-tray-seconds",
        "How long to wait for the desktop's system tray to appear (0-300, default 30).", "seconds", "30");
    p.addOption(wait);
    p.addHelpOption();
    p.addVersionOption();
    p.process(app);

    if (p.isSet(quit)) {
        std::puts(InstanceChannel::send("tray", "quit") ? "openwitty-tray: the tray icon was asked to quit"
                                                         : "openwitty-tray: no tray icon is running");
        return 0;
    }
    bool ok = false;
    const int waitSeconds = p.value(wait).toInt(&ok);
    if (!ok || waitSeconds < 0 || waitSeconds > 300) {
        std::fputs("--wait-for-tray-seconds requires 0-300\n", stderr);
        return 64;
    }
    InstanceChannel channel("tray");
    switch (channel.claimOrForward("activate", 5000)) {
        case InstanceChannel::Role::Forwarded:
            std::puts("openwitty-tray: already running");
            return 0;
        case InstanceChannel::Role::Failed:
            std::fprintf(stderr, "openwitty-tray: not starting a second tray icon: %s\n", qPrintable(channel.lastError()));
            return 1;
        case InstanceChannel::Role::Primary:
            break;
    }

    TrayModel model;
    const QString gui = siblingProgram("openwitty");
    TrayController tray(model, [gui](const QString& page) {
        if (openOpenWitty(page, gui, spawnDetached) == OpenResult::Failed)
            std::fprintf(stderr, "openwitty-tray: could not start %s\n", qPrintable(gui));
    });
    QObject::connect(&tray, &TrayController::quitRequested, &app, &QCoreApplication::quit);
    QObject::connect(&channel, &InstanceChannel::messageReceived, &app, [&](const QString& m) {
        if (m == QLatin1String("quit")) app.quit();
    });

    // Status: the settings file is watched (the GUI saves it atomically, so watch the directory too); the
    // service state is re-read every few seconds and whenever the menu opens.
    model.refresh();
    QFileSystemWatcher watcher;
    const QString cfg = model.configPath();
    if (!cfg.isEmpty()) {
        watcher.addPath(QFileInfo(cfg).absolutePath());
        if (QFileInfo::exists(cfg)) watcher.addPath(cfg);
        QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged, &model, [&] {
            if (QFileInfo::exists(cfg) && !watcher.files().contains(cfg)) watcher.addPath(cfg);
            model.refresh();
        });
        QObject::connect(&watcher, &QFileSystemWatcher::fileChanged, &model, &TrayModel::refresh);
    }
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &model, &TrayModel::refresh);
    poll.start(5000);

    // At login the tray host may appear a little after us: wait for it, then give up quietly (OpenWitty
    // itself works without a tray; nothing restarts this program).
    int waited = 0;
    QTimer trayWait;
    auto tryShow = [&] {
        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            trayWait.stop();
            tray.show();
            return;
        }
        if (waited >= waitSeconds) {
            std::fputs("openwitty-tray: this desktop has no system tray; OpenWitty itself works without it\n", stderr);
            app.exit(0);
            return;
        }
        ++waited;
    };
    QObject::connect(&trayWait, &QTimer::timeout, &app, tryShow);
    tryShow();
    if (!QSystemTrayIcon::isSystemTrayAvailable() && waitSeconds > 0) trayWait.start(1000);
    if (!QSystemTrayIcon::isSystemTrayAvailable() && waitSeconds == 0) return 0;
    return app.exec();
}
