#include "core/region/typography_region.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QLineF>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace vt {
namespace {

constexpr qreal GeometryEpsilon = 1.0e-7;
constexpr int MaximumFlattenDepth = 12;
constexpr int MaximumFlattenedPoints = 8192;

QString idWithPrefix(const QString& prefix)
{
    return prefix + QLatin1Char('-')
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool finitePoint(const QPointF& point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

qreal cross(const QPointF& a, const QPointF& b, const QPointF& c)
{
    const QPointF ab = b - a;
    const QPointF ac = c - a;
    return ab.x() * ac.y() - ab.y() * ac.x();
}

qreal pointLineDistance(const QPointF& point,
                        const QPointF& start,
                        const QPointF& end)
{
    const QPointF delta = end - start;
    const qreal length = std::hypot(delta.x(), delta.y());
    if (length <= GeometryEpsilon) {
        return QLineF(point, start).length();
    }
    return std::abs(cross(start, end, point)) / length;
}

QPointF cubicPoint(const QPointF& p0,
                   const QPointF& p1,
                   const QPointF& p2,
                   const QPointF& p3,
                   qreal t)
{
    const qreal u = 1.0 - t;
    return p0 * (u * u * u)
        + p1 * (3.0 * u * u * t)
        + p2 * (3.0 * u * t * t)
        + p3 * (t * t * t);
}

bool appendCubic(QVector<QPointF>* points,
                 const QPointF& p0,
                 const QPointF& p1,
                 const QPointF& p2,
                 const QPointF& p3,
                 qreal tolerance,
                 int depth,
                 const WorkControl& work)
{
    if (!points || points->size() >= MaximumFlattenedPoints || !work.consume()) {
        return false;
    }
    const qreal controlLength = QLineF(p0, p1).length()
        + QLineF(p1, p2).length() + QLineF(p2, p3).length();
    const qreal chord = QLineF(p0, p3).length();
    const qreal localTolerance = qMax<qreal>(
        qMax<qreal>(GeometryEpsilon, tolerance), (controlLength + chord) * 1.0e-7);
    if (depth >= MaximumFlattenDepth
        || points->size() >= MaximumFlattenedPoints - 1
        || (pointLineDistance(p1, p0, p3) <= localTolerance
            && pointLineDistance(p2, p0, p3) <= localTolerance)) {
        points->push_back(p3);
        return true;
    }

    const QPointF p01 = (p0 + p1) * 0.5;
    const QPointF p12 = (p1 + p2) * 0.5;
    const QPointF p23 = (p2 + p3) * 0.5;
    const QPointF p012 = (p01 + p12) * 0.5;
    const QPointF p123 = (p12 + p23) * 0.5;
    const QPointF middle = (p012 + p123) * 0.5;
    return appendCubic(points, p0, p01, p012, middle, tolerance,
                       depth + 1, work)
        && appendCubic(points, middle, p123, p23, p3, tolerance,
                       depth + 1, work);
}

bool onSegment(const QPointF& a, const QPointF& b, const QPointF& point)
{
    return point.x() >= qMin(a.x(), b.x()) - GeometryEpsilon
        && point.x() <= qMax(a.x(), b.x()) + GeometryEpsilon
        && point.y() >= qMin(a.y(), b.y()) - GeometryEpsilon
        && point.y() <= qMax(a.y(), b.y()) + GeometryEpsilon
        && std::abs(cross(a, b, point)) <= GeometryEpsilon;
}

bool segmentsIntersect(const QPointF& a,
                       const QPointF& b,
                       const QPointF& c,
                       const QPointF& d)
{
    const qreal abC = cross(a, b, c);
    const qreal abD = cross(a, b, d);
    const qreal cdA = cross(c, d, a);
    const qreal cdB = cross(c, d, b);
    const auto opposite = [](qreal left, qreal right) {
        return (left > GeometryEpsilon && right < -GeometryEpsilon)
            || (left < -GeometryEpsilon && right > GeometryEpsilon);
    };
    if (opposite(abC, abD) && opposite(cdA, cdB)) {
        return true;
    }
    return onSegment(a, b, c) || onSegment(a, b, d)
        || onSegment(c, d, a) || onSegment(c, d, b);
}

bool adjacentEdges(int left, int right, int edgeCount)
{
    return left == right
        || (left + 1) % edgeCount == right
        || (right + 1) % edgeCount == left;
}

bool pointInPolygon(const QVector<QPointF>& polygon, const QPointF& point)
{
    if (polygon.size() < 4) {
        return false;
    }
    bool inside = false;
    for (int index = 0, previous = polygon.size() - 1;
         index < polygon.size(); previous = index++) {
        const QPointF& a = polygon.at(previous);
        const QPointF& b = polygon.at(index);
        if (onSegment(a, b, point)) {
            return false;
        }
        const bool crosses = (a.y() > point.y()) != (b.y() > point.y());
        if (crosses) {
            const qreal x = (b.x() - a.x()) * (point.y() - a.y())
                / (b.y() - a.y()) + a.x();
            if (point.x() < x) {
                inside = !inside;
            }
        }
    }
    return inside;
}

bool simpleContour(const QVector<QPointF>& polygon,
                   QString* error,
                   const WorkControl& work)
{
    if (polygon.size() < 4 || polygon.first() != polygon.last()) {
        if (error) *error = QStringLiteral("Region contours must be closed and non-degenerate.");
        return false;
    }
    qreal area = 0.0;
    for (int index = 0; index + 1 < polygon.size(); ++index) {
        area += polygon.at(index).x() * polygon.at(index + 1).y()
            - polygon.at(index + 1).x() * polygon.at(index).y();
    }
    if (!std::isfinite(area) || std::abs(area) <= GeometryEpsilon) {
        if (error) *error = QStringLiteral("Region contours must enclose a non-zero area.");
        return false;
    }
    const int edgeCount = polygon.size() - 1;
    for (int left = 0; left < edgeCount; ++left) {
        for (int right = left + 1; right < edgeCount; ++right) {
            if (!work.consume()) return false;
            if (adjacentEdges(left, right, edgeCount)) {
                continue;
            }
            if (segmentsIntersect(polygon.at(left), polygon.at(left + 1),
                                  polygon.at(right), polygon.at(right + 1))) {
                if (error) *error = QStringLiteral("Self-intersecting region contours are unsupported.");
                return false;
            }
        }
    }
    return true;
}

QVector<RegionInterval> contourIntervals(const QVector<QPointF>& polygon,
                                          qreal y,
                                          const WorkControl& work)
{
    QVector<qreal> crossings;
    if (polygon.size() < 4) {
        return {};
    }
    crossings.reserve(polygon.size());
    constexpr qreal yEpsilon = 1.0e-8;
    for (int index = 0; index + 1 < polygon.size(); ++index) {
        if (!work.consume()) {
            return {};
        }
        const QPointF& a = polygon.at(index);
        const QPointF& b = polygon.at(index + 1);
        if (std::abs(a.y() - b.y()) <= yEpsilon) {
            continue;
        }
        // Half-open edge convention counts a vertex once and handles tangent
        // crossings deterministically without depending on QPainterPath fill.
        const bool crosses = (a.y() <= y && y < b.y())
            || (b.y() <= y && y < a.y());
        if (!crosses) {
            continue;
        }
        const qreal t = (y - a.y()) / (b.y() - a.y());
        const qreal x = a.x() + (b.x() - a.x()) * t;
        if (std::isfinite(x)) {
            crossings.push_back(x);
        }
    }
    std::sort(crossings.begin(), crossings.end());
    QVector<RegionInterval> result;
    for (int index = 0; index + 1 < crossings.size(); index += 2) {
        const qreal left = crossings.at(index);
        const qreal right = crossings.at(index + 1);
        if (right - left > GeometryEpsilon) {
            result.push_back({left, right});
        }
    }
    return result;
}

QVector<RegionInterval> subtractIntervals(const QVector<RegionInterval>& source,
                                           const QVector<RegionInterval>& holes)
{
    QVector<RegionInterval> result = source;
    for (const RegionInterval& hole : holes) {
        QVector<RegionInterval> next;
        for (const RegionInterval& span : result) {
            if (hole.right <= span.left + GeometryEpsilon
                || hole.left >= span.right - GeometryEpsilon) {
                next.push_back(span);
                continue;
            }
            if (hole.left > span.left + GeometryEpsilon) {
                next.push_back({span.left, qMin(hole.left, span.right)});
            }
            if (hole.right < span.right - GeometryEpsilon) {
                next.push_back({qMax(hole.right, span.left), span.right});
            }
        }
        result = std::move(next);
    }
    return result;
}

PathGeometry contourWithNodes(const QString& prefix,
                              const QVector<QPointF>& points)
{
    PathGeometry contour;
    contour.id = idWithPrefix(prefix);
    contour.closed = true;
    for (int index = 0; index < points.size(); ++index) {
        PathNode node;
        node.id = idWithPrefix(prefix + QStringLiteral("-node"));
        node.anchor = points.at(index);
        contour.nodes.push_back(node);
    }
    return contour;
}

bool parseDouble(const QJsonObject& object,
                 const QString& key,
                 qreal fallback,
                 qreal* value,
                 QString* error)
{
    const QJsonValue jsonValue = object.value(key);
    if (jsonValue.isUndefined()) {
        *value = fallback;
        return true;
    }
    if (!jsonValue.isDouble()) {
        if (error) *error = QStringLiteral("Region field '%1' must be numeric.").arg(key);
        return false;
    }
    *value = jsonValue.toDouble();
    return std::isfinite(*value);
}

} // namespace

QString typographyLayoutModeToString(TypographyLayoutMode mode)
{
    switch (mode) {
    case TypographyLayoutMode::Baseline: return QStringLiteral("baseline");
    case TypographyLayoutMode::Path: return QStringLiteral("path");
    case TypographyLayoutMode::Region: return QStringLiteral("region");
    }
    return QStringLiteral("baseline");
}

bool typographyLayoutModeFromString(const QString& value, TypographyLayoutMode* mode)
{
    if (!mode) return false;
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("baseline")) *mode = TypographyLayoutMode::Baseline;
    else if (normalized == QStringLiteral("path")) *mode = TypographyLayoutMode::Path;
    else if (normalized == QStringLiteral("region")) *mode = TypographyLayoutMode::Region;
    else return false;
    return true;
}

QString regionHorizontalAlignmentToString(RegionHorizontalAlignment alignment)
{
    switch (alignment) {
    case RegionHorizontalAlignment::Left: return QStringLiteral("left");
    case RegionHorizontalAlignment::Center: return QStringLiteral("center");
    case RegionHorizontalAlignment::Right: return QStringLiteral("right");
    case RegionHorizontalAlignment::Justified: return QStringLiteral("justified");
    }
    return QStringLiteral("left");
}

bool regionHorizontalAlignmentFromString(const QString& value,
                                         RegionHorizontalAlignment* alignment)
{
    if (!alignment) return false;
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("left")) *alignment = RegionHorizontalAlignment::Left;
    else if (normalized == QStringLiteral("center")) *alignment = RegionHorizontalAlignment::Center;
    else if (normalized == QStringLiteral("right")) *alignment = RegionHorizontalAlignment::Right;
    else if (normalized == QStringLiteral("justified")) *alignment = RegionHorizontalAlignment::Justified;
    else return false;
    return true;
}

