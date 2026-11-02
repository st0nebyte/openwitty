#include "WindowPlacement.hpp"

#include <QGuiApplication>
#include <QStandardPaths>

namespace windowplace {
namespace {
const QStringList kRuleKeys = {"Description", "wmclass", "wmclassmatch", "wmclasscomplete", "types",
                               "position",    "positionrule", "size",   "sizerule"};
QStringList write(const QString& group, const QString& key, const QString& value) {
    return {"kwriteconfig6", "--file", "kwinrulesrc", "--group", group, "--key", key, value};
}
QString appended(const QString& list) {
    QStringList l = list.split(',', Qt::SkipEmptyParts);
    for (auto& s : l) s = s.trimmed();
    if (!l.contains(kRuleGroup)) l << kRuleGroup;
    return l.join(',');
}
}  // namespace

QString detectMode(const QString& currentDesktop, const QString& platformName, bool haveKwriteconfig) {
    if (currentDesktop.split(':').contains("KDE", Qt::CaseInsensitive) && haveKwriteconfig) return "kwin";
    if (platformName == "xcb") return "app";
    return "unsupported";
}

QString detectMode() {
    return detectMode(qEnvironmentVariable("XDG_CURRENT_DESKTOP"), QGuiApplication::platformName(),
                      !QStandardPaths::findExecutable("kwriteconfig6").isEmpty());
}

QList<QStringList> ruleOnCommands(const QString& appId, const QString& existingOrder, const QString& legacyRules) {
    const QString g = kRuleGroup;
    QList<QStringList> c = {
        write(g, "Description", "OpenWitty: reopen where it was closed (written by OpenWitty)"),
        write(g, "wmclass", appId),
        write(g, "wmclassmatch", "1"),          // exact
        write(g, "wmclasscomplete", "false"),
        write(g, "types", "1"),                 // normal windows only
        write(g, "positionrule", "4"),          // Remember
        write(g, "sizerule", "4"),
    };
    if (!existingOrder.trimmed().isEmpty()) c << write("General", "Order", appended(existingOrder));
    if (!legacyRules.trimmed().isEmpty()) c << write("General", "rules", appended(legacyRules));
    return c;
}

QList<QStringList> ruleOffCommands() {
    QList<QStringList> c;
    for (const auto& k : kRuleKeys)
        c << QStringList{"kwriteconfig6", "--file", "kwinrulesrc", "--group", kRuleGroup, "--key", k, "--delete"};
    return c;
}

QStringList readRuleKey(const QString& key) {
    return {"kreadconfig6", "--file", "kwinrulesrc", "--group", kRuleGroup, "--key", key};
}
QStringList readGeneralKey(const QString& key) {
    return {"kreadconfig6", "--file", "kwinrulesrc", "--group", "General", "--key", key};
}

QStringList reconfigureCommand() {
    return {"dbus-send", "--session", "--type=method_call", "--dest=org.kde.KWin", "/KWin", "org.kde.KWin.reconfigure"};
}

bool parseGeometry(const QString& text, int* x, int* y, int* w, int* h) {
    const QStringList p = text.split(',');
    if (p.size() != 4) return false;
    int v[4];
    for (int i = 0; i < 4; ++i) {
        bool ok = false;
        v[i] = p[i].trimmed().toInt(&ok);
        if (!ok) return false;
    }
    if (v[2] < 200 || v[3] < 150 || v[2] > 10000 || v[3] > 10000 || v[0] < -10000 || v[1] < -10000 || v[0] > 20000 ||
        v[1] > 20000)
        return false;
    *x = v[0]; *y = v[1]; *w = v[2]; *h = v[3];
    return true;
}

}  // namespace windowplace
