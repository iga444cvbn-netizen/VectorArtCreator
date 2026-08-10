#include "core/effects/effect.h"

#include "core/effects/glyph_jitter_effect.h"
#include "core/effects/procedural_effect.h"
#include "core/effects/stretch_effect.h"
#include "core/effects/wave_effect.h"

#include <QJsonArray>

namespace vt {

bool EffectScope::includes(int clusterStart, int clusterLength) const
{
    if (kind == EffectScopeKind::WholeObject || clusterStart < 0) {
        return true;
    }
    const int clusterEnd = clusterStart + qMax(1, clusterLength);
    return clusterStart < end && clusterEnd > start;
}

QJsonObject EffectScope::toJson() const
{
    return {
        {QStringLiteral("kind"), kind == EffectScopeKind::TextRange
                                      ? QStringLiteral("textRange")
                                      : QStringLiteral("wholeObject")},
        {QStringLiteral("start"), start},
        {QStringLiteral("end"), end},
    };
}

QJsonObject EffectMaskStroke::toJson() const
{
    QJsonArray serializedPoints;
    for (const QPointF& point : points) {
        serializedPoints.append(QJsonObject{{QStringLiteral("x"), point.x()},
                                            {QStringLiteral("y"), point.y()}});
    }
    return {{QStringLiteral("points"), serializedPoints},
            {QStringLiteral("radius"), radius},
            {QStringLiteral("opacity"), opacity},
            {QStringLiteral("hardness"), hardness},
            {QStringLiteral("restore"), restore}};
}

EffectMaskStroke EffectMaskStroke::fromJson(const QJsonObject& object)
{
    EffectMaskStroke stroke;
    const QJsonArray serializedPoints = object.value(QStringLiteral("points")).toArray();
    for (const QJsonValue& value : serializedPoints) {
        const QJsonObject point = value.toObject();
        stroke.points.push_back(QPointF(point.value(QStringLiteral("x")).toDouble(),
                                        point.value(QStringLiteral("y")).toDouble()));
    }
    stroke.radius = qMax<qreal>(0.1, object.value(QStringLiteral("radius")).toDouble(stroke.radius));
    stroke.opacity = qBound<qreal>(0.0, object.value(QStringLiteral("opacity")).toDouble(stroke.opacity), 1.0);
    stroke.hardness = qBound<qreal>(0.0,
                                    object.value(QStringLiteral("hardness")).toDouble(stroke.hardness),
                                    1.0);
    stroke.restore = object.value(QStringLiteral("restore")).toBool(false);
    return stroke;
}

EffectScope EffectScope::fromJson(const QJsonObject& object)
{
    EffectScope scope;
    scope.kind = object.value(QStringLiteral("kind")).toString()
                     == QStringLiteral("textRange")
        ? EffectScopeKind::TextRange
        : EffectScopeKind::WholeObject;
    scope.start = qMax(0, object.value(QStringLiteral("start")).toInt());
    scope.end = qMax(scope.start, object.value(QStringLiteral("end")).toInt());
    return scope;
}

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
    struct Definition {
        const char* id;
        const char* name;
        ProceduralEffect::Mode mode;
    };
    static constexpr Definition definitions[] = {
        {"bounce", "Bounce", ProceduralEffect::Mode::Bounce},
        {"staircase", "Staircase", ProceduralEffect::Mode::Staircase},
        {"randomOffset", "Random Offset", ProceduralEffect::Mode::RandomOffset},
        {"randomRotation", "Random Rotation", ProceduralEffect::Mode::RandomRotation},
        {"randomScale", "Random Scale", ProceduralEffect::Mode::RandomScale},
        {"horizontalSpread", "Horizontal Spread", ProceduralEffect::Mode::HorizontalSpread},
        {"verticalSpread", "Vertical Spread", ProceduralEffect::Mode::VerticalSpread},
        {"arc", "Arc", ProceduralEffect::Mode::Arc},
        {"zigzag", "Zigzag", ProceduralEffect::Mode::Zigzag},
        {"sineRotation", "Sine Rotation", ProceduralEffect::Mode::SineRotation},
        {"crescendo", "Crescendo", ProceduralEffect::Mode::Crescendo},
        {"shrink", "Shrink", ProceduralEffect::Mode::Shrink},
        {"skew", "Skew", ProceduralEffect::Mode::Skew},
        {"compression", "Compression", ProceduralEffect::Mode::Compression},
        {"expandCenter", "Expand from Center", ProceduralEffect::Mode::ExpandCenter},
        {"squeezeCenter", "Squeeze to Center", ProceduralEffect::Mode::SqueezeCenter},
        {"baselineDrift", "Baseline Drift", ProceduralEffect::Mode::BaselineDrift},
        {"alternatingTilt", "Alternating Tilt", ProceduralEffect::Mode::AlternatingTilt},
    };
    for (const Definition& definition : definitions) {
        if (typeId == QLatin1String(definition.id)) {
            return std::make_unique<ProceduralEffect>(QString::fromLatin1(definition.id),
                                                       QString::fromLatin1(definition.name),
                                                       definition.mode);
        }
    }
    return nullptr;
}