QString regionVerticalAlignmentToString(RegionVerticalAlignment alignment)
{
    switch (alignment) {
    case RegionVerticalAlignment::Top: return QStringLiteral("top");
    case RegionVerticalAlignment::Center: return QStringLiteral("center");
    case RegionVerticalAlignment::Bottom: return QStringLiteral("bottom");
    }
    return QStringLiteral("top");
}

bool regionVerticalAlignmentFromString(const QString& value,
                                       RegionVerticalAlignment* alignment)
{
    if (!alignment) return false;
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("top")) *alignment = RegionVerticalAlignment::Top;
    else if (normalized == QStringLiteral("center")) *alignment = RegionVerticalAlignment::Center;
    else if (normalized == QStringLiteral("bottom")) *alignment = RegionVerticalAlignment::Bottom;
    else return false;
    return true;
}

QString regionOverflowModeToString(RegionOverflowMode mode)
{
    Q_UNUSED(mode);
    return QStringLiteral("clip");
}

bool regionOverflowModeFromString(const QString& value, RegionOverflowMode* mode)
{
    if (!mode || value.trimmed().compare(QStringLiteral("clip"), Qt::CaseInsensitive) != 0) {
        return false;
    }
    *mode = RegionOverflowMode::Clip;
    return true;
}

bool RegionTypographyProperties::isFinite() const
{
    constexpr qreal maximumPadding = PathGeometry::MaximumCoordinate;
    const qreal values[] = {paddingLeft, paddingRight, paddingTop, paddingBottom};
    for (qreal value : values) {
        if (!std::isfinite(value) || value < 0.0 || value > maximumPadding) return false;
    }
    const bool validHorizontal = horizontalAlignment == RegionHorizontalAlignment::Left
        || horizontalAlignment == RegionHorizontalAlignment::Center
        || horizontalAlignment == RegionHorizontalAlignment::Right
        || horizontalAlignment == RegionHorizontalAlignment::Justified;
    const bool validVertical = verticalAlignment == RegionVerticalAlignment::Top
        || verticalAlignment == RegionVerticalAlignment::Center
        || verticalAlignment == RegionVerticalAlignment::Bottom;
    return validHorizontal && validVertical && overflow == RegionOverflowMode::Clip;
}

