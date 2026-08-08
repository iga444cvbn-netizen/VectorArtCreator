#include "core/effects/stretch_effect.h"

#include <QtGlobal>

namespace vt {

QString StretchEffect::typeId() const
{
    return QStringLiteral("stretch");
}

QString StretchEffect::displayName() const
{
    return QStringLiteral("Global Stretch");
}

EffectDomain StretchEffect::domain() const
{
    return EffectDomain::Geometry;
}

std::unique_ptr<Effect> StretchEffect::clone() const
{
    return std::make_unique<StretchEffect>(*this);
}

QVector<EffectParameter> StretchEffect::parameterDefinitions() const
{
    return {
        {QStringLiteral("horizontal"), QStringLiteral("Horizontal"), horizontal, 0.1, 4.0, 0.01, false},
        {QStringLiteral("vertical"), QStringLiteral("Vertical"), vertical, 0.1, 4.0, 0.01, false},
    };
}

bool StretchEffect::setParameter(const QString& id, double value)
{
    if (id == QStringLiteral("horizontal")) {
        horizontal = qBound<qreal>(0.1, value, 4.0);
        return true;
    }
    if (id == QStringLiteral("vertical")) {
        vertical = qBound<qreal>(0.1, value, 4.0);
        return true;
    }
    return false;
}

QJsonObject StretchEffect::parametersToJson() const
{
    return {
        {QStringLiteral("horizontal"), horizontal},
        {QStringLiteral("vertical"), vertical},
    };
}

bool StretchEffect::parametersFromJson(const QJsonObject& object, QString* error)
{
    Q_UNUSED(error);
    setParameter(QStringLiteral("horizontal"), object.value(QStringLiteral("horizontal")).toDouble(horizontal));
    setParameter(QStringLiteral("vertical"), object.value(QStringLiteral("vertical")).toDouble(vertical));
    return true;
}

void StretchEffect::apply(VectorGeometry& geometry, const EffectContext& context) const
{
    Q_UNUSED(context);
    if (geometry.pieces.isEmpty()) {
        return;
    }

    geometry.recomputeBounds();
    const QPointF center = geometry.bounds.center();
    QTransform transform;
    transform.translate(center.x(), center.y());
    transform.scale(horizontal, vertical);
    transform.translate(-center.x(), -center.y());
    geometry.transformAll(transform);
}

} // namespace vt
