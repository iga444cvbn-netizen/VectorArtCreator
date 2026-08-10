#include "core/effects/stretch_effect.h"

#include <QtGlobal>
#include <cmath>

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
    const bool horizontalSet = setParameter(
        QStringLiteral("horizontal"), object.value(QStringLiteral("horizontal")).toDouble(horizontal));
    const bool verticalSet = setParameter(
        QStringLiteral("vertical"), object.value(QStringLiteral("vertical")).toDouble(vertical));
    if (!horizontalSet || !verticalSet) {
        if (error) {
            *error = QStringLiteral("Stretch effect contains an unknown parameter.");
        }
        return false;
    }
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
    // Preserve the parameter's meaning at strength 1 while using exponential
    // amplification.  This is strictly positive for every supported strength.
    const auto amplified = [this](qreal parameter) {
        return std::exp(std::log(qMax<qreal>(0.0001, parameter)) * masterStrength);
    };
    QTransform transform;
    Q_UNUSED(transform.translate(center.x(), center.y()));
    Q_UNUSED(transform.scale(amplified(horizontal), amplified(vertical)));
    Q_UNUSED(transform.translate(-center.x(), -center.y()));
    geometry.transformAll(transform);
}

} // namespace vt