QVector<QPair<QString, QString>> availableEffectTypes()
{
    return {
        {QStringLiteral("wave"), QStringLiteral("Wave")},
        {QStringLiteral("glyphJitter"), QStringLiteral("Glyph Jitter")},
        {QStringLiteral("stretch"), QStringLiteral("Global Stretch")},
        {QStringLiteral("bounce"), QStringLiteral("Bounce")},
        {QStringLiteral("staircase"), QStringLiteral("Staircase")},
        {QStringLiteral("randomOffset"), QStringLiteral("Random Offset")},
        {QStringLiteral("randomRotation"), QStringLiteral("Random Rotation")},
        {QStringLiteral("randomScale"), QStringLiteral("Random Scale")},
        {QStringLiteral("horizontalSpread"), QStringLiteral("Horizontal Spread")},
        {QStringLiteral("verticalSpread"), QStringLiteral("Vertical Spread")},
        {QStringLiteral("arc"), QStringLiteral("Arc")},
        {QStringLiteral("zigzag"), QStringLiteral("Zigzag")},
        {QStringLiteral("sineRotation"), QStringLiteral("Sine Rotation")},
        {QStringLiteral("crescendo"), QStringLiteral("Crescendo")},
        {QStringLiteral("shrink"), QStringLiteral("Shrink")},
        {QStringLiteral("skew"), QStringLiteral("Skew")},
        {QStringLiteral("compression"), QStringLiteral("Compression")},
        {QStringLiteral("expandCenter"), QStringLiteral("Expand from Center")},
        {QStringLiteral("squeezeCenter"), QStringLiteral("Squeeze to Center")},
        {QStringLiteral("baselineDrift"), QStringLiteral("Baseline Drift")},
        {QStringLiteral("alternatingTilt"), QStringLiteral("Alternating Tilt")},
    };
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

    const QString instanceId = object.value(QStringLiteral("id")).toString();
    if (!instanceId.isEmpty()) {
        effect->instanceId = instanceId;
    }
    if (object.value(QStringLiteral("scope")).isObject()) {
        effect->scope = EffectScope::fromJson(object.value(QStringLiteral("scope")).toObject());
    }
    effect->enabled = object.value(QStringLiteral("enabled")).toBool(true);
    effect->masterStrength = qBound<qreal>(0.0,
                                           object.value(QStringLiteral("masterStrength"))
                                               .toDouble(effect->masterStrength),
                                           3.0);
    const QJsonArray mask = object.value(QStringLiteral("mask")).toArray();
    for (const QJsonValue& value : mask) {
        if (value.isObject()) {
            effect->maskStrokes.push_back(EffectMaskStroke::fromJson(value.toObject()));
        }
    }
    effect->maskInverted = object.value(QStringLiteral("maskInverted")).toBool(false);
    const QJsonObject parameters = object.value(QStringLiteral("parameters")).toObject();
    if (!effect->parametersFromJson(parameters, error)) {
        return nullptr;
    }
    return effect;
}

} // namespace vt
