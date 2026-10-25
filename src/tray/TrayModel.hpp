// What the tray icon shows: whether the volume knob is shown in the desktop volume (the settings file) and
// the state of the background service (`systemctl --user show`). That is all it reads: never the device,
// never ALSA, never the control lease.
#pragma once
#include <QObject>
#include <QString>

#include "ServiceState.hpp"

class TrayModel : public QObject {
    Q_OBJECT
public:
    explicit TrayModel(CommandRunner runner = {}, QObject* parent = nullptr);

    void refresh();                       // re-read both; emits changed() when something differs
    bool knobSync() const { return knobSync_; }
    ServiceState service() const { return service_; }
    QString configPath() const { return configPath_; }

    QString knobLine() const;             // e.g. "Volume knob in desktop volume: on"
    QString serviceLine() const;          // e.g. "Background service: running"
    QString toolTip() const;              // title line + both status lines

signals:
    void changed();

private:
    CommandRunner runner_;
    QString configPath_;
    bool knobSync_{false};
    ServiceState service_{ServiceState::Unknown};
    bool first_{true};
};
