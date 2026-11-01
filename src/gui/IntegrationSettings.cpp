#include "IntegrationSettings.hpp"
#include "desktop/PortNames.hpp"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>

#include "GuiActivation.hpp"
#include "ServiceState.hpp"
#include "TrayAutostart.hpp"
#include "core/OutputFollow.hpp"
#include "desktop/SampleRates.hpp"
#include "desktop/SplitSources.hpp"

using namespace lewitt;

namespace {
constexpr const char* kUnit = kVolumeServiceUnit;
}  // namespace

static std::optional<AudioSetupStatus> realProbe() {
#ifdef LEWITT_HAVE_ALSA
    return collectAudioSetup();
#else
    return std::nullopt;
#endif
}

IntegrationSettings::IntegrationSettings(QObject* parent, Runner runner, SetupProbe probe, Launcher launcher,
                                         QString trayProgram)
    : QObject(parent), runner_(runner ? std::move(runner) : Runner(runCommand)),
      probe_(probe ? std::move(probe) : SetupProbe(realProbe)),
      launcher_(launcher ? std::move(launcher) : Launcher(spawnDetached)),
      trayProgram_(trayProgram.isEmpty() ? siblingProgram("openwitty-tray") : std::move(trayProgram)),
      trayAutostart_(trayAutostartEnabled()),
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
    trayAutostart_ = trayAutostartEnabled();
    refreshService();
    refreshSetup();
    emit changed();
}

void IntegrationSettings::refreshService() {
    serviceState_ = serviceStateName(queryServiceState(runner_));
}

bool IntegrationSettings::trayInstalled() const {
    const QFileInfo fi(trayProgram_);
    return fi.isAbsolute() ? fi.isExecutable() : !QStandardPaths::findExecutable(trayProgram_).isEmpty();
}

bool IntegrationSettings::setTrayAutostart(bool on) {
    QString err;
    bool ok;
    if (on) {
        ok = trayInstalled() && enableTrayAutostart(trayProgram_, &err);
        if (!trayInstalled()) err = "the tray icon (openwitty-tray) is not installed";
        if (ok && !launcher_(trayProgram_, {})) err = "the login entry was written, but the tray icon could not be started now";
    } else {
        ok = disableTrayAutostart(&err);
        if (trayInstalled()) run(trayProgram_, {"--quit"});   // a running icon goes away now; harmless if none runs
    }
    trayAutostart_ = trayAutostartEnabled();
    lastError_ = err.isEmpty() ? QString() : "System tray icon: " + err + ".";
    emit changed();
    return ok && err.isEmpty();
}

QString IntegrationSettings::findConnect2CaptureNode(const QByteArray& pwDumpJson) {
    const auto doc = QJsonDocument::fromJson(pwDumpJson);
    for (const auto& v : doc.array()) {
        const auto props = v.toObject().value("info").toObject().value("props").toObject();
        const QString name = props.value("node.name").toString();
        if (props.value("media.class").toString() == "Audio/Source" && isConnect2CaptureNodeName(name.toStdString()))
            return name;
    }
    return {};
}

QString IntegrationSettings::currentCaptureNode() {
    QString out;
    if (run("pw-dump", {}, &out) != 0) return {};
    return findConnect2CaptureNode(out.toUtf8());
}

bool IntegrationSettings::setSplitSources(bool on) {
    const QString path = QString::fromStdString(splitSourcesPath(userConfigHome()));
    lastError_.clear();
    if (path.isEmpty()) { lastError_ = "Separate inputs: no configuration directory."; emit changed(); return false; }
    if (!on) {
        if (QFile::exists(path) && !QFile::remove(path)) lastError_ = "Separate inputs: could not remove " + path + ".";
        else audioRestartNeeded_ = true;
    } else {
        const QString node = currentCaptureNode();
        const auto text = renderSplitSourcesConfig(node.toStdString());
        if (!text) {
            lastError_ = "Separate inputs: the CONNECT 2 input was not found in PipeWire (is it connected?).";
        } else {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(QByteArray::fromStdString(*text)) < 0)
                lastError_ = "Separate inputs: could not write " + path + ".";
            else audioRestartNeeded_ = true;
        }
    }
    refreshSetup();
    emit changed();
    return lastError_.isEmpty();
}

