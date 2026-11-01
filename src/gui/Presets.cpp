#include "Presets.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <cmath>

#include "DeviceController.hpp"
#include "protocol/Messages.hpp"

namespace presets {

const QList<Field>& fields() {
    static const QList<Field> f = [] {
        QList<Field> l;
        const QString names[2] = {"microphone", "instrument"};
        const QString titles[2] = {"Microphone", "Instrument"};
        for (int i = 0; i < 2; ++i) {
            const QString p = QString("in%1.").arg(i + 1), s = names[i], t = titles[i];
            l.push_back({p + "gain", s, "gain", t + " gain", Field::Gain});
            l.push_back({p + "mute", s, "mute", t + " mute", Field::Bool});
            l.push_back({p + "highPass", s, "lowCut", t + " low cut", Field::Bool});
            l.push_back({p + "clipguard", s, "clipguard", t + " Clipguard", Field::Bool});
            l.push_back({p + "preamp", s, "preamp", t + " preamp", Field::Preamp});
            l.push_back({p + "denoise", s, "denoise", t + " Denoise", Field::Bool});
            l.push_back({p + "compress", s, "compressor", t + " Comp", Field::Bool});
            if (i == 0) l.push_back({"in1.phantom", s, "phantom48V", "48 V phantom power", Field::Bool});
        }
        l.push_back({"mon.mix", "monitoring", "inputPlaybackMix", "Input/playback mix", Field::Mix});
        l.push_back({"mon.mic", "monitoring", "microphone", "Monitor microphone", Field::Bool});
        l.push_back({"mon.inst", "monitoring", "instrument", "Monitor instrument", Field::Bool});
        l.push_back({"out.mono", "monitoring", "mono", "Mono", Field::Bool});
        l.push_back({"loop.mute", "monitoring", "loopbackMute", "Loopback mute", Field::Bool});
        l.push_back({"spk.volume", "speakers", "volume", "Speaker volume", Field::Volume, true});
        l.push_back({"spk.mute", "speakers", "mute", "Speaker mute", Field::Bool, true});
        l.push_back({"hp.volume", "headphones", "volume", "Headphone volume", Field::Volume, true});
        l.push_back({"hp.mute", "headphones", "mute", "Headphone mute", Field::Bool, true});
        l.push_back({"set.gainLock", "device", "gainLock", "Gain lock", Field::Bool});
        l.push_back({"set.mixToSpeakers", "device", "mixToSpeakers", "Mix applies to speakers", Field::Bool});
        l.push_back({"set.volumeMapping", "device", "volumeKeys", "Volume keys control", Field::Mapping});
        l.push_back({"set.highImpedance", "device", "highImpedanceHeadphones", "High impedance headphone mode", Field::Bool});
        l.push_back({"set.highVolume", "device", "highVolumeSpeakers", "High volume speaker mode", Field::Bool});
        l.push_back({"set.customButton", "device", "customButton", "Custom button", Field::Button});
        l.push_back({"set.ledBrightness", "device", "ledBrightness", "LED brightness", Field::Brightness});
        for (const auto& [k, n] : {std::pair{"icons", "Icons"}, {"mute", "Mute"}, {"parameter", "Parameter"},
                                   {"metering", "Metering"}, {"gainReduction", "Gain reduction"}})
            l.push_back({QString("set.led.") + k, "ledColors", k, QString("LED colour: ") + n, Field::Color});
        return l;
    }();
    return f;
}

namespace {
const QStringList kPreamps{"clean", "warm", "vivid"};
const QStringList kMappings{"none", "speakers", "headphones"};

QJsonValue encode(const Field& f, const QVariant& v) {
    switch (f.kind) {
        case Field::Bool: return v.toBool();
        case Field::Gain: case Field::Volume: case Field::Mix: return std::round(v.toDouble() * 2.0) / 2.0;
        case Field::Preamp: return kPreamps.value(v.toInt(), "clean");
        case Field::Mapping: return kMappings.value(v.toInt(), "none");
        case Field::Brightness: case Field::Button: return v.toInt();
        case Field::Color: return v.toString().toLower();
    }
    return {};
}

// nullopt: not a valid value for this field.
std::optional<QVariant> decode(const Field& f, const QJsonValue& j) {
    const bool num = j.isDouble() && std::isfinite(j.toDouble());
    switch (f.kind) {
        case Field::Bool:
            if (j.isBool()) return QVariant(j.toBool());
            return std::nullopt;
        case Field::Gain: {
            const double lim = lewitt::gainLimitDb(f.key.startsWith("in1") ? 0 : 1);
            if (num && j.toDouble() >= 0.0 && j.toDouble() <= lim) return QVariant(j.toDouble());
            return std::nullopt;
        }
        case Field::Volume:
            if (num && j.toDouble() >= lewitt::kVolumeMinDb && j.toDouble() <= lewitt::kVolumeMaxDb) return QVariant(j.toDouble());
            return std::nullopt;
        case Field::Mix:
            if (num && j.toDouble() >= -100.0 && j.toDouble() <= 0.0) return QVariant(j.toDouble());
            return std::nullopt;
        case Field::Preamp:
            if (j.isString() && kPreamps.contains(j.toString().toLower())) return QVariant(int(kPreamps.indexOf(j.toString().toLower())));
            return std::nullopt;
        case Field::Mapping:
            if (j.isString() && kMappings.contains(j.toString().toLower())) return QVariant(int(kMappings.indexOf(j.toString().toLower())));
            return std::nullopt;
        case Field::Brightness:
            if (num && j.toDouble() == std::floor(j.toDouble()) && j.toDouble() >= 0 && j.toDouble() <= 100) return QVariant(j.toInt());
            return std::nullopt;
        case Field::Button:
            if (num && j.toDouble() == std::floor(j.toDouble()) && j.toDouble() >= 0 && j.toDouble() <= 255 &&
                lewitt::isKnownCustomButton(static_cast<std::uint8_t>(j.toInt())))
                return QVariant(j.toInt());
            return std::nullopt;
        case Field::Color: {
            static const QRegularExpression re("^#[0-9a-fA-F]{6}$");
            if (j.isString() && re.match(j.toString()).hasMatch()) return QVariant(j.toString().toLower());
            return std::nullopt;
        }
    }
    return std::nullopt;
}

QString text(const Field& f, const QVariant& v) {
    if (!v.isValid()) return "?";
    switch (f.kind) {
        case Field::Bool:
            if (f.name == "mute" || f.name == "loopbackMute") return v.toBool() ? "muted" : "on";
            return v.toBool() ? "on" : "off";
        case Field::Gain: case Field::Volume: return QString::number(v.toDouble(), 'f', 1) + " dB";
        case Field::Mix: return QString::number(v.toDouble(), 'f', 0);
        case Field::Preamp: { auto s = kPreamps.value(v.toInt(), "?"); s[0] = s[0].toUpper(); return s; }
        case Field::Mapping: { auto s = kMappings.value(v.toInt(), "?"); s[0] = s[0].toUpper(); return s; }
        case Field::Brightness: return QString::number(v.toInt()) + " %";
        case Field::Button: return QString::number(v.toInt());
        case Field::Color: return v.toString();
    }
    return v.toString();
}

bool same(const Field& f, const QVariant& a, const QVariant& b) {
    if (!a.isValid() || !b.isValid()) return false;
    switch (f.kind) {
        case Field::Gain: case Field::Volume: case Field::Mix: return std::abs(a.toDouble() - b.toDouble()) < 0.26;
        case Field::Bool: return a.toBool() == b.toBool();
        case Field::Color: return a.toString().compare(b.toString(), Qt::CaseInsensitive) == 0;
        default: return a.toInt() == b.toInt();
    }
}
}  // namespace

QJsonObject toJson(const QString& name, const QVariantMap& values, bool includeOutputs) {
    QJsonObject root{{"format", kFormat}, {"version", kVersion}, {"device", "LEWITT CONNECT 2"}, {"name", name},
                     {"created", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    QJsonObject sections;
    for (const auto& f : fields()) {
        if (f.output && !includeOutputs) continue;
        if (!values.contains(f.key)) continue;
        QJsonObject s = sections.value(f.section).toObject();
        s.insert(f.name, encode(f, values.value(f.key)));
        sections.insert(f.section, s);
    }
    QJsonObject device = sections.value("device").toObject();
    if (sections.contains("ledColors")) device.insert("ledColors", sections.take("ledColors"));
    if (!device.isEmpty()) sections.insert("device", device);
    if (sections.contains("speakers") || sections.contains("headphones")) {
        QJsonObject out;
        for (const char* k : {"speakers", "headphones"})
            if (sections.contains(k)) out.insert(k, sections.take(k));
        sections.insert("outputs", out);
    }
    for (auto it = sections.begin(); it != sections.end(); ++it) root.insert(it.key(), it.value());
    return root;
}

std::optional<Parsed> fromJson(const QByteArray& data, QString* error) {
    auto fail = [&](const QString& e) { if (error) *error = e; return std::nullopt; };
    if (data.size() > 256 * 1024) return fail("The file is too large for a preset.");
    QJsonParseError pe{};
    const auto doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) return fail("Not a preset file (invalid JSON).");
    const QJsonObject root = doc.object();
    if (root.value("format").toString() != kFormat) return fail("Not an OpenWitty preset file.");
    if (!root.value("version").isDouble() || root.value("version").toInt() < 1)
        return fail("The preset file has no valid version.");
    if (root.value("version").toInt() > kVersion)
        return fail("This preset was saved by a newer OpenWitty (format version " +
                    QString::number(root.value("version").toInt()) + ").");
    Parsed p;
    p.name = root.value("name").toString().left(64);
    // Flatten: outputs.{speakers,headphones} and device.ledColors become their own sections.
    QMap<QString, QJsonObject> sections;
    static const QStringList known{"format", "version", "device", "name", "created", "microphone", "instrument",
                                   "monitoring", "outputs"};
    for (auto it = root.begin(); it != root.end(); ++it)
        if (!known.contains(it.key())) p.ignored << it.key();
    for (const char* k : {"microphone", "instrument", "monitoring"})
        if (root.value(k).isObject()) sections[k] = root.value(k).toObject();
    if (root.value("outputs").isObject())
        for (const char* k : {"speakers", "headphones"})
            if (root.value("outputs").toObject().value(k).isObject()) sections[k] = root.value("outputs").toObject().value(k).toObject();
    if (root.value("device").isObject()) {
        QJsonObject d = root.value("device").toObject();
        if (d.value("ledColors").isObject()) sections["ledColors"] = d.take("ledColors").toObject();
        sections["device"] = d;
    }
    for (auto it = sections.begin(); it != sections.end(); ++it) {
        for (auto m = it.value().begin(); m != it.value().end(); ++m) {
            const Field* field = nullptr;
            for (const auto& f : fields())
                if (f.section == it.key() && f.name == m.key()) field = &f;
            if (!field) { p.ignored << it.key() + "." + m.key(); continue; }
            auto v = decode(*field, m.value());
            if (!v) return fail(QString("Invalid value for %1.").arg(field->label));
            p.values.insert(field->key, *v);
        }
    }
    if (p.values.isEmpty()) return fail("The preset contains no settings.");
    return p;
}

Plan plan(const QVariantMap& preset, const QVariantMap& current, const std::function<bool(const QString&)>& can) {
    // Order groups: 0 quieter (mute on, level down, loud modes off), 1 neutral, 2 48 V, 3 louder (levels up, loud modes
    // on, mix to speakers on), 4 unmute, 5 gain lock on (after the gains).
    QList<QPair<int, Step>> ordered;
    Plan out;
    for (const auto& f : fields()) {
        if (!preset.contains(f.key)) continue;
        const QVariant to = preset.value(f.key), from = current.value(f.key);
        if (same(f, to, from)) continue;
        Step s{f.key, to, from, f.label, text(f, from), text(f, to), false};
        if (!can(f.key)) { out.notAllowed << f.label + " (" + s.fromText + " → " + s.toText + ")"; continue; }
        const bool isMute = f.name == "mute" || f.name == "loopbackMute";
        const bool up = (f.kind == Field::Gain || f.kind == Field::Volume) && to.toDouble() > from.toDouble();
        const bool loudMode = f.key == "set.highImpedance" || f.key == "set.highVolume" || f.key == "set.mixToSpeakers";
        int group = 1;
        if (isMute) group = to.toBool() ? 0 : 4;
        else if (f.kind == Field::Gain || f.kind == Field::Volume) group = up ? 3 : 0;
        else if (loudMode) group = to.toBool() ? 3 : 0;
        else if (f.key == "in1.phantom") group = 2;
        else if (f.key == "set.gainLock") group = to.toBool() ? 5 : 0;
        // Monitor source: the device keeps one source selected, so a source is switched on before the other goes off.
        else if (f.key == "mon.mic" || f.key == "mon.inst") group = to.toBool() ? 1 : 2;
        s.warn = group == 3 || (group == 4 && f.key != "loop.mute") || (f.key == "in1.phantom" && to.toBool()) ||
                 f.key.startsWith("mon.") || f.key == "set.volumeMapping";
        ordered.push_back({group, s});
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [g, s] : ordered) out.steps.push_back(s);
    return out;
}

QString fileNameFor(const QString& name) {
    QString n;
    for (QChar c : name.trimmed())
        if (c.isLetterOrNumber() || c == ' ' || c == '-' || c == '_') n += c;
    n = n.simplified().left(64);
    return n.isEmpty() ? QString() : n + kSuffix;
}

}  // namespace presets

PresetStore::PresetStore(DeviceController* device, QString folder, QObject* parent)
    : QObject(parent), device_(device), folder_(std::move(folder)) {
    if (folder_.isEmpty())
        folder_ = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/openwitty/presets";
    refresh();
}

void PresetStore::refresh() {
    QVariantList l;
    QDir d(folder_);
    for (const auto& fi : d.entryInfoList({QString("*") + presets::kSuffix}, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        QFile f(fi.absoluteFilePath());
        QString name = fi.completeBaseName();
        if (f.open(QIODevice::ReadOnly)) {
            QString err;
            if (auto p = presets::fromJson(f.read(256 * 1024 + 1), &err); p && !p->name.isEmpty()) name = p->name;
        }
        l.push_back(QVariantMap{{"name", name}, {"file", fi.fileName()},
                                {"modified", fi.lastModified().toString("yyyy-MM-dd HH:mm")}});
    }
    list_ = l;
    emit listChanged();
}

bool PresetStore::exists(const QString& name) const {
    const QString f = presets::fileNameFor(name);
    return !f.isEmpty() && QFileInfo::exists(folder_ + "/" + f);
}

QString PresetStore::save(const QString& name, bool includeOutputs, bool overwrite) {
    const QString file = presets::fileNameFor(name);
    if (file.isEmpty()) return "Enter a name (letters, digits, spaces, - or _).";
    const QVariantMap v = device_->values();
    if (!v.contains("in1.gain")) return "No device settings to save (not connected).";
    if (!overwrite && QFileInfo::exists(folder_ + "/" + file)) return "A preset with this name exists.";
    if (!QDir().mkpath(folder_)) return "Cannot create " + folder_ + ".";
    QSaveFile f(folder_ + "/" + file);
    if (!f.open(QIODevice::WriteOnly)) return "Cannot write " + f.fileName() + ".";
    f.write(QJsonDocument(presets::toJson(name.simplified().left(64), v, includeOutputs)).toJson(QJsonDocument::Indented));
    if (!f.commit()) return "Cannot write " + f.fileName() + ".";
    refresh();
    return {};
}

std::optional<presets::Plan> PresetStore::planFor(const QString& file, QString* error, QVariantMap* info) const {
    if (file.isEmpty() || file.contains('/') || !file.endsWith(presets::kSuffix)) { *error = "Unknown preset."; return std::nullopt; }
    QFile f(folder_ + "/" + file);
    if (!f.open(QIODevice::ReadOnly)) { *error = "Cannot read the preset."; return std::nullopt; }
    auto p = presets::fromJson(f.read(256 * 1024 + 1), error);
    if (!p) return std::nullopt;
    const QVariantMap cur = device_->values();
    if (!cur.contains("in1.gain")) { *error = "No device settings yet (not connected)."; return std::nullopt; }
    if (info) { (*info)["name"] = p->name.isEmpty() ? QFileInfo(file).completeBaseName() : p->name; (*info)["ignored"] = p->ignored; }
    return presets::plan(p->values, cur, [&](const QString& k) { return cur.value("can." + k).toBool(); });
}

QVariantMap PresetStore::preview(const QString& file) const {
    QString err;
    QVariantMap out;
    auto pl = planFor(file, &err, &out);
    out["ok"] = pl.has_value();
    out["error"] = err;
    if (!pl) return out;
    QVariantList changes;
    for (const auto& s : pl->steps)
        changes.push_back(QVariantMap{{"key", s.key}, {"label", s.label}, {"from", s.fromText}, {"to", s.toText}, {"warn", s.warn}});
    out["changes"] = changes;
    out["notAllowed"] = pl->notAllowed;
    return out;
}

bool PresetStore::apply(const QString& file) {
    QString err;
    auto pl = planFor(file, &err, nullptr);
    if (!pl || pl->steps.isEmpty()) return false;
    QVariantList steps;
    for (const auto& s : pl->steps) steps.push_back(QVariantMap{{"key", s.key}, {"value", s.value}});
    device_->applyPreset(QFileInfo(file).completeBaseName(), steps);
    return true;
}

bool PresetStore::remove(const QString& file) {
    if (file.isEmpty() || file.contains('/') || !file.endsWith(presets::kSuffix)) return false;
    const bool ok = QFile::remove(folder_ + "/" + file);
    refresh();
    return ok;
}

void PresetStore::openFolder() const {
    QDir().mkpath(folder_);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder_));
}
