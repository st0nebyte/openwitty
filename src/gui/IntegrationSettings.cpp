#include "IntegrationSettings.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QProcess>


using namespace lewitt;

namespace {
constexpr const char* kUnit = "openwitty-volumed.service";

int runProcess(const QString& program, const QStringList& args, QString* out) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(5000)) {
        p.kill();
        return -1;
    }
    if (out) *out = QString::fromUtf8(p.readAllStandardOutput());
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}
}  // namespace

static std::optional<AudioSetupStatus> realProbe() {
#ifdef LEWITT_HAVE_ALSA
    return collectAudioSetup();
#else
    return std::nullopt;
#endif
}

IntegrationSettings::IntegrationSettings(QObject* parent, Runner runner, SetupProbe probe)
    : QObject(parent), runner_(runner ? std::move(runner) : Runner(runProcess)),
      probe_(probe ? std::move(probe) : SetupProbe(realProbe)),
      path_(QString::fromStdString(userConfigPath())) {
    // Configuration and service state only; the setup check (opens the CONNECT 2's ALSA mixer) runs when
    // the Desktop integration page is opened (refresh()).
    loadConfig();
    refreshService();
}

void IntegrationSettings::loadConfig() {
    auto parsed = loadUserConfig(path_.toStdString());
    cfg_ = parsed.config;
    warnings_.clear();
    for (const auto& w : parsed.warnings) warnings_ << QString::fromStdString(w);
}

int IntegrationSettings::run(const QString& program, const QStringList& args, QString* out) {
    return runner_(program, args, out);
}

void IntegrationSettings::refresh() {
    loadConfig();
    refreshService();
    refreshSetup();
    emit changed();
}

void IntegrationSettings::refreshService() {
    QString out;
    const int rc = run("systemctl", {"--user", "show", "-p", "LoadState", "-p", "UnitFileState", "-p", "ActiveState", kUnit}, &out);
    if (rc != 0) {
        serviceState_ = "unknown";
        return;
    }
    auto value = [&](const QString& key) {
        for (const auto& line : out.split('\n'))
            if (line.startsWith(key + "=")) return line.mid(key.size() + 1).trimmed();
        return QString();
    };
    if (value("LoadState") != "loaded") serviceState_ = "not-installed";
    else if (value("ActiveState") == "active") serviceState_ = "active";
    else if (value("UnitFileState") == "enabled") serviceState_ = "enabled";
    else serviceState_ = "disabled";
}

void IntegrationSettings::refreshSetup() {
    const auto probed = probe_();
    setupAvailable_ = probed.has_value();
    if (!probed) return;
    const AudioSetupStatus& st = *probed;
    cardFound_ = st.card.has_value();
    mixerLoads_ = cardFound_ && !st.mixerLoadError;
    volumeControl_ = st.volumeControl;
    quirkConfigured_ = st.quirkConfigured;
    quirkActive_ = st.quirkActive;
    findings_.clear();
    for (const auto& f : audioSetupFindings(st)) findings_ << QString::fromStdString(f);
}

bool IntegrationSettings::save(UserConfig next) {
    if (next.desktopVolumeSync == DesktopVolumeSync::Off) next.vendorFallback = false;   // fallback needs sync
    if (auto s = saveUserConfig(next, path_.toStdString()); !s) {
        lastError_ = QString::fromStdString(s.error().message);
        emit changed();
        return false;
    }
    lastError_.clear();
    cfg_ = next;
    warnings_.clear();
    emit changed();
    emit configApplied();
    return true;
}

void IntegrationSettings::setDesktopSync(bool on) {
    UserConfig next = cfg_;
    next.desktopVolumeSync = on ? DesktopVolumeSync::Mirror : DesktopVolumeSync::Off;
    save(next);
}

void IntegrationSettings::setVendorFallback(bool on, bool confirmed) {
    if (on && (!confirmed || !desktopSync())) return;   // enabling needs the dialog and active sync
    UserConfig next = cfg_;
    next.vendorFallback = on;
    save(next);
}

void IntegrationSettings::setOutputControl(bool on, bool confirmed) {
    if (on && !confirmed) return;
    UserConfig next = cfg_;
    next.outputControl = on;
    save(next);
}

bool IntegrationSettings::setBackgroundService(bool on) {
    refreshService();
    if (serviceState_ == "not-installed") {
        lastError_ = on ? "The background service is not installed (openwitty-volumed.service)." : QString();
        emit changed();
        return !on;
    }
    const bool isOn = serviceState_ == "enabled" || serviceState_ == "active";
    if (on && !desktopSync()) {
        lastError_ = "Turn on \"Show knob and Mute button in desktop volume\" first: with desktop sync off the "
                     "background service would only idle.";
        emit changed();
        return false;
    }
    if (on == isOn && serviceState_ != "unknown") {   // nothing to do
        lastError_.clear();
        emit changed();
        return true;
    }
    const QString verb = on ? "enable" : "disable";
    const int rc = run("systemctl", {"--user", verb, "--now", kUnit});
    refreshService();
    const bool nowOn = serviceState_ == "enabled" || serviceState_ == "active";
    const bool ok = rc == 0 && (on ? serviceState_ == "active" : !nowOn);
    if (ok) {
        lastError_.clear();
    } else {
        lastError_ = QString("Could not %1 the background service (systemctl --user %1 --now %2: %3; state now: %4).")
                         .arg(verb, kUnit, rc == 0 ? QString("no effect") : QString("exit %1").arg(rc), serviceState_);
    }
    emit changed();
    return ok;
}

bool IntegrationSettings::resetToDefaults() {
    // Settings first: even if the service cannot be disabled, it goes idle because desktop sync is off.
    const bool saved = save(UserConfig{});
    const QString saveError = saved ? QString() : lastError_;
    refreshService();
    bool serviceOk = true;
    if (serviceState_ == "enabled" || serviceState_ == "active" || serviceState_ == "unknown")
        serviceOk = serviceState_ == "unknown" ? false : setBackgroundService(false);
    QStringList problems;
    if (!saved) problems << "the settings could not be saved (" + saveError + ")";
    if (!serviceOk)
        problems << (serviceState_ == "unknown"
                         ? QString("the background service state could not be checked")
                         : QString("the background service could not be disabled (%1)").arg(lastError_));
    if (problems.isEmpty()) {
        lastError_.clear();
    } else {
        lastError_ = "Reset incomplete: " + problems.join("; ") +
                     (saved ? ". Desktop sync is off, so a running background service only idles; disable it with "
                              "\"systemctl --user disable --now openwitty-volumed.service\"."
                            : ".");
    }
    emit changed();
    return problems.isEmpty();
}

QString IntegrationSettings::quirkInstructions() const {
    return QStringLiteral(
        "Linux 7.3 cannot load the CONNECT 2's mixer because one of the device's microphone controls is broken, "
        "so the desktop has no hardware volume for it. A kernel option for this one device works around that. "
        "OpenWitty never installs it for you. To install it, create the file\n"
        "  /etc/modprobe.d/openwitty-connect2.conf\n"
        "with this line (administrator rights needed):\n"
        "  options snd_usb_audio quirk_flags=29c2:0004:ignore_ctl_error\n"
        "It takes effect after unplugging and replugging the CONNECT 2, or after a reboot. "
        "Side effect: errors of that broken control are reported as 0 instead of as errors. "
        "To undo, delete the file.");
}

void IntegrationSettings::copyQuirkInstructions() {
    if (auto* cb = QGuiApplication::clipboard())
        cb->setText("options snd_usb_audio quirk_flags=29c2:0004:ignore_ctl_error\n");
}