bool IntegrationSettings::setExtraSampleRates(bool on) {
    const QString path = QString::fromStdString(sampleRatesPath(userConfigHome()));
    lastError_.clear();
    if (path.isEmpty()) { lastError_ = "Sample rates: no configuration directory."; emit changed(); return false; }
    if (!on) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            const bool ours = isOpenWittySampleRatesConfig(f.readAll().toStdString());
            f.close();
            if (!ours) lastError_ = "Sample rates: " + path + " was not written by OpenWitty; left unchanged.";
            else if (!QFile::remove(path)) lastError_ = "Sample rates: could not remove " + path + ".";
            else audioRestartNeeded_ = true;
        }
    } else {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || f.write(QByteArray::fromStdString(renderSampleRatesConfig())) < 0)
            lastError_ = "Sample rates: could not write " + path + ".";
        else audioRestartNeeded_ = true;
    }
    refreshSetup();
    emit changed();
    return lastError_.isEmpty();
}

bool IntegrationSettings::restartAudio(bool confirmed) {
    if (!confirmed) return false;
    const int rc = run("systemctl", {"--user", "restart", "pipewire.service", "pipewire-pulse.service", "wireplumber.service"});
    if (rc != 0) lastError_ = "Restarting desktop audio failed (systemctl exit " + QString::number(rc) + ").";
    else { lastError_.clear(); audioRestartNeeded_ = false; }
    refreshSetup();
    emit changed();
    // PipeWire brings the devices back a few seconds after the restart: look again then.
    if (rc == 0) QTimer::singleShot(4000, this, [this] { refreshSetup(); emit changed(); });
    return rc == 0;
}

