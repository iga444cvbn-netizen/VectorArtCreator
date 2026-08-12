#include "core/effects/effect_mask_distance.h"

#include "core/deformation/contour_sampler.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vt {

namespace {

bool finitePoint(const QPointF& point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool finitePath(const QPainterPath& path)
{
    for (int index = 0; index < path.elementCount(); ++index) {
        const QPainterPath::Element element = path.elementAt(index);
        if (!std::isfinite(element.x) || !std::isfinite(element.y)) {
            return false;
        }
    }
    return true;
}

qreal pointSegmentDistance(const QPointF& point, const QPointF& start, const QPointF& end)
{
    const QPointF segment = end - start;
    const qreal lengthSquared = QPointF::dotProduct(segment, segment);
    if (lengthSquared <= 1.0e-12) {
        return QLineF(point, start).length();
    }
    const qreal parameter = qBound<qreal>(
        0.0, QPointF::dotProduct(point - start, segment) / lengthSquared, 1.0);
    return QLineF(point, start + segment * parameter).length();
}

qreal segmentDistance(const QPointF& firstStart,
                      const QPointF& firstEnd,
                      const QPointF& secondStart,
                      const QPointF& secondEnd)
{
    QPointF intersection;
    if (QLineF(firstStart, firstEnd).intersects(
            QLineF(secondStart, secondEnd), &intersection) == QLineF::BoundedIntersection) {
        return 0.0;
    }
    return std::min({pointSegmentDistance(firstStart, secondStart, secondEnd),
                     pointSegmentDistance(firstEnd, secondStart, secondEnd),
                     pointSegmentDistance(secondStart, firstStart, firstEnd),
                     pointSegmentDistance(secondEnd, firstStart, firstEnd)});
}

} // namespace

qreal EffectMaskDistance::minimumContourDistance(const QPainterPath& path,
                                                 const QVector<QPointF>& brushPoints,
                                                 qreal flattenTolerance,
                                                 const WorkControl& work)
{
    const qreal infinity = std::numeric_limits<qreal>::infinity();
    if (path.isEmpty() || brushPoints.isEmpty()
        || !std::isfinite(flattenTolerance) || flattenTolerance <= 0.0
        || !finitePath(path)) {
        return infinity;
    }
    for (const QPointF& brushPoint : brushPoints) {
        if (!finitePoint(brushPoint)) {
            return infinity;
        }
    }
    if (!work.isRunning()) {
        return infinity;
    }
    for (const QPointF& brushPoint : brushPoints) {
        if (!work.consume(qMax(1, path.elementCount()))) {
            return infinity;
        }
        // Distance is to the painted fill, not merely its outline. Counters and
        // concave gaps remain outside according to the path's fill rule.
        if (path.contains(brushPoint)) {
            return 0.0;
        }
    }
    const QVector<SampledContour> contours = ContourSampler::samplePath(
        path, qBound<qreal>(0.02, flattenTolerance, 1.0), 8192, work);
    if (!work.isRunning()) {
        return infinity;
    }
    qreal minimum = infinity;
    for (const SampledContour& contour : contours) {
        if (!work.consume()) {
            return infinity;
        }
        const int contourSegments = contour.closed
            ? contour.points.size()
            : qMax(0, contour.points.size() - 1);
        for (int contourIndex = 0; contourIndex < contourSegments; ++contourIndex) {
            if (!work.consume()) {
                return infinity;
            }
            const QPointF contourStart = contour.points.at(contourIndex);
            const QPointF contourEnd = contour.points.at(
                (contourIndex + 1) % contour.points.size());
            if (brushPoints.size() == 1) {
                minimum = qMin(minimum,
                               pointSegmentDistance(brushPoints.front(), contourStart, contourEnd));
                continue;
            }
            for (int brushIndex = 1; brushIndex < brushPoints.size(); ++brushIndex) {
                if (!work.consume()) {
                    return infinity;
                }
                minimum = qMin(minimum,
                               segmentDistance(contourStart,
                                               contourEnd,
                                               brushPoints.at(brushIndex - 1),
                                               brushPoints.at(brushIndex)));
                if (qFuzzyIsNull(minimum)) {
                    return 0.0;
                }
            }
        }
    }
    return work.isRunning() ? minimum : infinity;
}

qreal EffectMaskDistance::strokeInfluence(const QPainterPath& path,
                                          const QVector<QPointF>& brushPoints,
                                          qreal radius,
                                          qreal hardness,
                                          qreal opacity,
                                          const WorkControl& work)
{
    if (!std::isfinite(radius) || radius <= 0.0
        || !std::isfinite(hardness) || !std::isfinite(opacity)
        || brushPoints.isEmpty() || !work.isRunning()) {
        return 0.0;
    }
    const qreal distance = minimumContourDistance(
        path, brushPoints, qBound<qreal>(0.02, radius * 0.02, 0.5), work);
    if (!work.isRunning() || !std::isfinite(distance) || distance >= radius) {
        return 0.0;
    }
    const qreal boundedHardness = qBound<qreal>(0.0, hardness, 1.0);
    const qreal innerRadius = radius * (0.1 + 0.85 * boundedHardness);
    const qreal normalized = innerRadius >= radius
        ? 1.0
        : qBound<qreal>(0.0, (radius - distance) / (radius - innerRadius), 1.0);
    const qreal falloff = normalized * normalized * (3.0 - 2.0 * normalized);
    return qBound<qreal>(0.0, opacity, 1.0) * falloff;
}

} // namespace vt
