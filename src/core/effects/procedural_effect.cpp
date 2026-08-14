#include "core/effects/procedural_effect.h"

#include <QtGlobal>

#include <cmath>
#include <utility>

namespace vt {

namespace {

quint64 mix(quint64 value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

qreal randomUnit(quint32 seed, int index, quint64 salt)
{
    const quint64 input = (static_cast<quint64>(seed) << 32U)
        ^ static_cast<quint32>(index)
        ^ salt;
    return static_cast<qreal>((mix(input) >> 11U) * (1.0 / 9007199254740992.0));
}

bool modeUsesProgress(ProceduralEffect::Mode mode)
{
    switch (mode) {
    case ProceduralEffect::Mode::Bounce:
    case ProceduralEffect::Mode::Staircase:
    case ProceduralEffect::Mode::HorizontalSpread:
    case ProceduralEffect::Mode::VerticalSpread:
    case ProceduralEffect::Mode::Arc:
    case ProceduralEffect::Mode::Zigzag:
    case ProceduralEffect::Mode::SineRotation:
    case ProceduralEffect::Mode::Crescendo:
    case ProceduralEffect::Mode::Shrink:
    case ProceduralEffect::Mode::ExpandCenter:
    case ProceduralEffect::Mode::SqueezeCenter:
    case ProceduralEffect::Mode::BaselineDrift:
        return true;
    case ProceduralEffect::Mode::RandomOffset:
    case ProceduralEffect::Mode::RandomRotation:
    case ProceduralEffect::Mode::RandomScale:
    case ProceduralEffect::Mode::Skew:
    case ProceduralEffect::Mode::Compression:
    case ProceduralEffect::Mode::AlternatingTilt:
        return false;
    }
    return false;
}

qreal normalizedProgress(const GeometryPiece& piece,
                         const QRectF& referenceBounds,
                         qreal width)
{
    if (piece.hasEffectReferenceProgress
        && std::isfinite(piece.effectReferenceProgress)) {
        return qBound<qreal>(0.0, piece.effectReferenceProgress, 1.0);
    }

    const QPointF effectAnchor = piece.hasEffectReferenceAnchor
        ? piece.effectReferenceAnchor : piece.originalAnchor;
    return qBound<qreal>(
        0.0,
        (effectAnchor.x() - referenceBounds.left()) / width,
        1.0);
}

} // namespace

ProceduralEffect::ProceduralEffect(QString typeId, QString displayName, Mode mode)
    : m_typeId(std::move(typeId))
    , m_displayName(std::move(displayName))
    , m_mode(mode)
{
}

QString ProceduralEffect::typeId() const
{
    return m_typeId;
}

QString ProceduralEffect::displayName() const
{
    return m_displayName;
}

EffectDomain ProceduralEffect::domain() const
{
    return EffectDomain::GlyphTransform;
}

std::unique_ptr<Effect> ProceduralEffect::clone() const
{
    return std::make_unique<ProceduralEffect>(*this);
}

QVector<EffectParameter> ProceduralEffect::parameterDefinitions() const
{
    return {
        {QStringLiteral("strength"), QStringLiteral("Strength"), m_strength, -1.0, 1.0, 0.01, false},
        {QStringLiteral("frequency"), QStringLiteral("Frequency"), m_frequency, 0.1, 12.0, 0.1, false},
        {QStringLiteral("seed"), QStringLiteral("Seed"), static_cast<double>(m_seed), 0.0, 4294967295.0, 1.0, true},
    };
}

bool ProceduralEffect::setParameter(const QString& id, double value)
{
    if (id == QStringLiteral("strength")) {
        m_strength = qBound<qreal>(-1.0, value, 1.0);
        return true;
    }
    if (id == QStringLiteral("frequency")) {
        m_frequency = qBound<qreal>(0.1, value, 12.0);
        return true;
    }
    if (id == QStringLiteral("seed")) {
        m_seed = static_cast<quint32>(qBound<double>(0.0, value, 4294967295.0));
        return true;
    }
    return false;
}

QJsonObject ProceduralEffect::parametersToJson() const
{
    return {{QStringLiteral("strength"), m_strength},
            {QStringLiteral("frequency"), m_frequency},
            {QStringLiteral("seed"), static_cast<double>(m_seed)}};
}

bool ProceduralEffect::parametersFromJson(const QJsonObject& object, QString* error)
{
    const bool strengthSet = setParameter(
        QStringLiteral("strength"), object.value(QStringLiteral("strength")).toDouble(m_strength));
    const bool frequencySet = setParameter(
        QStringLiteral("frequency"), object.value(QStringLiteral("frequency")).toDouble(m_frequency));
    const bool seedSet = setParameter(
        QStringLiteral("seed"), object.value(QStringLiteral("seed")).toDouble(m_seed));
    if (!strengthSet || !frequencySet || !seedSet) {
        if (error) {
            *error = QStringLiteral("Procedural effect contains an unknown parameter.");
        }
        return false;
    }
    return true;
}

void ProceduralEffect::apply(VectorGeometry& geometry, const EffectContext& context) const
{
    if (geometry.pieces.isEmpty()) {
        return;
    }
    const qreal width = qMax<qreal>(1.0, context.referenceBounds.width());
    const qreal height = qMax<qreal>(1.0, context.referenceHeight);
    const bool usesProgress = modeUsesProgress(m_mode);
    constexpr qreal pi = 3.14159265358979323846;
    constexpr qreal twoPi = 2.0 * pi;

    for (int index = 0; index < geometry.pieces.size(); ++index) {
        if (!context.work.consume(qMax(1, geometry.pieces.at(index).path.elementCount()))) {
            break;
        }
        GeometryPiece& piece = geometry.pieces[index];
        const int glyphIndex = piece.sourceGlyphIndex >= 0 ? piece.sourceGlyphIndex : index;
        const qreal progress = usesProgress
            ? normalizedProgress(piece, context.referenceBounds, width) : 0.0;
        const qreal phase = twoPi * (progress * m_frequency);
        const qreal signedRandom = randomUnit(m_seed, glyphIndex, 0x7134ULL) * 2.0 - 1.0;
        const qreal positiveRandom = randomUnit(m_seed, glyphIndex, 0x9411ULL);
        const qreal amount = m_strength * context.effectiveStrength(*this);
        QPointF translation;
        qreal rotation = 0.0;
        qreal scaleX = 1.0;
        qreal scaleY = 1.0;
        qreal shear = 0.0;

        switch (m_mode) {
        case Mode::Bounce:
            translation.setY(std::abs(std::sin(phase)) * amount * height * 0.24);
            break;
        case Mode::Staircase:
            translation.setY(std::floor(progress * m_frequency * 6.0) * amount * height * 0.04);
            break;
        case Mode::RandomOffset:
            translation = QPointF(signedRandom * amount * height * 0.18,
                                  (randomUnit(m_seed, glyphIndex, 0x8112ULL) * 2.0 - 1.0)
                                      * amount * height * 0.18);
            break;
        case Mode::RandomRotation:
            rotation = signedRandom * amount * 24.0;
            break;
        case Mode::RandomScale:
            scaleX = qMax<qreal>(0.1, 1.0 + signedRandom * amount * 0.25);
            scaleY = qMax<qreal>(0.1, 1.0 + (positiveRandom * 2.0 - 1.0) * amount * 0.25);
            break;
        case Mode::HorizontalSpread:
            translation.setX((progress - 0.5) * amount * width * 0.22);
            break;
        case Mode::VerticalSpread:
            translation.setY((progress - 0.5) * amount * height * 0.28);
            break;
        case Mode::Arc:
            translation.setY(std::sin(pi * progress) * amount * height * 0.3);
            break;
        case Mode::Zigzag:
            translation.setY((std::fmod(progress * m_frequency * 2.0, 2.0) - 1.0)
                             * amount * height * 0.18);
            break;
        case Mode::SineRotation:
            rotation = std::sin(phase) * amount * 18.0;
            break;
        case Mode::Crescendo:
            scaleX = scaleY = qMax<qreal>(0.1, 1.0 + progress * amount * 0.45);
            break;
        case Mode::Shrink:
            scaleX = scaleY = qMax<qreal>(0.1, 1.0 - progress * amount * 0.45);
            break;
        case Mode::Skew:
            shear = amount * 0.35;
            break;
        case Mode::Compression:
            scaleX = qMax<qreal>(0.1, 1.0 - amount * 0.35);
            scaleY = qMax<qreal>(0.1, 1.0 + amount * 0.12);
            break;
        case Mode::ExpandCenter: {
            const qreal distance = (progress - 0.5) * 2.0;
            translation.setX(distance * amount * width * 0.16);
            break;
        }
        case Mode::SqueezeCenter: {
            const qreal distance = (progress - 0.5) * 2.0;
            translation.setX(-distance * amount * width * 0.16);
            break;
        }
        case Mode::BaselineDrift:
            translation.setY(progress * amount * height * 0.22);
            break;
        case Mode::AlternatingTilt:
            rotation = (glyphIndex % 2 == 0 ? 1.0 : -1.0) * amount * 14.0;
            break;
        }

        const QPointF center = piece.path.isEmpty() ? piece.anchor : piece.path.boundingRect().center();
        QTransform transform;
        Q_UNUSED(transform.translate(center.x() + translation.x(), center.y() + translation.y()));
        Q_UNUSED(transform.rotate(rotation));
        Q_UNUSED(transform.shear(shear, 0.0));
        Q_UNUSED(transform.scale(scaleX, scaleY));
        Q_UNUSED(transform.translate(-center.x(), -center.y()));
        geometry.transformPiece(index, transform);
    }
    geometry.recomputeBounds();
}

} // namespace vt
