#include "TrayModel.hpp"

#include "core/UserConfig.hpp"

TrayModel::TrayModel(CommandRunner runner, QObject* parent)
    : QObject(parent), runner_(runner ? std::move(runner) : CommandRunner(runCommand)),
      configPath_(QString::fromStdString(lewitt::userConfigPath())) {}

void TrayModel::refresh() {
    const bool sync = !configPath_.isEmpty() &&
                      lewitt::loadUserConfig(configPath_.toStdString()).config.desktopVolumeSync ==
                          lewitt::DesktopVolumeSync::Mirror;
    const ServiceState svc = queryServiceState(runner_);
    if (!first_ && sync == knobSync_ && svc == service_) return;
    first_ = false;
    knobSync_ = sync;
    service_ = svc;
    emit changed();
}

QString TrayModel::knobLine() const {
    return knobSync_ ? QStringLiteral("Volume knob in desktop volume: on")
                     : QStringLiteral("Volume knob in desktop volume: off");
}

QString TrayModel::serviceLine() const {
    switch (service_) {
        case ServiceState::Active:
            return knobSync_ ? QStringLiteral("Background service: running")
                             : QStringLiteral("Background service: running, idle (knob display is off)");
        case ServiceState::Enabled: return QStringLiteral("Background service: on, not running");
        case ServiceState::Disabled: return QStringLiteral("Background service: off");
        case ServiceState::NotInstalled: return QStringLiteral("Background service: not installed");
        case ServiceState::Unknown: break;
    }
    return QStringLiteral("Background service: state unknown");
}

QString TrayModel::toolTip() const { return QStringLiteral("OpenWitty\n") + knobLine() + '\n' + serviceLine(); }
