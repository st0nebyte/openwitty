// Presets (P3): OpenWitty's own preset files (JSON, "openwitty-preset" version 1) with the CONNECT 2's user settings.
// The official app's .preset format is not decoded (it may be CONNECT 6 only), so these files are OpenWitty's own.
//
// A preset is applied through the ordinary GUI write path, one verified SET per setting, in a safe order: mutes and
// level reductions first, increases and unmutes last. Nothing is written without the user confirming the change list.
// Settings without permission on the real device are listed and skipped. Not part of a preset: the Denoise noise level
// (the device measures it), Dim, the device modes (macOS / mobile streaming) and, unless chosen, the output levels.
#pragma once
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <optional>

namespace presets {

inline constexpr const char* kFormat = "openwitty-preset";
inline constexpr int kVersion = 1;
inline constexpr const char* kSuffix = ".owpreset";

struct Field {
    QString key;       // GUI control key (DeviceController::setValue / values)
    QString section;   // JSON object: microphone, instrument, monitoring, speakers, headphones, device, ledColors
    QString name;      // JSON member
    QString label;     // shown in the change list
    enum Kind { Bool, Gain, Volume, Mix, Preamp, Mapping, Brightness, Button, Color } kind;
    bool output{false};   // only with "include output levels"
};
const QList<Field>& fields();

// The preset file for the device values `values` (DeviceController::values()).
QJsonObject toJson(const QString& name, const QVariantMap& values, bool includeOutputs);

struct Parsed {
    QString name;
    QVariantMap values;     // control key -> value, validated
    QStringList ignored;    // members that were not understood (newer version, typos)
};
std::optional<Parsed> fromJson(const QByteArray& data, QString* error);

struct Step {
    QString key;
    QVariant value;
    QVariant from;
    QString label;
    QString fromText, toText;
    bool warn{false};       // can get louder, feed back or needs care (48 V)
};
struct Plan {
    QList<Step> steps;         // in the order they are applied
    QStringList notAllowed;    // labels of changes without permission (skipped)
};
// `can(key)`: may this control be written (DeviceController values "can.<key>").
Plan plan(const QVariantMap& preset, const QVariantMap& current, const std::function<bool(const QString&)>& can);

// Readable name -> safe file name (letters, digits, space, - and _; at most 64 characters).
QString fileNameFor(const QString& name);

}  // namespace presets

class DeviceController;

// QML facade ("presets"): the files in <data>/openwitty/presets.
class PresetStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged)
    Q_PROPERTY(QString folder READ folder CONSTANT)
public:
    explicit PresetStore(DeviceController* device, QString folder = {}, QObject* parent = nullptr);
    QVariantList list() const { return list_; }
    QString folder() const { return folder_; }
    Q_INVOKABLE void refresh();
    // Saves the device's current settings; an existing preset of that name is replaced only with `overwrite`.
    // Returns "" or an error text.
    Q_INVOKABLE QString save(const QString& name, bool includeOutputs, bool overwrite);
    Q_INVOKABLE bool exists(const QString& name) const;
    // {ok, error, name, changes: [{key, label, from, to, warn}], notAllowed: [...], ignored: [...]}
    Q_INVOKABLE QVariantMap preview(const QString& file) const;
    // Sends the steps of preview(file) as they are now (computed again against the current device values).
    Q_INVOKABLE bool apply(const QString& file);
    Q_INVOKABLE bool remove(const QString& file);
    Q_INVOKABLE void openFolder() const;
signals:
    void listChanged();
private:
    DeviceController* device_;
    QString folder_;
    QVariantList list_;
    std::optional<presets::Plan> planFor(const QString& file, QString* error, QVariantMap* info) const;
};