QJsonObject RegionTypographyProperties::toJson() const
{
    return {
        {QStringLiteral("regionId"), regionId},
        {QStringLiteral("paddingLeft"), paddingLeft},
        {QStringLiteral("paddingRight"), paddingRight},
        {QStringLiteral("paddingTop"), paddingTop},
        {QStringLiteral("paddingBottom"), paddingBottom},
        {QStringLiteral("horizontalAlignment"), regionHorizontalAlignmentToString(horizontalAlignment)},
        {QStringLiteral("verticalAlignment"), regionVerticalAlignmentToString(verticalAlignment)},
        {QStringLiteral("overflow"), regionOverflowModeToString(overflow)},
    };
}

bool RegionTypographyProperties::fromJson(const QJsonObject& object,
                                          RegionTypographyProperties* properties,
                                          QString* error)
{
    if (!properties) {
        if (error) *error = QStringLiteral("Region typography output is null.");
        return false;
    }
    RegionTypographyProperties result;
    const QJsonValue idValue = object.value(QStringLiteral("regionId"));
    if (!idValue.isUndefined() && !idValue.isString()) {
        if (error) *error = QStringLiteral("Region typography regionId must be a string.");
        return false;
    }
    result.regionId = idValue.toString();
    if (!parseDouble(object, QStringLiteral("paddingLeft"), result.paddingLeft,
                     &result.paddingLeft, error)
        || !parseDouble(object, QStringLiteral("paddingRight"), result.paddingRight,
                         &result.paddingRight, error)
        || !parseDouble(object, QStringLiteral("paddingTop"), result.paddingTop,
                         &result.paddingTop, error)
        || !parseDouble(object, QStringLiteral("paddingBottom"), result.paddingBottom,
                         &result.paddingBottom, error)) {
        return false;
    }
    const QJsonValue horizontal = object.value(QStringLiteral("horizontalAlignment"));
    if (!horizontal.isUndefined()
        && (!horizontal.isString()
            || !regionHorizontalAlignmentFromString(horizontal.toString(), &result.horizontalAlignment))) {
        if (error) *error = QStringLiteral("Region typography has an invalid horizontal alignment.");
        return false;
    }
    const QJsonValue vertical = object.value(QStringLiteral("verticalAlignment"));
    if (!vertical.isUndefined()
        && (!vertical.isString()
            || !regionVerticalAlignmentFromString(vertical.toString(), &result.verticalAlignment))) {
        if (error) *error = QStringLiteral("Region typography has an invalid vertical alignment.");
        return false;
    }
    const QJsonValue overflow = object.value(QStringLiteral("overflow"));
    if (!overflow.isUndefined()
        && (!overflow.isString()
            || !regionOverflowModeFromString(overflow.toString(), &result.overflow))) {
        if (error) *error = QStringLiteral("Region typography has an invalid overflow mode.");
        return false;
    }
    if (!result.isFinite()) {
        if (error) *error = QStringLiteral("Region typography contains invalid padding.");
        return false;
    }
    *properties = std::move(result);
    return true;
}

