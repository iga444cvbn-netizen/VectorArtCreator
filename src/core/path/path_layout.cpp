#include "core/path/path_layout.h"

#include "core/text/text_engine.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vt {
namespace {

constexpr qreal MinimumLength = 1.0e-9;

QPointF cubicPoint(const QPointF& p0,
                   const QPointF& p1,
                   const QPointF& p2,
                   const QPointF& p3,
                   qreal t)
{
    const qreal oneMinusT = 1.0 - t;
    const qreal a = oneMinusT * oneMinusT * oneMinusT;
    const qreal b = 3.0 * oneMinusT * oneMinusT * t;
    const qreal c = 3.0 * oneMinusT * t * t;
    const qreal d = t * t * t;
    return p0 * a + p1 * b + p2 * c + p3 * d;
}

QPointF cubicTangent(const QPointF& p0,
                     const QPointF& p1,
                     const QPointF& p2,
                     const QPointF& p3,
                     qreal t)
{
    const qreal oneMinusT = 1.0 - t;
    return (p1 - p0) * (3.0 * oneMinusT * oneMinusT)
        + (p2 - p1) * (6.0 * oneMinusT * t)
        + (p3 - p2) * (3.0 * t * t);
}

QPointF normalizedOrFallback(const QPointF& vector, const QPointF& fallback)
{
    const qreal length = std::hypot(vector.x(), vector.y());
    if (length > MinimumLength && std::isfinite(length)) {
        return vector / length;
    }
    const qreal fallbackLength = std::hypot(fallback.x(), fallback.y());
    if (fallbackLength > MinimumLength && std::isfinite(fallbackLength)) {
        return fallback / fallbackLength;
    }
    return QPointF(1.0, 0.0);
}

PathArcSample makeSample(const PathArcSegment& segment, qreal distance, qreal t)
{
    const QPointF point = cubicPoint(segment.p0, segment.p1, segment.p2, segment.p3, t);
    const QPointF chord = segment.p3 - segment.p0;
    return {distance, t, point, normalizedOrFallback(
        cubicTangent(segment.p0, segment.p1, segment.p2, segment.p3, t), chord)};
}

bool appendCubicSubdivision(PathArcSegment* segment,
                            qreal t0,
                            qreal t1,
                            const QPointF& p0,
                            const QPointF& p1,
                            const QPointF& p2,
                            const QPointF& p3,
                            int depth,
                            const WorkControl& work)
{
    if (!segment || !work.consume()) {
        return false;
    }
    const qreal chord = QLineF(p0, p3).length();
    const qreal controlLength = QLineF(p0, p1).length()
        + QLineF(p1, p2).length() + QLineF(p2, p3).length();
    const qreal tolerance = qMax<qreal>(0.01, (controlLength + chord) * 0.0001);
    if (depth >= PathArcLengthTable::MaximumSubdivisionDepth
        || segment->samples.size() >= PathArcLengthTable::MaximumSamplesPerSegment - 1
        || controlLength - chord <= tolerance) {
        const qreal previousDistance = segment->samples.isEmpty()
            ? 0.0
            : segment->samples.constLast().distance;
        const QPointF previousPoint = segment->samples.isEmpty()
            ? segment->p0
            : segment->samples.constLast().point;
        const qreal distance = previousDistance + QLineF(previousPoint, p3).length();
        if (segment->samples.size() < PathArcLengthTable::MaximumSamplesPerSegment) {
            segment->samples.push_back(makeSample(*segment, distance, t1));
        } else {
            // A saturated traversal still has to finish at the current
            // interval's endpoint. Replacing the last sample keeps the table
            // bounded and preserves a usable terminal lookup point.
            segment->samples.last() = makeSample(*segment, distance, t1);
        }
        return true;
    }

    const QPointF p01 = (p0 + p1) * 0.5;
    const QPointF p12 = (p1 + p2) * 0.5;
    const QPointF p23 = (p2 + p3) * 0.5;
    const QPointF p012 = (p01 + p12) * 0.5;
    const QPointF p123 = (p12 + p23) * 0.5;
    const QPointF middle = (p012 + p123) * 0.5;
    const qreal tm = (t0 + t1) * 0.5;
    if (!appendCubicSubdivision(segment, t0, tm, p0, p01, p012, middle,
                                depth + 1, work)) {
        return false;
    }
    // The left recursion emits a t=1 sample for its local curve. Replace its
    // parameter with the global midpoint before descending into the right
    // half. Distances remain accumulated from the actual polyline points.
    if (!segment->samples.isEmpty()) {
        segment->samples.last().t = tm;
        segment->samples.last().point = middle;
        segment->samples.last().tangent = normalizedOrFallback(
            cubicTangent(segment->p0, segment->p1, segment->p2, segment->p3, tm),
            segment->p3 - segment->p0);
    }
    return appendCubicSubdivision(segment, tm, t1, middle, p123, p23, p3,
                                  depth + 1, work);
}

bool appendLineSubdivision(PathArcSegment* segment, const WorkControl& work)
{
    if (!segment || !work.consume()) {
        return false;
    }
    const qreal length = QLineF(segment->p0, segment->p3).length();
    const QPointF tangent = normalizedOrFallback(segment->p3 - segment->p0,
                                                 QPointF(1.0, 0.0));
    segment->samples.push_back({0.0, 0.0, segment->p0, tangent});
    segment->samples.push_back({length, 1.0, segment->p3, tangent});
    segment->length = length;
    return true;
}

void setSegmentLength(PathArcSegment* segment)
{
    if (!segment || segment->samples.size() < 2) {
        return;
    }
    qreal length = 0.0;
    for (int index = 1; index < segment->samples.size(); ++index) {
        const QPointF delta = segment->samples.at(index).point
            - segment->samples.at(index - 1).point;
        const qreal step = std::hypot(delta.x(), delta.y());
        length += std::isfinite(step) ? step : 0.0;
        segment->samples[index].distance = length;
    }
    segment->length = length;
}

QPointF interpolatedTangent(const PathArcSample& left,
                            const PathArcSample& right,
                            qreal fraction)
{
    return normalizedOrFallback(left.tangent * (1.0 - fraction)
                                    + right.tangent * fraction,
                                right.point - left.point);
}

} // namespace

