// Settings > Desktop integration: the GUI side of the shared user configuration (core/UserConfig),
// the optional background service (systemd user unit openwitty-volumed.service) and the read-only
// desktop audio setup check. Nothing here writes the device; changes that affect device writes are
// handed to DeviceController, which applies them through its normal session/lease/read-back paths.
#pragma once
#include <QObject>
#include <QStringList>
#include <functional>

#include "core/UserConfig.hpp"
#include "desktop/AudioSetupCheck.hpp"

class IntegrationSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool desktopSync READ desktopSync NOTIFY changed)
    Q_PROPERTY(bool vendorFallback READ vendorFallback NOTIFY changed)
    Q_PROPERTY(bool outputControl READ outputControl NOTIFY changed)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)
    Q_PROPERTY(QStringList configWarnings READ configWarnings NOTIFY changed)
    // not-installed | disabled | enabled | active | unknown
    Q_PROPERTY(QString serviceState READ serviceState NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    // The background service is on while desktop sync is off: it only idles (the UI offers to turn it off).
    Q_PROPERTY(bool serviceIdleOn READ serviceIdleOn NOTIFY changed)
    // setup check (read-only)
    Q_PROPERTY(bool setupAvailable READ setupAvailable NOTIFY changed)     // false: built without ALSA support
    Q_PROPERTY(bool cardFound READ cardFound NOTIFY changed)
    Q_PROPERTY(bool mixerLoads READ mixerLoads NOTIFY changed)
    Q_PROPERTY(bool volumeControl READ volumeControl NOTIFY changed)
    Q_PROPERTY(bool quirkConfigured READ quirkConfigured NOTIFY changed)
    Q_PROPERTY(bool quirkActive READ quirkActive NOTIFY changed)
    Q_PROPERTY(QStringList findings READ findings NOTIFY changed)
    Q_PROPERTY(QString quirkInstructions READ quirkInstructions CONSTANT)
    // Optional system tray icon (openwitty-tray): installed at all, and started at login (XDG autostart).
    Q_PROPERTY(bool trayInstalled READ trayInstalled CONSTANT)
    Q_PROPERTY(bool trayAutostart READ trayAutostart NOTIFY changed)
public:
    // Runs a command (program + arguments), returns its exit status and stdout. Injectable for tests.
    using Runner = std::function<int(const QString& program, const QStringList& args, QString* out)>;
    // Collects the desktop audio setup status; nullopt = not available (built without ALSA). Injectable
    // so tests never open the real device's mixer.
    using SetupProbe = std::function<std::optional<lewitt::AudioSetupStatus>()>;
    // Starts a program detached (the tray icon). Injectable for tests.
    using Launcher = std::function<bool(const QString& program, const QStringList& args)>;
    explicit IntegrationSettings(QObject* parent = nullptr, Runner runner = {}, SetupProbe probe = {},
                                 Launcher launcher = {}, QString trayProgram = {});

    bool desktopSync() const { return cfg_.desktopVolumeSync == lewitt::DesktopVolumeSync::Mirror; }
    bool vendorFallback() const { return cfg_.vendorFallback; }
    bool outputControl() const { return cfg_.outputControl; }
    QString configPath() const { return path_; }
    QStringList configWarnings() const { return warnings_; }
    QString serviceState() const { return serviceState_; }
    QString lastError() const { return lastError_; }
    bool serviceIdleOn() const {
        return !desktopSync() && (serviceState_ == "enabled" || serviceState_ == "active");
    }
    bool setupAvailable() const { return setupAvailable_; }
    bool cardFound() const { return cardFound_; }
    bool mixerLoads() const { return mixerLoads_; }
    bool volumeControl() const { return volumeControl_; }
    bool quirkConfigured() const { return quirkConfigured_; }
    bool quirkActive() const { return quirkActive_; }
    QStringList findings() const { return findings_; }
    QString quirkInstructions() const;
    bool trayInstalled() const;
    bool trayAutostart() const { return trayAutostart_; }
    QString trayProgram() const { return trayProgram_; }
    const lewitt::UserConfig& config() const { return cfg_; }

    Q_INVOKABLE void refresh();                        // config + service state + setup check
    Q_INVOKABLE void setDesktopSync(bool on);          // off also turns the vendor fallback off
    // Device-writing options: only with explicit confirmation from the UI dialog.
    Q_INVOKABLE void setVendorFallback(bool on, bool confirmed);
    Q_INVOKABLE void setOutputControl(bool on, bool confirmed);
    // systemctl --user enable --now / disable --now; turning it off is always allowed, turning it on
    // needs desktop sync. Returns whether the resulting service state is the requested one.
    Q_INVOKABLE bool setBackgroundService(bool on);
    // Defaults in the file, then the background service off. Partial failure is reported in lastError.
    Q_INVOKABLE bool resetToDefaults();
    Q_INVOKABLE void copyQuirkInstructions();
    // On: write the login autostart entry and start the tray icon now (it is single-instance). Off: remove
    // the entry and ask a running tray icon to quit. Never touches the background service.
    Q_INVOKABLE bool setTrayAutostart(bool on);

signals:
    void changed();
    // Emitted after the file was saved; DeviceController re-applies the configuration.
    void configApplied();

private:
    bool save(lewitt::UserConfig next);
    void loadConfig();
    void refreshService();
    void refreshSetup();
    int run(const QString& program, const QStringList& args, QString* out = nullptr);

    Runner runner_;
    SetupProbe probe_;
    Launcher launcher_;
    QString trayProgram_;
    bool trayAutostart_{false};
    QString path_;
    lewitt::UserConfig cfg_;
    QStringList warnings_;
    QString serviceState_{"unknown"};
    QString lastError_;
    bool setupAvailable_{false}, cardFound_{false}, mixerLoads_{false}, volumeControl_{false};
    bool quirkConfigured_{false}, quirkActive_{false};
    QStringList findings_;
};
