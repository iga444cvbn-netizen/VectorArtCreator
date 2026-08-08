#include "core/effects/effect.h"

#include "core/effects/glyph_jitter_effect.h"
#include "core/effects/stretch_effect.h"
#include "core/effects/wave_effect.h"

#include <QJsonArray>

namespace vt {

std::unique_ptr<Effect> createEffect(const QString& typeId)
{
    if (typeId == QStringLiteral("wave")) {
        return std::make_unique<WaveEffect>();
    }
    if (typeId == QStringLiteral("glyphJitter")) {
        return std::make_unique<GlyphJitterEffect>();
    }
    if (typeId == QStringLiteral("stretch")) {
        return std::make_unique<StretchEffect>();
    }
    return nullptr;
}

std::unique_ptr<Effect> effectFromJson(const QJsonObject& object, QString* error)
{
    const QString typeId = object.value(QStringLiteral("type")).toString();
    if (typeId.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Effect entry is missing its type ID.");
        }
        return nullptr;
    }

    std::unique_ptr<Effect> effect = createEffect(typeId);
    if (!effect) {
        if (error) {
            *error = QStringLiteral("Unknown effect type '%1'.").arg(typeId);
        }
        return nullptr;
    }

    effect->enabled = object.value(QStringLiteral("enabled")).toBool(true);
    const QJsonObject parameters = object.value(QStringLiteral("parameters")).toObject();
    if (!effect->parametersFromJson(parameters, error)) {
        return nullptr;
    }
    return effect;
}

} // namespace vt