TypographyRegion TypographyRegion::makeRectangle(const QRectF& rawBounds)
{
    const QRectF bounds = rawBounds.normalized();
    const QVector<QPointF> points = {
        bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()};
    TypographyRegion region;
    region.id = idWithPrefix(QStringLiteral("region"));
    region.outer = contourWithNodes(QStringLiteral("region-outer"), points);
    return region;
}

TypographyRegion TypographyRegion::makeEllipse(const QRectF& rawBounds)
{
    const QRectF bounds = rawBounds.normalized();
    const qreal kappa = 0.5522847498307936;
    const qreal rx = bounds.width() * 0.5;
    const qreal ry = bounds.height() * 0.5;
    const QPointF center = bounds.center();
    TypographyRegion region;
    region.id = idWithPrefix(QStringLiteral("region"));
    region.outer.id = idWithPrefix(QStringLiteral("region-outer"));
    region.outer.closed = true;
    const QVector<QPointF> anchors = {
        {center.x() + rx, center.y()},
        {center.x(), center.y() + ry},
        {center.x() - rx, center.y()},
        {center.x(), center.y() - ry},
    };
    region.outer.nodes.reserve(anchors.size());
    for (int index = 0; index < anchors.size(); ++index) {
        PathNode node;
        node.id = idWithPrefix(QStringLiteral("region-outer-node"));
        node.anchor = anchors.at(index);
        switch (index) {
        case 0: // right
            node.outgoingHandle = node.anchor + QPointF(0.0, kappa * ry);
            node.incomingHandle = node.anchor + QPointF(0.0, -kappa * ry);
            break;
        case 1: // bottom
            node.outgoingHandle = node.anchor + QPointF(-kappa * rx, 0.0);
            node.incomingHandle = node.anchor + QPointF(kappa * rx, 0.0);
            break;
        case 2: // left
            node.outgoingHandle = node.anchor + QPointF(0.0, -kappa * ry);
            node.incomingHandle = node.anchor + QPointF(0.0, kappa * ry);
            break;
        case 3: // top
            node.outgoingHandle = node.anchor + QPointF(kappa * rx, 0.0);
            node.incomingHandle = node.anchor + QPointF(-kappa * rx, 0.0);
            break;
        default:
            break;
        }
        node.hasIncomingHandle = true;
        node.hasOutgoingHandle = true;
        region.outer.nodes.push_back(node);
    }
    return region;
}

