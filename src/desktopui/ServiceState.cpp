#include "ServiceState.hpp"

#include <QProcess>

QStringList serviceShowArguments() {
    return {"--user", "show", "-p", "LoadState", "-p", "UnitFileState", "-p", "ActiveState", kVolumeServiceUnit};
}

ServiceState parseServiceState(int exitCode, const QString& out) {
    if (exitCode != 0) return ServiceState::Unknown;
    auto value = [&](const QString& key) {
        for (const auto& line : out.split('\n'))
            if (line.startsWith(key + "=")) return line.mid(key.size() + 1).trimmed();
        return QString();
    };
    if (value("LoadState") != "loaded") return ServiceState::NotInstalled;
    if (value("ActiveState") == "active") return ServiceState::Active;
    if (value("UnitFileState") == "enabled") return ServiceState::Enabled;
    return ServiceState::Disabled;
}

ServiceState queryServiceState(const CommandRunner& run) {
    QString out;
    const int rc = run("systemctl", serviceShowArguments(), &out);
    return parseServiceState(rc, out);
}

QString serviceStateName(ServiceState s) {
    switch (s) {
        case ServiceState::NotInstalled: return "not-installed";
        case ServiceState::Disabled: return "disabled";
        case ServiceState::Enabled: return "enabled";
        case ServiceState::Active: return "active";
        case ServiceState::Unknown: break;
    }
    return "unknown";
}

int runCommand(const QString& program, const QStringList& args, QString* out) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(5000)) {
        p.kill();
        p.waitForFinished(1000);
        return -1;
    }
    if (out) *out = QString::fromUtf8(p.readAllStandardOutput());
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}
