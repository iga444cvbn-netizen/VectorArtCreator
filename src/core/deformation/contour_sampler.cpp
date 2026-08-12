#include "core/deformation/contour_sampler.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vt {

namespace {

constexpr int MaximumSubdivisionDepth = 12;

qreal distanceSquared(const QPointF& left, const QPointF& right)
{
    const qreal dx = left.x() - right.x();
    const qreal dy = left.y() - right.y();
    return dx * dx + dy * dy;
}

qreal pointLineDistanceSquared(const QPointF& point,
                               const QPointF& lineStart,
                               const QPointF& lineEnd)
{
    const QPointF line = lineEnd - lineStart;
    const qreal lengthSquared = line.x() * line.x() + line.y() * line.y();
    if (lengthSquared <= 1.0e-12) {
        return distanceSquared(point, lineStart);
    }
    const qreal projection = qBound<qreal>(
        0.0,
        ((point.x() - lineStart.x()) * line.x() + (point.y() - lineStart.y()) * line.y())
            / lengthSquared,
        1.0);
    const QPointF closest = lineStart + line * projection;
    return distanceSquared(point, closest);
}

void appendDistinct(QVector<QPointF>* points, const QPointF& point)
{
    if (!points || !std::isfinite(point.x()) || !std::isfinite(point.y())) {
        return;
    }
    if (points->isEmpty() || distanceSquared(points->last(), point) > 1.0e-10) {
        points->push_back(point);
    }
}

void appendBounded(QVector<QPointF>* points, const QPointF& point, int maxPoints)
{
    if (!points || !std::isfinite(point.x()) || !std::isfinite(point.y())) {
        return;
    }
    if (points->size() < maxPoints) {
        appendDistinct(points, point);
    } else if (!points->isEmpty()) {
        // Keep the contour endpoint even when adaptive subdivision reaches
        // its safety cap. This preserves an open path's endpoint without
        // allowing the cap to be exceeded.
        points->last() = point;
    }
}

void flattenCubic(const QPointF& p0,
                  const QPointF& p1,
                  const QPointF& p2,
                  const QPointF& p3,
                  qreal toleranceSquared,
                  int depth,
                  int maxPoints,
                  QVector<QPointF>* points,
                  const WorkControl& work)
{
    if (!points || !work.consume()) {
        return;
    }
    if (points->size() >= maxPoints) {
        appendBounded(points, p3, maxPoints);
        return;
    }

    const qreal flatness = qMax(pointLineDistanceSquared(p1, p0, p3),
                                pointLineDistanceSquared(p2, p0, p3));
    if (depth >= MaximumSubdivisionDepth || flatness <= toleranceSquared) {
        appendBounded(points, p3, maxPoints);
        return;
    }

    const QPointF p01 = (p0 + p1) * 0.5;
    const QPointF p12 = (p1 + p2) * 0.5;
    const QPointF p23 = (p2 + p3) * 0.5;
    const QPointF p012 = (p01 + p12) * 0.5;
    const QPointF p123 = (p12 + p23) * 0.5;
    const QPointF midpoint = (p012 + p123) * 0.5;
    flattenCubic(p0, p01, p012, midpoint, toleranceSquared, depth + 1,
                 maxPoints, points, work);
    flattenCubic(midpoint, p123, p23, p3, toleranceSquared, depth + 1,
                 maxPoints, points, work);
}

QVector<QPointF> simplifyContour(const QVector<QPointF>& points,
                                 bool closed,
                                 qreal tolerance,
                                 const WorkControl& work)
{
    if (points.size() < 3) {
        return points;
    }

    const qreal minimumDistanceSquared = qMax<qreal>(1.0e-8, tolerance * tolerance * 0.25);
    QVector<QPointF> result;
    result.reserve(points.size());
    for (const QPointF& point : points) {
        if (!work.consume()) {
            return {};
        }
        if (result.isEmpty() || distanceSquared(result.last(), point) > minimumDistanceSquared) {
            result.push_back(point);
        }
    }
    if (closed && result.size() > 1
        && distanceSquared(result.first(), result.last()) <= minimumDistanceSquared) {
        result.removeLast();
    }
    if (result.size() < 3) {
        return result;
    }

    bool removed = true;
    const qreal collinearToleranceSquared = tolerance * tolerance * 0.04;
    while (removed && result.size() > (closed ? 3 : 2)) {
        removed = false;
        const int count = result.size();
        const int begin = closed ? 0 : 1;
        const int end = closed ? count : count - 1;
        for (int index = begin; index < end; ++index) {
            if (!work.consume()) {
                return {};
            }
            const int previousIndex = (index - 1 + count) % count;
            const int nextIndex = (index + 1) % count;
            if (!closed && (index == 0 || index == count - 1)) {
                continue;
            }
            const qreal deviation = pointLineDistanceSquared(
                result[index], result[previousIndex], result[nextIndex]);
            if (deviation <= collinearToleranceSquared) {
                result.removeAt(index);
                removed = true;
                break;
            }
        }
    }
    return result;
}

} // namespace

