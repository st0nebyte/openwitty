// State of the optional openwitty-volumed systemd user service, from `systemctl --user show`. Shared by the
// Settings page and the tray companion (both only read it here; enabling/disabling is the Settings page's job).
#pragma once
#include <QString>
#include <QStringList>
#include <functional>

enum class ServiceState { NotInstalled, Disabled, Enabled, Active, Unknown };

// Runs a command (program + arguments), returns its exit status and stdout. Injectable for tests.
using CommandRunner = std::function<int(const QString& program, const QStringList& args, QString* out)>;

inline constexpr const char* kVolumeServiceUnit = "openwitty-volumed.service";
QStringList serviceShowArguments();                                  // for "systemctl"
ServiceState parseServiceState(int exitCode, const QString& showOutput);
ServiceState queryServiceState(const CommandRunner& run);
QString serviceStateName(ServiceState s);                            // not-installed | disabled | enabled | active | unknown
int runCommand(const QString& program, const QStringList& args, QString* out);   // QProcess, 5 s timeout
