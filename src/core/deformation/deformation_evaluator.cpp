#include "core/deformation/deformation_evaluator.h"

#include "core/deformation/contour_sampler.h"

#include <QLineF>
#include <QTransform>
#include <QtGlobal>

#include <cmath>

namespace vt {

namespace {

constexpr qreal MaximumGlobalStrength = 4.0;

bool finitePoint(const QPointF& point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

qreal distance(const QPointF& left, const QPointF& right)
{
    return QLineF(left, right).length();
}

QPointF normalized(const QPointF& value)
{
    const qreal length = QLineF(QPointF(), value).length();
    if (length <= 1.0e-8) {
        return {};
    }
    return value / length;
}

qreal brushInfluence(qreal distanceFromCenter, qreal radius, qreal hardness)
{
    if (!std::isfinite(distanceFromCenter) || radius <= 0.0 || distanceFromCenter >= radius) {
        return 0.0;
    }
    const qreal t = qBound<qreal>(0.0, 1.0 - distanceFromCenter / radius, 1.0);
    const qreal smooth = t * t * (3.0 - 2.0 * t);
    const qreal hardCurve = std::pow(t, 0.5);
    const qreal boundedHardness = qBound<qreal>(0.0, hardness, 1.0);
    return qBound<qreal>(0.0, (1.0 - boundedHardness) * smooth + boundedHardness * hardCurve, 1.0);
}

QPointF boundedMovement(const QPointF& movement, qreal radius)
{
    if (!finitePoint(movement)) {
        return {};
    }
    const qreal length = QLineF(QPointF(), movement).length();
    const qreal maximum = qMax<qreal>(0.01, radius * 0.75);
    if (length <= maximum || length <= 1.0e-8) {
        return movement;
    }
    return normalized(movement) * maximum;
}

qreal sampleFactor(const DeformationStroke& stroke,
                   const BrushSample& sample,
                   qreal globalStrength,
                   qreal distanceFromCenter)
{
    const qreal influence = brushInfluence(distanceFromCenter, stroke.radius, stroke.hardness);
    return qBound<qreal>(0.0, stroke.strength * globalStrength * sample.pressure * influence, 4.0);
}

QPointF radialDisplacement(BrushMode mode,
                           const QPointF& point,
                           const QPointF& center,
                           qreal radius,
                           qreal factor)
{
    const QPointF direction = normalized(point - center);
    if (direction.isNull()) {
        return {};
    }
    const qreal distanceFromCenter = distance(point, center);
    const qreal amount = qMin(radius * 0.18, qMax<qreal>(0.0, distanceFromCenter * 0.5)) * factor;
    const qreal sign = mode == BrushMode::Pinch ? -1.0 : 1.0;
    return direction * (sign * amount);
}

void applyGlyphStroke(const DeformationStroke& stroke,
                      qreal globalStrength,
                      VectorGeometry& geometry)
{
    if (stroke.mode == BrushMode::Smooth || stroke.samples.isEmpty()) {
        return;
    }

    for (GeometryPiece& piece : geometry.pieces) {
        QPointF totalDisplacement;
        for (const BrushSample& sample : stroke.samples) {
            const qreal factor = sampleFactor(
                stroke, sample, globalStrength, distance(piece.anchor, sample.position));
            if (factor <= 0.0) {
                continue;
            }
            switch (stroke.mode) {
            case BrushMode::Push:
                totalDisplacement += boundedMovement(sample.delta, stroke.radius) * factor;
                break;
            case BrushMode::Pull:
                totalDisplacement += (sample.position - piece.anchor) * qMin<qreal>(0.75, factor * 0.2);
                break;
            case BrushMode::Inflate:
            case BrushMode::Pinch:
                totalDisplacement += radialDisplacement(
                    stroke.mode, piece.anchor, sample.position, stroke.radius, factor);
                break;
            case BrushMode::Smooth:
                break;
            }
        }
        if (finitePoint(totalDisplacement)) {
            QTransform transform;
            Q_UNUSED(transform.translate(totalDisplacement.x(), totalDisplacement.y()));
            piece.path = transform.map(piece.path);
            piece.anchor = transform.map(piece.anchor);
        }
    }
}

void applyShapeStroke(const DeformationStroke& stroke,
                      qreal globalStrength,
                      qreal samplingTolerance,
                      VectorGeometry& geometry)
{
    if (stroke.samples.isEmpty()) {
        return;
    }

    for (GeometryPiece& piece : geometry.pieces) {
        QVector<SampledContour> contours = ContourSampler::samplePath(
            piece.path, samplingTolerance, 8192);
        if (contours.isEmpty()) {
            continue;
        }

        for (const BrushSample& sample : stroke.samples) {
            for (SampledContour& contour : contours) {
                if (contour.points.isEmpty()) {
                    continue;
                }
                const QVector<QPointF> beforeSmooth = contour.points;
                const int pointCount = contour.points.size();
                for (int pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
                    QPointF& point = contour.points[pointIndex];
                    const qreal factor = sampleFactor(
                        stroke, sample, globalStrength, distance(point, sample.position));
                    if (factor <= 0.0) {
                        continue;
                    }

                    QPointF displacement;
                    switch (stroke.mode) {
                    case BrushMode::Push:
                        displacement = boundedMovement(sample.delta, stroke.radius) * factor;
                        break;
                    case BrushMode::Pull:
                        displacement = (sample.position - point) * qMin<qreal>(0.75, factor * 0.2);
                        break;
                    case BrushMode::Inflate:
                    case BrushMode::Pinch:
                        displacement = radialDisplacement(
                            stroke.mode, point, sample.position, stroke.radius, factor);
                        break;
                    case BrushMode::Smooth: {
                        const int previousIndex = contour.closed
                            ? (pointIndex - 1 + pointCount) % pointCount
                            : qMax(0, pointIndex - 1);
                        const int nextIndex = contour.closed
                            ? (pointIndex + 1) % pointCount
                            : qMin(pointCount - 1, pointIndex + 1);
                        const QPointF average = (beforeSmooth[previousIndex] + beforeSmooth[nextIndex]) * 0.5;
                        displacement = (average - point) * qMin<qreal>(1.0, factor * 0.35);
                        break;
                    }
                    }

                    const QPointF candidate = point + displacement;
                    if (finitePoint(candidate)) {
                        point = candidate;
                    }
                }
            }
        }

        piece.path = ContourSampler::reconstructPath(
            contours, piece.path.fillRule(), samplingTolerance * 0.35);
    }
}

} // namespace

void DeformationEvaluator::apply(const ManualDeformation& deformation, VectorGeometry& geometry)
{
    if (!deformation.enabled || deformation.strokes.isEmpty()) {
        return;
    }
    const qreal globalStrength = qBound<qreal>(0.0, deformation.strength, MaximumGlobalStrength);
    if (qFuzzyIsNull(globalStrength)) {
        return;
    }

    const qreal samplingTolerance = qBound<qreal>(0.05, geometry.referenceHeight * 0.0025, 1.0);
    for (const DeformationStroke& stroke : deformation.strokes) {
        if (stroke.samples.isEmpty() || !std::isfinite(stroke.radius) || stroke.radius <= 0.0) {
            continue;
        }
        const qreal boundedRadius = qBound<qreal>(0.01, stroke.radius, 100000.0);
        DeformationStroke boundedStroke = stroke;
        boundedStroke.radius = boundedRadius;
        boundedStroke.strength = qBound<qreal>(0.0, stroke.strength, 4.0);
        boundedStroke.hardness = qBound<qreal>(0.0, stroke.hardness, 1.0);
        if (boundedStroke.mode == BrushMode::Smooth) {
            // Smooth is a contour operation. Normalize invalid legacy or
            // programmatic Glyphs strokes instead of silently dropping them
            // in applyGlyphStroke().
            boundedStroke.target = BrushTarget::Shape;
        }
        if (boundedStroke.target == BrushTarget::Glyphs) {
            applyGlyphStroke(boundedStroke, globalStrength, geometry);
        } else {
            applyShapeStroke(boundedStroke, globalStrength, samplingTolerance, geometry);
        }
        geometry.recomputeBounds();
    }
}

} // namespace vt