std::optional<PathArcLengthTable> PathArcLengthTable::build(
    const PathGeometry& path,
    const WorkControl& work)
{
    if (!path.validate() || path.nodes.isEmpty() || path.segmentCount() <= 0) {
        PathArcLengthTable empty;
        if (!work.consume()) {
            return std::nullopt;
        }
        return empty;
    }

    PathArcLengthTable table;
    table.m_segments.reserve(path.segmentCount());
    for (int index = 0; index < path.segmentCount(); ++index) {
        if (!work.consume()) {
            return std::nullopt;
        }
        PathArcSegment segment;
        if (!path.segmentControlPoints(index, &segment.p0, &segment.p1,
                                       &segment.p2, &segment.p3)) {
            return std::nullopt;
        }
        const bool cubic = path.isCubicSegment(index);
        const bool isLine = !cubic
            || (segment.p0 == segment.p1 && segment.p2 == segment.p3);
        if (isLine) {
            if (!appendLineSubdivision(&segment, work)) {
                return std::nullopt;
            }
        } else {
            segment.samples.push_back(makeSample(segment, 0.0, 0.0));
            if (!appendCubicSubdivision(&segment, 0.0, 1.0, segment.p0, segment.p1,
                                         segment.p2, segment.p3, 0, work)) {
                return std::nullopt;
            }
            setSegmentLength(&segment);
        }
        table.m_totalLength += segment.length;
        table.m_segments.push_back(std::move(segment));
    }
    if (!std::isfinite(table.m_totalLength)) {
        return std::nullopt;
    }
    return table;
}

PathPosition PathArcLengthTable::positionAt(qreal distance, bool wrapClosed) const
{
    PathPosition result;
    if (!std::isfinite(distance) || m_segments.isEmpty()) {
        return result;
    }
    if (m_totalLength <= MinimumLength) {
        return result;
    }
    if (wrapClosed) {
        distance = std::fmod(distance, m_totalLength);
        if (distance < 0.0) {
            distance += m_totalLength;
        }
    } else if (distance < 0.0 || distance > m_totalLength) {
        return result;
    }

    qreal segmentStart = 0.0;
    const PathArcSegment* selected = nullptr;
    for (const PathArcSegment& segment : m_segments) {
        if (distance <= segmentStart + segment.length || &segment == &m_segments.constLast()) {
            selected = &segment;
            break;
        }
        segmentStart += segment.length;
    }
    if (!selected || selected->samples.size() < 2) {
        return result;
    }

    const qreal localDistance = qBound<qreal>(0.0, distance - segmentStart,
                                               selected->length);
    auto upper = std::upper_bound(
        selected->samples.cbegin(), selected->samples.cend(), localDistance,
        [](qreal value, const PathArcSample& sample) {
            return value < sample.distance;
        });
    if (upper == selected->samples.cbegin()) {
        upper = selected->samples.cbegin() + 1;
    }
    if (upper == selected->samples.cend()) {
        upper = selected->samples.cend() - 1;
    }
    const auto left = upper - 1;
    const PathArcSample& a = *left;
    const PathArcSample& b = *upper;
    const qreal denominator = b.distance - a.distance;
    const qreal fraction = denominator > MinimumLength
        ? qBound<qreal>(0.0, (localDistance - a.distance) / denominator, 1.0)
        : 0.0;
    const qreal t = a.t * (1.0 - fraction) + b.t * fraction;
    result.valid = true;
    result.distance = distance;
    result.point = cubicPoint(selected->p0, selected->p1, selected->p2, selected->p3, t);
    result.tangent = interpolatedTangent(a, b, fraction);
    return result;
}

