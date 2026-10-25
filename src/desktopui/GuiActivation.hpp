// Opening OpenWitty from outside the window (tray, a second launcher click): a running window is asked to
// come to the front, so there is never a second device-owning GUI; only when none answers is OpenWitty
// started. The message format is shared with the GUI's receiver.
#pragma once
#include <QString>
#include <QStringList>
#include <functional>

struct GuiActivation {
    QString page;    // "" (main window) or a --show page, e.g. "settings-desktop"
    QString token;   // Wayland XDG activation token, if the requester has one
};

QString encodeGuiActivation(const GuiActivation& a);
// nullopt-like: ok=false for anything that is not an activation request.
bool decodeGuiActivation(const QString& message, GuiActivation* out);

enum class OpenResult { Activated, Started, Failed };
using Spawner = std::function<bool(const QString& program, const QStringList& args)>;
OpenResult openOpenWitty(const QString& page, const QString& guiProgram, const Spawner& spawn);
bool spawnDetached(const QString& program, const QStringList& args);    // QProcess::startDetached

// The executable `name` next to the running program (installed side by side), else the bare name (PATH).
QString siblingProgram(const QString& name);
