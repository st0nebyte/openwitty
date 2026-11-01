// The tray icon and its menu. The menu shows the two status lines and offers exactly three actions: open
// OpenWitty, open its Desktop integration settings, and quit the tray icon. Opening goes through
// openOpenWitty() (activates a running window, otherwise starts one); quitting only ends this tray process -
// the background service is a separate systemd unit and is not touched.
#pragma once
#include <QObject>
#include <QString>
#include <functional>
#include <memory>

class QAction;
class QMenu;
class QSystemTrayIcon;
class TrayModel;

class TrayController : public QObject {
    Q_OBJECT
public:
    using OpenGui = std::function<void(const QString& page)>;   // "" or "settings-desktop"
    TrayController(TrayModel& model, OpenGui open, QObject* parent = nullptr);
    ~TrayController() override;

    void show();                                   // makes the icon visible
    QMenu* menu() const { return menu_.get(); }
    QSystemTrayIcon* icon() const { return icon_; }
    QAction* deviceStatus() const { return device_; }
    QAction* knobStatus() const { return knob_; }
    QAction* serviceStatus() const { return service_; }
    QAction* openAction() const { return open_; }
    QAction* settingsAction() const { return settings_; }
    QAction* quitAction() const { return quit_; }

signals:
    void quitRequested();

private:
    void update();
    TrayModel& model_;
    OpenGui openGui_;
    std::unique_ptr<QMenu> menu_;
    QSystemTrayIcon* icon_{nullptr};
    QAction* device_{nullptr};
    QAction* knob_{nullptr};
    QAction* service_{nullptr};
    QAction* open_{nullptr};
    QAction* settings_{nullptr};
    QAction* quit_{nullptr};
};
