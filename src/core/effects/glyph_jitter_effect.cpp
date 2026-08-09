#include "core/effects/glyph_jitter_effect.h"

#include <QtGlobal>

#include <cmath>

namespace vt {

namespace {

quint64 mix64(quint64 value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

qreal unitValue(quint32 seed, int glyphIndex, quint64 salt)
{
    const quint64 input = (static_cast<quint64>(seed) << 32U)
        ^ static_cast<quint32>(glyphIndex)
        ^ salt;
    const quint64 value = mix64(input);
    return static_cast<qreal>((value >> 11U) * (1.0 / 9007199254740992.0));
}

} // namespace

QString GlyphJitterEffect::typeId() const
{
    return QStringLiteral("glyphJitter");
}

QString GlyphJitterEffect::displayName() const
{
    return QStringLiteral("Glyph Jitter");
}

EffectDomain GlyphJitterEffect::domain() const
{
    return EffectDomain::GlyphTransform;
}

std::unique_ptr<Effect> GlyphJitterEffect::clone() const
{
    return std::make_unique<GlyphJitterEffect>(*this);
}

QVector<EffectParameter> GlyphJitterEffect::parameterDefinitions() const
{
    return {
        {QStringLiteral("amount"), QStringLiteral("Amount"), amount, 0.0, 0.5, 0.005, false},
        {QStringLiteral("seed"), QStringLiteral("Seed"), static_cast<double>(seed), 0.0, 4294967295.0, 1.0, true},
    };
}

bool GlyphJitterEffect::setParameter(const QString& id, double value)
{
    if (id == QStringLiteral("amount")) {
        amount = qBound<qreal>(0.0, value, 0.5);
        return true;
    }
    if (id == QStringLiteral("seed")) {
        seed = static_cast<quint32>(qBound<double>(0.0, value, 4294967295.0));
        return true;
    }
    return false;
}

QJsonObject GlyphJitterEffect::parametersToJson() const
{
    return {
        {QStringLiteral("amount"), amount},
        {QStringLiteral("seed"), static_cast<double>(seed)},
    };
}

bool GlyphJitterEffect::parametersFromJson(const QJsonObject& object, QString* error)
{
    const bool amountSet = setParameter(
        QStringLiteral("amount"), object.value(QStringLiteral("amount")).toDouble(amount));
    const bool seedSet = setParameter(
        QStringLiteral("seed"), object.value(QStringLiteral("seed")).toDouble(seed));
    if (!amountSet || !seedSet) {
        if (error) {
            *error = QStringLiteral("Glyph jitter effect contains an unknown parameter.");
        }
        return false;
    }
    return true;
}

void GlyphJitterEffect::apply(VectorGeometry& geometry, const EffectContext& context) const
{
    const qreal rotationRangeDegrees = amount * 12.0;
    for (int index = 0; index < geometry.pieces.size(); ++index) {
        GeometryPiece& piece = geometry.pieces[index];
        const qreal xOffset = (unitValue(seed, piece.sourceGlyphIndex, 0x1234ULL) * 2.0 - 1.0)
            * amount * masterStrength * context.referenceHeight;
        const qreal yOffset = (unitValue(seed, piece.sourceGlyphIndex, 0x5678ULL) * 2.0 - 1.0)
            * amount * masterStrength * context.referenceHeight;
        const qreal angle = (unitValue(seed, piece.sourceGlyphIndex, 0x9abcULL) * 2.0 - 1.0)
            * rotationRangeDegrees * masterStrength;

        const QPointF center = piece.path.isEmpty()
            ? piece.anchor
            : piece.path.boundingRect().center();
        QTransform transform;
        Q_UNUSED(transform.translate(center.x() + xOffset, center.y() + yOffset));
        Q_UNUSED(transform.rotate(angle));
        Q_UNUSED(transform.translate(-center.x(), -center.y()));
        geometry.transformPiece(index, transform);
    }
    geometry.recomputeBounds();
}

} // namespace vt