QVector<SampledContour> ContourSampler::samplePath(const QPainterPath& path,
                                                   qreal tolerance,
                                                   int maxPointsPerContour,
                                                   const WorkControl& work)
{
    QVector<SampledContour> contours;
    if (path.isEmpty()) {
        return contours;
    }

    const qreal boundedTolerance = qMax<qreal>(0.01, tolerance);
    const int boundedMaximum = qMax(8, maxPointsPerContour);
    QVector<QPointF> points;
    QPointF current;
    bool hasCurrent = false;

    const auto flush = [&]() {
        if (points.size() < 2) {
            points.clear();
            return;
        }
        // QPainterPath represents a closed subpath by making its final
        // element coincide with its first element (the same closure test Qt
        // uses internally). Point count alone must never turn an open
        // polyline into a closed contour.
        const bool closed = points.size() >= 3 && points.first() == points.last();
        if (closed) {
            points.removeLast();
        }
        SampledContour contour;
        contour.closed = closed;
        contour.points = simplifyContour(points, closed, boundedTolerance, work);
        if (contour.points.size() >= (closed ? 3 : 2)) {
            contours.push_back(std::move(contour));
        }
        points.clear();
    };

    for (int index = 0; index < path.elementCount(); ++index) {
        if (!work.consume()) {
            break;
        }
        const QPainterPath::Element element = path.elementAt(index);
        switch (element.type) {
        case QPainterPath::MoveToElement:
            flush();
            current = QPointF(element.x, element.y);
            appendDistinct(&points, current);
            hasCurrent = true;
            break;
        case QPainterPath::LineToElement:
            current = QPointF(element.x, element.y);
            if (!hasCurrent) {
                appendDistinct(&points, current);
            } else {
                appendDistinct(&points, current);
            }
            hasCurrent = true;
            break;
        case QPainterPath::CurveToElement: {
            if (!hasCurrent || index + 2 >= path.elementCount()) {
                break;
            }
            const QPointF control1(element.x, element.y);
            const QPainterPath::Element control2Element = path.elementAt(index + 1);
            const QPainterPath::Element endElement = path.elementAt(index + 2);
            const QPointF control2(control2Element.x, control2Element.y);
            const QPointF end(endElement.x, endElement.y);
            flattenCubic(current,
                         control1,
                         control2,
                         end,
                         boundedTolerance * boundedTolerance,
                         0,
                         boundedMaximum,
                         &points,
                         work);
            current = end;
            index += 2;
            break;
        }
        case QPainterPath::CurveToDataElement:
            break;
        }
    }
    flush();
    return contours;
}

QPainterPath ContourSampler::reconstructPath(const QVector<SampledContour>& contours,
                                             Qt::FillRule fillRule,
                                             qreal simplificationTolerance,
                                             const WorkControl& work)
{
    QPainterPath result;
    result.setFillRule(fillRule);
    for (const SampledContour& contour : contours) {
        if (!work.consume()) {
            break;
        }
        const QVector<QPointF> points = simplifyContour(
            contour.points, contour.closed, qMax<qreal>(0.01, simplificationTolerance), work);
        if (points.size() < (contour.closed ? 3 : 2)) {
            continue;
        }
        result.moveTo(points.first());
        for (int index = 1; index < points.size(); ++index) {
            if (!work.consume()) {
                return result;
            }
            result.lineTo(points[index]);
        }
        if (contour.closed) {
            result.closeSubpath();
        }
    }
    return result;
}

} // namespace vt
