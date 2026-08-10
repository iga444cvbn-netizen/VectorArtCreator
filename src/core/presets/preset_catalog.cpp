#include "core/presets/preset_catalog.h"

#include "core/effects/effect_registry.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QUuid>

namespace vt {

PresetCatalog::PresetCatalog(const PresetManager& userPresets) : m_userPresets(userPresets) {}

QVector<PresetCatalogEntry> PresetCatalog::builtinEntries(QString* error) const
{
    QFile resource(QStringLiteral(":/presets/builtins.json"));
    if (!resource.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Built-in preset resource is unavailable.");
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(resource.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error) *error = QStringLiteral("Built-in preset resource is invalid.");
        return {};
    }
    QVector<PresetCatalogEntry> result;
    QSet<QString> ids;
    for (const QJsonValue& value : document.array()) {
        const QJsonObject json = value.toObject();
        PresetCatalogEntry entry;
        entry.builtIn = true;
        entry.preset.id = json.value(QStringLiteral("id")).toString();
        entry.preset.name = json.value(QStringLiteral("name")).toString();
        entry.category = json.value(QStringLiteral("category")).toString();
        entry.description = json.value(QStringLiteral("description")).toString();
        for (const QJsonValue& tag : json.value(QStringLiteral("tags")).toArray()) entry.tags.push_back(tag.toString());
        if (entry.preset.id.isEmpty() || entry.preset.name.isEmpty() || ids.contains(entry.preset.id)) {
            if (error) *error = QStringLiteral("Built-in presets contain a missing or duplicate ID.");
            return {};
        }
        ids.insert(entry.preset.id);
        for (const QJsonValue& effectValue : json.value(QStringLiteral("effects")).toArray()) {
            const QJsonObject effectJson = effectValue.toObject();
            std::unique_ptr<Effect> effect = EffectRegistry::instance().create(effectJson.value(QStringLiteral("type")).toString());
            if (!effect) {
                if (error) *error = QStringLiteral("Built-in preset '%1' references an unknown effect.").arg(entry.preset.id);
                return {};
            }
            effect->masterStrength = qBound(0.0, effectJson.value(QStringLiteral("masterStrength")).toDouble(1.0), 3.0);
            QString parameterError;
            if (!effect->parametersFromJson(effectJson.value(QStringLiteral("parameters")).toObject(), &parameterError)) {
                if (error) *error = QStringLiteral("Built-in preset '%1': %2").arg(entry.preset.id, parameterError);
                return {};
            }
            entry.preset.effects.append(std::move(effect));
        }
        result.push_back(std::move(entry));
    }
    return result;
}

QVector<PresetCatalogEntry> PresetCatalog::entries(QString* diagnostics) const
{
    QVector<PresetCatalogEntry> result = builtinEntries(diagnostics);
    if (diagnostics && !diagnostics->isEmpty()) return {};
    QString userDiagnostics;
    for (const PresetInfo& info : m_userPresets.listPresets(&userDiagnostics)) {
        Preset preset;
        QString loadError;
        if (!m_userPresets.loadPresetById(info.id, &preset, &loadError)) {
            userDiagnostics += (userDiagnostics.isEmpty() ? QString() : QStringLiteral("\n")) + loadError;
            continue;
        }
        result.push_back({std::move(preset), QStringLiteral("My Presets"), QStringLiteral("User preset"), {}, false});
    }
    if (diagnostics && !userDiagnostics.isEmpty()) *diagnostics = userDiagnostics;
    return result;
}

bool PresetCatalog::presetById(const QString& id, PresetCatalogEntry* entry, QString* error) const
{
    QString diagnostics;
    const QVector<PresetCatalogEntry> all = entries(&diagnostics);
    for (const PresetCatalogEntry& candidate : all) {
        if (candidate.preset.id == id) {
            if (entry) *entry = candidate;
            return true;
        }
    }
    if (error) *error = diagnostics.isEmpty() ? QStringLiteral("Preset '%1' does not exist.").arg(id) : diagnostics;
    return false;
}

bool PresetCatalog::duplicateBuiltIn(const QString& id, Preset* copy, QString* error) const
{
    PresetCatalogEntry entry;
    if (!presetById(id, &entry, error)) return false;
    if (!entry.builtIn) {
        if (error) *error = QStringLiteral("Only built-in presets can be duplicated to My Presets.");
        return false;
    }
    entry.preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (copy) *copy = std::move(entry.preset);
    return true;
}

} // namespace vt
