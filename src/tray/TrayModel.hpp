// What the tray icon shows: whether a CONNECT 2 is plugged in (USB IDs in sysfs only), whether the desktop volume
// follows its knob (the settings file) and the state of the background service (`systemctl --user show`). That is
// all it reads: never the device itself, never ALSA, never the control lease.
#pragma once
#include <QObject>
#include <QString>
#include <functional>

#include "ServiceState.hpp"

class TrayModel : public QObject {
    Q_OBJECT
public:
    explicit TrayModel(CommandRunner runner = {}, QObject* parent = nullptr);

    void refresh();                       // re-read both; emits changed() when something differs
    bool knobSync() const { return knobSync_; }
    bool devicePresent() const { return present_; }
    // Tests: replaces the sysfs scan for the CONNECT 2's USB IDs.
    void setPresenceProbe(std::function<bool()> probe) { probe_ = std::move(probe); }
    ServiceState service() const { return service_; }
    QString configPath() const { return configPath_; }

    QString deviceLine() const;           // e.g. "CONNECT 2 connected"
    QString knobLine() const;             // e.g. "Desktop volume follows the CONNECT 2 knob"
    QString serviceLine() const;          // e.g. "Keeps working when the window is closed"
    QString toolTip() const;              // title line + the three status lines

signals:
    void changed();

private:
    CommandRunner runner_;
    QString configPath_;
    bool knobSync_{false};
    bool present_{false};
    std::function<bool()> probe_;
    ServiceState service_{ServiceState::Unknown};
    bool first_{true};
};
