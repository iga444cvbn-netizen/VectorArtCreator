#include "core/effects/effect.h"

#include "core/effects/effect_registry.h"

#include <QJsonArray>

namespace vt {

bool EffectScope::includes(int clusterStart, int clusterLength) const
{
    if (kind == EffectScopeKind::WholeObject) {
        return true;
    }
    // Decorations deliberately have no text cluster.  A character-range
    // effect must not transform an entire underline or strikeout line.
    if (clusterStart < 0) {
        return false;
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
    return EffectRegistry::instance().create(typeId);
}

QVector<QPair<QString, QString>> availableEffectTypes()
{
    QVector<QPair<QString, QString>> types;
    for (const EffectDescriptor& descriptor : EffectRegistry::instance().descriptors())
        types.push_back({descriptor.typeId, descriptor.displayName});
    return types;
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
