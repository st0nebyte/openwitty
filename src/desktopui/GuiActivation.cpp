#include "GuiActivation.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>

#include "InstanceChannel.hpp"

QString encodeGuiActivation(const GuiActivation& a) {
    return QStringLiteral("activate\n") + a.page + '\n' + a.token;
}

bool decodeGuiActivation(const QString& message, GuiActivation* out) {
    const QStringList parts = message.split('\n');
    if (parts.value(0) != QLatin1String("activate")) return false;
    if (out) *out = GuiActivation{parts.value(1), parts.value(2)};
    return true;
}

OpenResult openOpenWitty(const QString& page, const QString& guiProgram, const Spawner& spawn) {
    if (InstanceChannel::send("gui", encodeGuiActivation({page, qEnvironmentVariable("XDG_ACTIVATION_TOKEN")})))
        return OpenResult::Activated;
    QStringList args;
    if (!page.isEmpty()) args << "--show" << page;
    return spawn(guiProgram, args) ? OpenResult::Started : OpenResult::Failed;
}

bool spawnDetached(const QString& program, const QStringList& args) { return QProcess::startDetached(program, args); }

QString siblingProgram(const QString& name) {
    const QString p = QCoreApplication::applicationDirPath() + '/' + name;
    const QFileInfo fi(p);
    return fi.isFile() && fi.isExecutable() ? p : name;
}
