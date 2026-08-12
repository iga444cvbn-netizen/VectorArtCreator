#include "core/effects/trail_effect.h"

#include <cmath>
#include <utility>

namespace vt {

TrailEffect::TrailEffect(QString id, QString name)
    : m_id(std::move(id)), m_name(std::move(name))
{
}

QString TrailEffect::typeId() const { return m_id; }
QString TrailEffect::displayName() const { return m_name; }
EffectDomain TrailEffect::domain() const { return EffectDomain::Generator; }
std::unique_ptr<Effect> TrailEffect::clone() const
{
    return std::make_unique<TrailEffect>(*this);
}

QVector<EffectParameter> TrailEffect::parameterDefinitions() const
{
    return {{QStringLiteral("copyCount"), QStringLiteral("Copy count"),
             double(m_count), 1, 12, 1, true},
            {QStringLiteral("offsetX"), QStringLiteral("Offset X"),
             m_offsetX, -1, 1, .01, false},
            {QStringLiteral("offsetY"), QStringLiteral("Offset Y"),
             m_offsetY, -1, 1, .01, false},
            {QStringLiteral("rotationStep"), QStringLiteral("Rotation"),
             m_rotation, -45, 45, 1, false},
            {QStringLiteral("scaleStep"), QStringLiteral("Scale"),
             m_scale, .1, 1.5, .01, false},
            {QStringLiteral("opacityDecay"), QStringLiteral("Opacity decay"),
             m_decay, .1, 1, .01, false}};
}

bool TrailEffect::setParameter(const QString& id, double value)
{
    if (id == QStringLiteral("copyCount")) m_count = qBound(1, qRound(value), 12);
    else if (id == QStringLiteral("offsetX")) m_offsetX = qBound<qreal>(-1, value, 1);
    else if (id == QStringLiteral("offsetY")) m_offsetY = qBound<qreal>(-1, value, 1);
    else if (id == QStringLiteral("rotationStep")) m_rotation = qBound<qreal>(-45, value, 45);
    else if (id == QStringLiteral("scaleStep")) m_scale = qBound<qreal>(.1, value, 1.5);
    else if (id == QStringLiteral("opacityDecay")) m_decay = qBound<qreal>(.1, value, 1);
    else return false;
    return true;
}

QJsonObject TrailEffect::parametersToJson() const
{
    return {{QStringLiteral("copyCount"), m_count},
            {QStringLiteral("offsetX"), m_offsetX},
            {QStringLiteral("offsetY"), m_offsetY},
            {QStringLiteral("rotationStep"), m_rotation},
            {QStringLiteral("scaleStep"), m_scale},
            {QStringLiteral("opacityDecay"), m_decay}};
}

bool TrailEffect::parametersFromJson(const QJsonObject& object, QString* error)
{
    for (const EffectParameter& parameter : parameterDefinitions()) {
        if (!setParameter(parameter.id,
                          object.value(parameter.id).toDouble(parameter.value))) {
            if (error) *error = QStringLiteral("Invalid trail parameter.");
            return false;
        }
    }
    return true;
}

void TrailEffect::apply(VectorGeometry& geometry, const EffectContext& context) const
{
    const qreal strength = context.effectiveStrength(*this);
    if (qFuzzyIsNull(strength)) return;

    qint64 copyUnits = 1;
    for (const GeometryPiece& piece : geometry.pieces) {
        copyUnits += qMax(1, piece.path.elementCount());
    }
    if (!context.work.consume(copyUnits)) return;
    const QVector<GeometryPiece> originals = geometry.pieces;
    constexpr int pieceCap = 4096;
    for (const GeometryPiece& base : originals) {
        if (!context.work.consume()) break;
        if (!scope.includes(base.sourceClusterStart, base.sourceClusterLength)
            || base.generationDepth >= 2) {
            continue;
        }
        for (int copyIndex = 1;
             copyIndex <= m_count && geometry.pieces.size() < pieceCap;
             ++copyIndex) {
            if (!context.work.consume(qMax(1, base.path.elementCount()))) break;
            GeometryPiece copy = base;
            copy.generationDepth = base.generationDepth + 1;
            copy.generatorEffectId = instanceId;
            copy.opacityMultiplier = qBound<qreal>(
                0, base.opacityMultiplier * std::pow(m_decay, copyIndex) * strength, 1);
            const QPointF center = copy.path.boundingRect().center();
            QTransform transform;
            transform.translate(
                center.x() + copyIndex * m_offsetX * context.referenceHeight * strength,
                center.y() + copyIndex * m_offsetY * context.referenceHeight * strength);
            transform.rotate(copyIndex * m_rotation * strength);
            transform.scale(std::pow(m_scale, copyIndex * strength),
                            std::pow(m_scale, copyIndex * strength));
            transform.translate(-center.x(), -center.y());
            copy.path = transform.map(copy.path);
            copy.anchor = transform.map(copy.anchor);
            geometry.pieces.push_back(std::move(copy));
        }
        if (!context.work.isRunning()) break;
    }
    geometry.recomputeBounds();
}

} // namespace vt