void IntegrationSettings::refreshSetup() {
    softwareInputMute_ = softwareInputMuteConfigured(userConfigHome());
    {
        const QString path = QString::fromStdString(splitSourcesPath(userConfigHome()));
        QFile f(path);
        if (!path.isEmpty() && f.open(QIODevice::ReadOnly)) {
            const auto target = splitSourcesTarget(f.readAll().toStdString());
            const QString node = currentCaptureNode();
            // "stale" only when ANOTHER CONNECT 2 input is present; right after an audio restart (or while the device
            // is unplugged) no input is present yet, and the file is still right for it.
            splitSources_ = (target && (node.isEmpty() || QString::fromStdString(*target) == node)) ? "on" : "stale";
        } else {
            splitSources_ = currentCaptureNode().isEmpty() ? "unavailable" : "off";
        }
    }
    {
        const QString path = QString::fromStdString(sampleRatesPath(userConfigHome()));
        QFile f(path);
        extraSampleRates_ = !path.isEmpty() && f.open(QIODevice::ReadOnly)
                            && isOpenWittySampleRatesConfig(f.readAll().toStdString());
        QString out;
        liveSampleRates_.clear();
        if (run("pw-metadata", {"-n", "settings", "0", "clock.allowed-rates"}, &out) == 0)
            if (auto r = parseAllowedRates(out.toStdString())) {
                QStringList parts;
                for (int hz : *r) parts << QString::number(hz / 1000.0, 'f', hz % 1000 ? 1 : 0) + " kHz";
                liveSampleRates_ = parts.join(", ");
            }
    }
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

namespace {
QString prefName(lewitt::FollowPref f) {
    using lewitt::FollowPref;
    return f == FollowPref::Speakers ? "speakers" : f == FollowPref::Headphones ? "headphones"
         : f == FollowPref::Keep ? "keep" : "disable";
}
std::optional<lewitt::FollowPref> prefFrom(const QString& v, bool allowDisable) {
    using lewitt::FollowPref;
    if (v == "speakers") return FollowPref::Speakers;
    if (v == "headphones") return FollowPref::Headphones;
    if (v == "keep") return FollowPref::Keep;
    if (v == "disable" && allowDisable) return FollowPref::Disable;
    return std::nullopt;
}
}  // namespace

QString IntegrationSettings::followBothOn() const { return prefName(cfg_.followBothOn); }
QString IntegrationSettings::followBothMuted() const { return prefName(cfg_.followBothMuted); }
void IntegrationSettings::setFollowBothOn(const QString& v) {
    const auto p = prefFrom(v, false);
    if (!p || *p == cfg_.followBothOn) return;
    UserConfig next = cfg_;
    next.followBothOn = *p;
    save(next);
}
void IntegrationSettings::setFollowBothMuted(const QString& v) {
    const auto p = prefFrom(v, true);
    if (!p || *p == cfg_.followBothMuted) return;
    UserConfig next = cfg_;
    next.followBothMuted = *p;
    save(next);
}

QVariantMap IntegrationSettings::buttonFollow() const {
    QVariantMap m;
    for (const auto& e : cfg_.buttonFollow)
        if (e.size() == 18) m[QString::fromStdString(e.substr(0, 16))] = e[17] - '0';
    return m;
}

void IntegrationSettings::setButtonFollow(const QString& deviceKey, int base, bool confirmed) {
    const std::string key = deviceKey.toStdString();
    if (key.size() != 16 || key.find_first_not_of("0123456789abcdef") != std::string::npos) return;
    const bool set = base == 1 || base == 2;
    if (set && !confirmed) return;                       // choosing it authorizes device writes: needs the dialog
    UserConfig next = lewitt::withButtonFollow(cfg_, key, set ? std::optional<std::uint8_t>(std::uint8_t(base))
                                                              : std::nullopt);
    if (next.buttonFollow == cfg_.buttonFollow) return;
    save(next);
}

void IntegrationSettings::setFollowMoveFocus(bool on) {
    if (on == cfg_.followMoveFocus) return;
    UserConfig next = cfg_;
    next.followMoveFocus = on;
    save(next);
}

namespace {
// The window's command runner, in the shape the desktop helpers use.
lewitt::CommandRunner commandRunner(const IntegrationSettings::Runner& r) {
    return [r](const std::vector<std::string>& argv, std::string* out) {
        if (argv.empty()) return -1;
        QStringList args;
        for (std::size_t i = 1; i < argv.size(); ++i) args << QString::fromStdString(argv[i]);
        QString o;
        const int rc = r(QString::fromStdString(argv[0]), args, &o);
        if (out) *out = o.toStdString();
        return rc;
    };
}
}  // namespace

void IntegrationSettings::setNameChannels(bool on) {
    if (on == cfg_.nameChannels) return;
    UserConfig next = cfg_;
    next.nameChannels = on;
    save(next);
    if (on) lewitt::applyPortNames(commandRunner(runner_));
    else lewitt::clearPortNames(commandRunner(runner_));
}

int IntegrationSettings::applyChannelNames() {
    return cfg_.nameChannels ? lewitt::applyPortNames(commandRunner(runner_)) : 0;
}

QVariantMap IntegrationSettings::permissions() const {
    QVariantMap m;
    for (const auto& p : lewitt::devicePermissions()) m[QString::fromUtf8(p.name)] = cfg_.*(p.field);
    return m;
}

QString IntegrationSettings::permissionDescription(const QString& name) const {
    const auto* p = lewitt::findDevicePermission(name.toStdString());
    return p ? QString::fromUtf8(p->description) : QString();
}

void IntegrationSettings::setPermission(const QString& name, bool on, bool confirmed) {
    const auto* p = lewitt::findDevicePermission(name.toStdString());
    if (!p) return;
    if (on && !confirmed) return;
    UserConfig next = cfg_;
    next.*(p->field) = on;
    if (on && (name == "speaker" || name == "headphone")) next.legacyOutputControlPending = false;   // reconfirmed
    save(next);
}

void IntegrationSettings::setSpeakerControl(bool on, bool confirmed) { setPermission("speaker", on, confirmed); }
void IntegrationSettings::setHeadphoneControl(bool on, bool confirmed) { setPermission("headphone", on, confirmed); }
void IntegrationSettings::setLedBrightnessControl(bool on, bool confirmed) { setPermission("ledBrightness", on, confirmed); }

void IntegrationSettings::dismissLegacyOutputControl() {
    UserConfig next = cfg_;
    next.legacyOutputControlPending = false;
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
        lastError_ = "Turn on \"Show the volume knob in the desktop volume\" first: with desktop sync off the "
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
    if (trayAutostart_ && !setTrayAutostart(false)) problems << "the tray icon login entry could not be removed";
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
        "To undo, delete the file.\n\n"
        "Speaker level after a logout: with this option the login screen's own audio service can also set the "
        "CONNECT 2's speaker level. After a logout the speaker was observed at about -23.5 dB instead of the knob "
        "position - louder or quieter than before. Turn your speakers down before logging out if that matters. "
        "An optional fix for the login screen is described in INSTALLATION.md (not yet validated).");
}

void IntegrationSettings::copyQuirkInstructions() {
    if (auto* cb = QGuiApplication::clipboard())
        cb->setText("options snd_usb_audio quirk_flags=29c2:0004:ignore_ctl_error\n");
}