TypographyRegion TypographyRegion::makeCustom(const QRectF& bounds)
{
    const QRectF normalized = bounds.normalized();
    // A deterministic arch-shaped contour gives the preset a useful
    // genuinely concave scanline profile: the top bar is one interval while
    // the lower body exposes two separated leg intervals. It remains an
    // ordinary editable line contour rather than a primitive flag.
    const qreal notchY = normalized.top() + normalized.height() * 0.32;
    const qreal leftLeg = normalized.left() + normalized.width() * 0.30;
    const qreal rightLeg = normalized.left() + normalized.width() * 0.70;
    const QVector<QPointF> points = {
        normalized.topLeft(),
        normalized.topRight(),
        normalized.bottomRight(),
        {rightLeg, normalized.bottom()},
        {rightLeg, notchY},
        {leftLeg, notchY},
        {leftLeg, normalized.bottom()},
        normalized.bottomLeft(),
    };
    TypographyRegion region;
    region.id = idWithPrefix(QStringLiteral("region"));
    region.outer = contourWithNodes(QStringLiteral("region-custom-outer"), points);
    return region;
}

std::optional<QVector<QPointF>> flattenRegionContour(const PathGeometry& contour,
                                                     qreal tolerance,
                                                     const WorkControl& work)
{
    if (!contour.validate() || contour.segmentCount() <= 0 || !contour.closed) {
        return std::nullopt;
    }
    QVector<QPointF> points;
    points.reserve(qMin(MaximumFlattenedPoints, contour.nodes.size() * 4));
    points.push_back(contour.nodes.front().anchor);
    for (int segmentIndex = 0; segmentIndex < contour.segmentCount(); ++segmentIndex) {
        QPointF p0;
        QPointF p1;
        QPointF p2;
        QPointF p3;
        if (!contour.segmentControlPoints(segmentIndex, &p0, &p1, &p2, &p3)
            || !work.consume()) {
            return std::nullopt;
        }
        if (contour.isCubicSegment(segmentIndex)) {
            if (!appendCubic(&points, p0, p1, p2, p3,
                             qMax<qreal>(GeometryEpsilon, tolerance), 0, work)) {
                return std::nullopt;
            }
        } else {
            if (points.size() >= MaximumFlattenedPoints) return std::nullopt;
            points.push_back(p3);
        }
    }
    if (points.isEmpty() || points.last() != points.first()) {
        points.push_back(points.first());
    }
    return points;
}

