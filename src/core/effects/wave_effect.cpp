#include "core/effects/wave_effect.h"

#include <QtGlobal>

#include <cmath>

namespace vt {

QString WaveEffect::typeId() const
{
    return QStringLiteral("wave");
}

QString WaveEffect::displayName() const
{
    return QStringLiteral("Wave");
}

EffectDomain WaveEffect::domain() const
{
    return EffectDomain::GlyphTransform;
}

std::unique_ptr<Effect> WaveEffect::clone() const
{
    return std::make_unique<WaveEffect>(*this);
}

QVector<EffectParameter> WaveEffect::parameterDefinitions() const
{
    return {
        {QStringLiteral("amplitude"), QStringLiteral("Amplitude"), amplitude, 0.0, 1.0, 0.01, false},
        {QStringLiteral("frequency"), QStringLiteral("Frequency"), frequency, 0.0, 20.0, 0.1, false},
        {QStringLiteral("phase"), QStringLiteral("Phase (cycles)"), phase, -1.0, 1.0, 0.01, false},
    };
}

bool WaveEffect::setParameter(const QString& id, double value)
{
    if (id == QStringLiteral("amplitude")) {
        amplitude = qBound<qreal>(0.0, value, 1.0);
        return true;
    }
    if (id == QStringLiteral("frequency")) {
        frequency = qBound<qreal>(0.0, value, 20.0);
        return true;
    }
    if (id == QStringLiteral("phase")) {
        phase = qBound<qreal>(-1.0, value, 1.0);
        return true;
    }
    return false;
}

QJsonObject WaveEffect::parametersToJson() const
{
    return {
        {QStringLiteral("amplitude"), amplitude},
        {QStringLiteral("frequency"), frequency},
        {QStringLiteral("phase"), phase},
    };
}

bool WaveEffect::parametersFromJson(const QJsonObject& object, QString* error)
{
    Q_UNUSED(error);
    setParameter(QStringLiteral("amplitude"), object.value(QStringLiteral("amplitude")).toDouble(amplitude));
    setParameter(QStringLiteral("frequency"), object.value(QStringLiteral("frequency")).toDouble(frequency));
    setParameter(QStringLiteral("phase"), object.value(QStringLiteral("phase")).toDouble(phase));
    return true;
}

void WaveEffect::apply(VectorGeometry& geometry, const EffectContext& context) const
{
    const qreal width = qMax<qreal>(1.0, context.referenceBounds.width());
    constexpr qreal twoPi = 6.28318530717958647692;

    for (int index = 0; index < geometry.pieces.size(); ++index) {
        const GeometryPiece& piece = geometry.pieces[index];
        const qreal progress = qBound<qreal>(
            0.0,
            (piece.originalAnchor.x() - context.referenceBounds.left()) / width,
            1.0);
        const qreal displacement = amplitude * context.referenceHeight
            * std::sin(twoPi * (frequency * progress + phase));
        geometry.translatePiece(index, QPointF(0.0, displacement));
    }
    geometry.recomputeBounds();
}

} // namespace vt
