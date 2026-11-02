// Phase 10B: host-side GUI preferences (first-run onboarding done, show tooltips), kept in their own file
// $XDG_CONFIG_HOME/openwitty/gui.conf (~/.config/openwitty/gui.conf) - separate from openwitty.conf, so a GUI
// preference can never touch a device-write permission. Missing or invalid values fall back to the defaults
// (onboarding not done, tooltips on). Saved atomically (temporary file + rename). Nothing here touches the device.
#pragma once
#include <QObject>
#include <QString>

class GuiPreferences : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool onboardingDone READ onboardingDone NOTIFY changed)
    Q_PROPERTY(bool showTooltips READ showTooltips NOTIFY changed)
    // "Reopen the window where it was closed" (default off). The geometry is used only on X11; on KDE Plasma KWin
    // remembers it (see WindowPlacement.hpp).
    Q_PROPERTY(bool rememberPosition READ rememberPosition NOTIFY changed)
public:
    // path empty = the XDG default location
    explicit GuiPreferences(QString path = {}, QObject* parent = nullptr);
    static QString defaultPath();
    QString path() const { return path_; }
    bool onboardingDone() const { return onboardingDone_; }
    bool showTooltips() const { return showTooltips_; }
    bool rememberPosition() const { return rememberPosition_; }
    QString windowGeometry() const { return windowGeometry_; }   // "x,y,w,h" or empty
    QString lastError() const { return error_; }

    Q_INVOKABLE void setOnboardingDone(bool done);
    Q_INVOKABLE void setShowTooltips(bool on);
    Q_INVOKABLE void setRememberPosition(bool on);
    void setWindowGeometry(const QString& g);
    void reload();
    // First-run tour: only for an interactive start without an explicit page, until finished or skipped once.
    bool showOnboardingAtStart(bool screenshotRun, bool explicitPage) const {
        return !screenshotRun && !explicitPage && !onboardingDone_;
    }

signals:
    void changed();

private:
    bool save();
    QString path_;
    bool onboardingDone_{false};
    bool showTooltips_{true};
    bool rememberPosition_{false};
    QString windowGeometry_;
    QString error_;
};