std::optional<FlattenedTypographyRegion> flattenTypographyRegion(
    const TypographyRegion& region,
    qreal tolerance,
    const WorkControl& work)
{
    const auto outer = flattenRegionContour(region.outer, tolerance, work);
    if (!outer.has_value() || !work.isRunning()) return std::nullopt;
    FlattenedTypographyRegion result;
    result.outer = *outer;
    result.holes.reserve(region.holes.size());
    for (const PathGeometry& hole : region.holes) {
        const auto flattened = flattenRegionContour(hole, tolerance, work);
        if (!flattened.has_value() || !work.isRunning()) return std::nullopt;
        result.holes.push_back(*flattened);
    }
    return result;
}

bool TypographyRegion::validate(QString* error, const WorkControl& work) const
{
    if (!work.consume()) {
        if (error) *error = work.interruptionMessage();
        return false;
    }
    if (id.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("Region has no stable identity.");
        return false;
    }
    if (holes.size() > MaximumHoles) {
        if (error) *error = QStringLiteral("Region has too many hole contours.");
        return false;
    }
    if (!outer.validate(error) || !outer.closed) {
        if (error && error->isEmpty()) *error = QStringLiteral("Region outer contour must be closed.");
        return false;
    }
    QSet<QString> identities;
    identities.insert(id);
    if (identities.contains(outer.id)) {
        if (error) *error = QStringLiteral("Region identities must be unique.");
        return false;
    }
    identities.insert(outer.id);
    for (const PathNode& node : outer.nodes) {
        if (identities.contains(node.id)) {
            if (error) *error = QStringLiteral("Region identities must be unique.");
            return false;
        }
        identities.insert(node.id);
    }
    auto outerPoints = flattenRegionContour(outer, 0.05, work);
    if (!outerPoints.has_value() || !simpleContour(*outerPoints, error, work)) {
        if (!work.isRunning() && error) *error = work.interruptionMessage();
        return false;
    }
    for (int index = 0; index < holes.size(); ++index) {
        const PathGeometry& hole = holes.at(index);
        if (!hole.validate(error) || !hole.closed) {
            if (error && error->isEmpty()) {
                *error = QStringLiteral("Region hole %1 must be closed.").arg(index);
            }
            return false;
        }
        if (identities.contains(hole.id)) {
            if (error) *error = QStringLiteral("Region contains duplicate contour identities.");
            return false;
        }
        identities.insert(hole.id);
        for (const PathNode& node : hole.nodes) {
            if (identities.contains(node.id)) {
                if (error) *error = QStringLiteral("Region contains duplicate node identities.");
                return false;
            }
            identities.insert(node.id);
        }
        const auto holePoints = flattenRegionContour(hole, 0.05, work);
        if (!holePoints.has_value() || !simpleContour(*holePoints, error, work)) {
            if (!work.isRunning() && error) *error = work.interruptionMessage();
            return false;
        }
        for (int pointIndex = 0; pointIndex + 1 < holePoints->size(); ++pointIndex) {
            if (!work.consume()) {
                if (error) *error = work.interruptionMessage();
                return false;
            }
            if (!pointInPolygon(*outerPoints, holePoints->at(pointIndex))) {
                if (error) *error = QStringLiteral("Region hole lies outside or touches the outer contour.");
                return false;
            }
        }
        const int outerEdges = outerPoints->size() - 1;
        const int holeEdges = holePoints->size() - 1;
        for (int a = 0; a < outerEdges; ++a) {
            for (int b = 0; b < holeEdges; ++b) {
                if (!work.consume()) {
                    if (error) *error = work.interruptionMessage();
                    return false;
                }
                if (segmentsIntersect(outerPoints->at(a), outerPoints->at(a + 1),
                                      holePoints->at(b), holePoints->at(b + 1))) {
                    if (error) *error = QStringLiteral("Region hole touches or crosses the outer contour.");
                    return false;
                }
            }
        }
        for (int otherIndex = 0; otherIndex < index; ++otherIndex) {
            const auto otherPoints = flattenRegionContour(holes.at(otherIndex), 0.05, work);
            if (!otherPoints.has_value()) {
                if (!work.isRunning() && error) *error = work.interruptionMessage();
                return false;
            }
            const int otherEdges = otherPoints->size() - 1;
            for (int a = 0; a < holeEdges; ++a) {
                for (int b = 0; b < otherEdges; ++b) {
                    if (!work.consume()) {
                        if (error) *error = work.interruptionMessage();
                        return false;
                    }
                    if (segmentsIntersect(holePoints->at(a), holePoints->at(a + 1),
                                          otherPoints->at(b), otherPoints->at(b + 1))) {
                        if (error) *error = QStringLiteral("Region holes may not overlap or touch.");
                        return false;
                    }
                }
            }
            if (!work.consume()) {
                if (error) *error = work.interruptionMessage();
                return false;
            }
            if (pointInPolygon(*otherPoints, holePoints->at(0))
                || pointInPolygon(*holePoints, otherPoints->at(0))) {
                if (error) *error = QStringLiteral("Nested region holes are unsupported.");
                return false;
            }
        }
    }
    return true;
}

