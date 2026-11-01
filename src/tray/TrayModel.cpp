#include "TrayModel.hpp"

#include <QDir>
#include <QFile>

#include "core/UserConfig.hpp"

namespace {
// A CONNECT 2 (USB 29c2:0004) is plugged in: sysfs attributes only, the device itself is never opened.
bool connect2Plugged() {
    const QDir usb(QStringLiteral("/sys/bus/usb/devices"));
    for (const QString& d : usb.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        auto attr = [&](const char* name) {
            QFile f(usb.filePath(d) + '/' + QLatin1String(name));
            return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.read(16)).trimmed() : QString();
        };
        if (attr("idVendor") == QLatin1String("29c2") && attr("idProduct") == QLatin1String("0004")) return true;
    }
    return false;
}
}  // namespace

TrayModel::TrayModel(CommandRunner runner, QObject* parent)
    : QObject(parent), runner_(runner ? std::move(runner) : CommandRunner(runCommand)),
      configPath_(QString::fromStdString(lewitt::userConfigPath())) {}

void TrayModel::refresh() {
    const bool sync = !configPath_.isEmpty() &&
                      lewitt::loadUserConfig(configPath_.toStdString()).config.desktopVolumeSync ==
                          lewitt::DesktopVolumeSync::Mirror;
    const ServiceState svc = queryServiceState(runner_);
    const bool present = probe_ ? probe_() : connect2Plugged();
    if (!first_ && sync == knobSync_ && svc == service_ && present == present_) return;
    first_ = false;
    knobSync_ = sync;
    present_ = present;
    service_ = svc;
    emit changed();
}

QString TrayModel::deviceLine() const {
    return present_ ? QStringLiteral("CONNECT 2 connected") : QStringLiteral("CONNECT 2 not connected");
}

QString TrayModel::knobLine() const {
    return knobSync_ ? QStringLiteral("Desktop volume follows the CONNECT 2 knob")
                     : QStringLiteral("Desktop volume does not follow the CONNECT 2 knob");
}

QString TrayModel::serviceLine() const {
    switch (service_) {
        case ServiceState::Active:
            return knobSync_ ? QStringLiteral("Keeps working when the window is closed")
                             : QStringLiteral("Runs in the background, but has nothing to do");
        case ServiceState::Enabled: return QStringLiteral("Background service switched on, but not running");
        case ServiceState::Disabled: return QStringLiteral("Works only while the window is open");
        case ServiceState::NotInstalled: return QStringLiteral("Background service not installed");
        case ServiceState::Unknown: break;
    }
    return QStringLiteral("Background service state unknown");
}

QString TrayModel::toolTip() const {
    return QStringLiteral("OpenWitty\n") + deviceLine() + '\n' + knobLine() + '\n' + serviceLine();
}
