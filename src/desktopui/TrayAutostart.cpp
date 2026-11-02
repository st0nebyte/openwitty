#include "TrayAutostart.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

namespace {
QString configHome() {
    const QString x = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!x.isEmpty() && QDir::isAbsolutePath(x)) return x;      // the spec ignores relative values
    const QString home = qEnvironmentVariable("HOME");
    return home.isEmpty() ? QString() : home + "/.config";
}
QString quoteExec(const QString& program) {
    if (!program.contains(QRegularExpression(QStringLiteral("[\\s\"'\\\\$`]")))) return program;
    QString q = program;
    q.replace('\\', "\\\\").replace('"', "\\\"").replace('`', "\\`").replace('$', "\\$");
    return '"' + q + '"';
}
QString entryPath(const char* file) {
    const QString c = configHome();
    return c.isEmpty() ? QString() : c + "/autostart/" + file;
}
bool writeEntry(const QString& p, const QString& text, QString* error) {
    if (p.isEmpty()) {
        if (error) *error = "no configuration directory (HOME is not set)";
        return false;
    }
    if (!QDir().mkpath(QFileInfo(p).absolutePath())) {
        if (error) *error = "cannot create " + QFileInfo(p).absolutePath();
        return false;
    }
    QSaveFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || f.write(text.toUtf8()) < 0 || !f.commit()) {
        if (error) *error = "cannot write " + p;
        return false;
    }
    return true;
}
bool removeEntry(const QString& p, QString* error) {
    if (p.isEmpty() || !QFileInfo::exists(p)) return true;
    if (!QFile::remove(p)) {
        if (error) *error = "cannot remove " + p;
        return false;
    }
    return true;
}
}  // namespace

QString trayAutostartPath() { return entryPath("io.github.st0nebyte.openwitty-tray.desktop"); }

bool trayAutostartEnabled() {
    const QString p = trayAutostartPath();
    return !p.isEmpty() && QFileInfo::exists(p);
}

QString trayAutostartContents(const QString& trayProgram) {
    return QStringLiteral(
               "[Desktop Entry]\n"
               "# Written by OpenWitty (Settings > Desktop integration > system tray). Delete to turn it off.\n"
               "Type=Application\n"
               "Name=OpenWitty tray icon\n"
               "Comment=Status icon for OpenWitty (unofficial); opens OpenWitty, never uses the device\n"
               "Exec=%1\n"
               "Icon=openwitty\n"
               "Terminal=false\n"
               "NoDisplay=true\n")
        .arg(quoteExec(trayProgram));
}

bool enableTrayAutostart(const QString& trayProgram, QString* error) {
    return writeEntry(trayAutostartPath(), trayAutostartContents(trayProgram), error);
}

bool disableTrayAutostart(QString* error) { return removeEntry(trayAutostartPath(), error); }

QString windowAutostartPath() { return entryPath("io.github.st0nebyte.openwitty-window.desktop"); }

bool windowAutostartEnabled() {
    const QString p = windowAutostartPath();
    return !p.isEmpty() && QFileInfo::exists(p);
}

QString windowAutostartContents(const QString& guiProgram) {
    return QStringLiteral(
               "[Desktop Entry]\n"
               "# Written by OpenWitty (Settings > Desktop integration > Open the window at login). Delete to turn it off.\n"
               "Type=Application\n"
               "Name=OpenWitty\n"
               "Comment=Opens the OpenWitty window (unofficial CONNECT 2 control) at login\n"
               "Exec=%1\n"
               "Icon=openwitty\n"
               "Terminal=false\n"
               "NoDisplay=true\n")
        .arg(quoteExec(guiProgram));
}

bool enableWindowAutostart(const QString& guiProgram, QString* error) {
    return writeEntry(windowAutostartPath(), windowAutostartContents(guiProgram), error);
}

bool disableWindowAutostart(QString* error) { return removeEntry(windowAutostartPath(), error); }
