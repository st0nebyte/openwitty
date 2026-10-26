#include "GuiPreferences.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace {
int parseBool(const QString& v) {   // 1 / 0 / -1 (invalid)
    if (v == "true") return 1;
    if (v == "false") return 0;
    return -1;
}
}  // namespace

QString GuiPreferences::defaultPath() {
    const QString x = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!x.isEmpty() && QDir::isAbsolutePath(x)) return x + "/openwitty/gui.conf";
    const QString home = qEnvironmentVariable("HOME");
    return home.isEmpty() ? QString() : home + "/.config/openwitty/gui.conf";
}

GuiPreferences::GuiPreferences(QString path, QObject* parent)
    : QObject(parent), path_(path.isEmpty() ? defaultPath() : std::move(path)) {
    reload();
}

void GuiPreferences::reload() {
    onboardingDone_ = false;
    showTooltips_ = true;
    QFile f(path_);
    if (path_.isEmpty() || !f.open(QIODevice::ReadOnly | QIODevice::Text)) return;   // missing: defaults
    int onboarding = -2, tooltips = -2;                     // -2 unseen, -3 duplicated
    for (const QByteArray& raw : f.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).section('#', 0, 0).trimmed();
        if (line.isEmpty() || line == "[gui]") continue;
        const QString key = line.section('=', 0, 0).trimmed(), value = line.section('=', 1).trimmed();
        int* slot = key == "onboarding_done" ? &onboarding : key == "show_tooltips" ? &tooltips : nullptr;
        if (!slot) continue;                                // unknown keys are ignored
        *slot = *slot == -2 ? parseBool(value) : -3;        // a duplicate is ambiguous: default
    }
    if (onboarding == 1) onboardingDone_ = true;
    if (tooltips == 0) showTooltips_ = false;
}

bool GuiPreferences::save() {
    if (path_.isEmpty()) {
        error_ = "no configuration directory";
        return false;
    }
    QDir().mkpath(QFileInfo(path_).absolutePath());
    QSaveFile f(path_);
    const QByteArray text = QByteArray("# OpenWitty window preferences (not device settings). Delete to reset.\n[gui]\n") +
                            "onboarding_done = " + (onboardingDone_ ? "true" : "false") + "\n" +
                            "show_tooltips = " + (showTooltips_ ? "true" : "false") + "\n";
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || f.write(text) != text.size() || !f.commit()) {
        error_ = "cannot save " + path_;
        return false;
    }
    error_.clear();
    return true;
}

void GuiPreferences::setOnboardingDone(bool done) {
    if (onboardingDone_ == done) return;
    onboardingDone_ = done;
    save();
    emit changed();
}

void GuiPreferences::setShowTooltips(bool on) {
    if (showTooltips_ == on) return;
    showTooltips_ = on;
    save();
    emit changed();
}