bool PathLayoutEngine::apply(VectorGeometry* geometry,
                             const ShapedText& shaped,
                             const PathGeometry& path,
                             const PathTypographyProperties& settings,
                             QString* error,
                             const WorkControl& work)
{
    if (!geometry) {
        if (error) {
            *error = QStringLiteral("Path layout received null geometry.");
        }
        return false;
    }
    if (!settings.enabled) {
        return true;
    }
    if (!settings.isFinite() || settings.overflow != PathOverflowMode::Clip) {
        if (error) {
            *error = QStringLiteral("Path typography contains an unsupported overflow mode.");
        }
        return false;
    }
    if (settings.pathId.trimmed().isEmpty() || settings.pathId != path.id) {
        if (error) {
            *error = QStringLiteral("Path typography references a different path identity.");
        }
        return false;
    }
    if (!path.validate(error)) {
        return false;
    }
    PathGeometry traversal = path;
    if (settings.reverse) {
        traversal.reverseDirection();
    }
    const std::optional<PathArcLengthTable> table = PathArcLengthTable::build(traversal, work);
    if (!table.has_value() || !work.isRunning()) {
        if (error && !work.isRunning()) {
            *error = work.interruptionMessage();
        }
        return false;
    }
    VectorGeometry candidate = *geometry;
    if (table->totalLength() <= MinimumLength) {
        // A degenerate path has no meaningful baseline. Clear pieces instead
        // of returning one endpoint for every glyph. Keep the mutation in the
        // candidate so cancellation can never publish a partial stage.
        for (GeometryPiece& piece : candidate.pieces) {
            piece.path = QPainterPath();
        }
        candidate.recomputeBounds();
        *geometry = std::move(candidate);
        return true;
    }

    const bool wrapClosed = traversal.closed;
    const auto lineOffset = [&shaped](int lineIndex) {
        if (lineIndex < 0 || lineIndex >= shaped.lineBounds.size()) {
            return 0.0;
        }
        return shaped.lineBounds.at(lineIndex).y();
    };

    for (GeometryPiece& piece : candidate.pieces) {
        if (!work.consume()) {
            if (error) {
                *error = work.interruptionMessage();
            }
            return false;
        }
        const int lineIndex = qMax(0, piece.sourceLineIndex);
        const QRectF line = lineIndex < shaped.lineBounds.size()
            ? shaped.lineBounds.at(lineIndex)
            : QRectF();
        const QPointF origin = piece.layoutOrigin.isNull()
            ? piece.anchor
            : piece.layoutOrigin;
        qreal relativeX = origin.x() - line.left();
        if (!std::isfinite(relativeX)) {
            relativeX = 0.0;
        }
        const qreal advance = std::isfinite(piece.layoutAdvance)
            ? qMax<qreal>(0.0, piece.layoutAdvance) : 0.0;
        if (advance > table->totalLength() && !wrapClosed) {
            piece.path = QPainterPath();
            continue;
        }
        const qreal distance = settings.startOffset + relativeX;
        const qreal endDistance = distance + advance;
        if (!std::isfinite(distance) || !std::isfinite(endDistance)) {
            piece.path = QPainterPath();
            continue;
        }
        if (!wrapClosed && (distance < 0.0 || endDistance > table->totalLength())) {
            // Clip the complete glyph. This is intentional: it prevents the
            // common endpoint-pile-up failure for overflow text.
            piece.path = QPainterPath();
            continue;
        }
        const PathPosition start = table->positionAt(distance, wrapClosed);
        const PathPosition middle = table->positionAt(
            distance + advance * 0.5, wrapClosed);
        if (!start.valid || !middle.valid) {
            piece.path = QPainterPath();
            continue;
        }
        QPointF tangent = settings.followTangent ? middle.tangent : QPointF(1.0, 0.0);
        tangent = normalizedOrFallback(tangent, QPointF(1.0, 0.0));
        QPointF normal(-tangent.y(), tangent.x());
        if (settings.flip) {
            // Flip changes the side of the baseline without mirroring or
            // turning the glyph outline. Reverse traversal remains the
            // independent direction control.
            normal = -normal;
        }
        const QPointF target = start.point + normal
            * (settings.baselineOffset + lineOffset(lineIndex));
        const QPointF localOrigin = origin;
        QTransform transform;
        transform.setMatrix(tangent.x(), tangent.y(), 0.0,
                            normal.x(), normal.y(), 0.0,
                            target.x() - tangent.x() * localOrigin.x()
                                - normal.x() * localOrigin.y(),
                            target.y() - tangent.y() * localOrigin.x()
                                - normal.y() * localOrigin.y(),
                            1.0);
        piece.path = transform.map(piece.path);
        piece.anchor = target;
    }
    const QPainterPath derivedPath = traversal.toPainterPath();
    const QRectF pathBounds = derivedPath.boundingRect();
    if (!pathBounds.isEmpty()) {
        candidate.setReferenceBounds(pathBounds);
    }
    if (!work.isRunning()) {
        if (error) {
            *error = work.interruptionMessage();
        }
        return false;
    }
    candidate.recomputeBounds();
    *geometry = std::move(candidate);
    return true;
}

} // namespace vt
