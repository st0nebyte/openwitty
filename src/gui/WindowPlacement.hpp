// "Reopen the window where it was closed". Wayland does not let an application place its own window, so on KDE
// Plasma the window manager does it: one KWin window rule (group "openwitty-window" in ~/.config/kwinrulesrc) that
// remembers position and size of the OpenWitty window, written and removed only by the Settings switch, with
// kwriteconfig6 (fixed argv, no shell). On an X11 session the window restores its own geometry from gui.conf.
// Other Wayland desktops: not supported (the switch says so).
#pragma once
#include <QList>
#include <QString>
#include <QStringList>

namespace windowplace {

inline constexpr const char* kRuleGroup = "openwitty-window";

// "kwin" (KDE Plasma with kwriteconfig6), "app" (X11 session: the window restores itself) or "unsupported".
QString detectMode(const QString& currentDesktop, const QString& platformName, bool haveKwriteconfig);
QString detectMode();   // from XDG_CURRENT_DESKTOP, the Qt platform and PATH

// kwriteconfig6 calls (program + args) that create the rule. existingOrder / legacyRules: the comma-separated
// [General] Order and the old "rules" list as kreadconfig6 printed them; the group is appended to each non-empty one
// (KWin drops groups missing from a legacy list, and lists groups missing from Order by itself).
QList<QStringList> ruleOnCommands(const QString& appId, const QString& existingOrder, const QString& legacyRules);
// Removes every key of the rule group (KWin drops the empty group and its Order entry at its next save).
QList<QStringList> ruleOffCommands();
// Reads one key of the rule group (to tell whether the rule is there).
QStringList readRuleKey(const QString& key);
QStringList readGeneralKey(const QString& key);
// Tells KWin to reload its rules.
QStringList reconfigureCommand();

// gui.conf "window_geometry = x,y,w,h": parsed only if four integers with a sane size.
bool parseGeometry(const QString& text, int* x, int* y, int* w, int* h);

}  // namespace windowplace
