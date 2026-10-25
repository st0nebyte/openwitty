// Optional login autostart of the tray companion: one XDG autostart entry
// ($XDG_CONFIG_HOME/autostart/io.github.st0nebyte.openwitty-tray.desktop), written and removed only when the
// user flips the switch in Settings. Nothing installs it automatically.
#pragma once
#include <QString>

QString trayAutostartPath();
bool trayAutostartEnabled();
QString trayAutostartContents(const QString& trayProgram);
bool enableTrayAutostart(const QString& trayProgram, QString* error = nullptr);
bool disableTrayAutostart(QString* error = nullptr);