QPainterPath TypographyRegion::toPainterPath() const
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    path.addPath(outer.toPainterPath());
    for (const PathGeometry& hole : holes) {
        path.addPath(hole.toPainterPath());
    }
    return path;
}

TypographyRegion TypographyRegion::duplicatedFresh() const
{
    TypographyRegion result(*this);
    result.id = idWithPrefix(QStringLiteral("region"));
    auto freshContour = [](PathGeometry* contour, const QString& prefix) {
        if (!contour) return;
        contour->id = idWithPrefix(prefix);
        for (PathNode& node : contour->nodes) node.id = idWithPrefix(prefix + QStringLiteral("-node"));
    };
    freshContour(&result.outer, QStringLiteral("region-outer"));
    for (PathGeometry& hole : result.holes) freshContour(&hole, QStringLiteral("region-hole"));
    return result;
}

QJsonObject TypographyRegion::toJson() const
{
    QJsonArray serializedHoles;
    for (const PathGeometry& hole : holes) serializedHoles.append(hole.toJson());
    return {{QStringLiteral("id"), id},
            {QStringLiteral("outer"), outer.toJson()},
            {QStringLiteral("holes"), serializedHoles}};
}

bool TypographyRegion::fromJson(const QJsonObject& object,
                                TypographyRegion* region,
                                QString* error,
                                const WorkControl& work)
{
    if (!region) {
        if (error) *error = QStringLiteral("Region output is null.");
        return false;
    }
    if (!work.consume()) {
        if (error) *error = work.interruptionMessage();
        return false;
    }
    const QJsonValue idValue = object.value(QStringLiteral("id"));
    const QJsonValue outerValue = object.value(QStringLiteral("outer"));
    const QJsonValue holesValue = object.value(QStringLiteral("holes"));
    if (!idValue.isString() || !outerValue.isObject()
        || (!holesValue.isUndefined() && !holesValue.isArray())) {
        if (error) *error = QStringLiteral("Region JSON has missing or invalid contour data.");
        return false;
    }
    TypographyRegion result;
    result.id = idValue.toString();
    QString contourError;
    if (!PathGeometry::fromJson(outerValue.toObject(), &result.outer, &contourError)) {
        if (error) *error = QStringLiteral("Invalid region outer contour: %1").arg(contourError);
        return false;
    }
    for (int nodeIndex = 0; nodeIndex < result.outer.nodes.size(); ++nodeIndex) {
        if (!work.consume()) {
            if (error) *error = work.interruptionMessage();
            return false;
        }
    }
    if (holesValue.isArray()) {
        const QJsonArray holes = holesValue.toArray();
        if (holes.size() > MaximumHoles) {
            if (error) *error = QStringLiteral("Region has too many hole contours.");
            return false;
        }
        for (int index = 0; index < holes.size(); ++index) {
            if (!holes.at(index).isObject()) {
                if (error) *error = QStringLiteral("Region hole %1 is not an object.").arg(index);
                return false;
            }
            PathGeometry hole;
            if (!PathGeometry::fromJson(holes.at(index).toObject(), &hole, &contourError)) {
                if (error) *error = QStringLiteral("Invalid region hole %1: %2").arg(index).arg(contourError);
                return false;
            }
            for (int nodeIndex = 0; nodeIndex < hole.nodes.size(); ++nodeIndex) {
                if (!work.consume()) {
                    if (error) *error = work.interruptionMessage();
                    return false;
                }
            }
            result.holes.push_back(std::move(hole));
        }
    }
    if (!result.validate(error, work)) return false;
    *region = std::move(result);
    return true;
}

QVector<RegionInterval> regionIntervalsAtY(const TypographyRegion& region,
                                            qreal y,
                                            const WorkControl& work)
{
    const auto flattened = flattenTypographyRegion(region, 0.05, work);
    if (!flattened.has_value() || !work.isRunning()) return {};
    return regionIntervalsAtY(*flattened, y, work);
}

QVector<RegionInterval> regionIntervalsAtY(const FlattenedTypographyRegion& region,
                                            qreal y,
                                            const WorkControl& work)
{
    if (!std::isfinite(y) || !work.consume()) return {};
    QVector<RegionInterval> result = contourIntervals(region.outer, y, work);
    for (const QVector<QPointF>& hole : region.holes) {
        result = subtractIntervals(result, contourIntervals(hole, y, work));
        if (result.isEmpty()) break;
    }
    std::sort(result.begin(), result.end(), [](const RegionInterval& left,
                                               const RegionInterval& right) {
        if (!qFuzzyCompare(left.left, right.left)) return left.left < right.left;
        return left.right < right.right;
    });
    return result;
}

} // namespace vt
