#include "core/presets/preset.h"

#include <QJsonObject>

#include <utility>

namespace vt {

QJsonDocument Preset::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("vectorTypographyPreset"));
    root.insert(QStringLiteral("formatVersion"), CurrentFormatVersion);
    root.insert(QStringLiteral("id"), id);
    root.insert(QStringLiteral("name"), name);
    root.insert(QStringLiteral("effects"), effects.toJson());
    return QJsonDocument(root);
}

bool Preset::fromJson(const QJsonDocument& json, Preset* preset, QString* error)
{
    if (!preset || !json.isObject()) {
        if (error) {
            *error = QStringLiteral("Preset JSON must contain an object at its root.");
        }
        return false;
    }

    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("vectorTypographyPreset")) {
        if (error) {
            *error = QStringLiteral("The file is not a Vector Typography preset.");
        }
        return false;
    }
    const int version = root.value(QStringLiteral("formatVersion")).toInt(-1);
    if (version < 1 || version > CurrentFormatVersion) {
        if (error) {
            *error = QStringLiteral("Unsupported preset format version %1.").arg(version);
        }
        return false;
    }
    const QJsonValue effectsValue = root.value(QStringLiteral("effects"));
    if (!effectsValue.isArray()) {
        if (error) {
            *error = QStringLiteral("Preset is missing its effect stack array.");
        }
        return false;
    }

    Preset result;
    result.formatVersion = CurrentFormatVersion;
    result.id = root.value(QStringLiteral("id")).toString();
    result.name = root.value(QStringLiteral("name")).toString();
    QString effectError;
    result.effects = EffectStack::fromJson(effectsValue.toArray(), &effectError);
    if (!effectError.isEmpty()) {
        if (error) {
            *error = effectError;
        }
        return false;
    }
    *preset = std::move(result);
    return true;
}

} // namespace vt
