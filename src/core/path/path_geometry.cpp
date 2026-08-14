#include "core/path/path_geometry.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace vt {
namespace {

QString newPathId(const QString& prefix)
{
    return prefix + QLatin1Char('-')
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString pathError(const QString& message, QString* error)
{
    if (error) {
        *error = message;
    }
    return message;
}

bool finitePoint(const QPointF& point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool boundedPoint(const QPointF& point)
{
    return finitePoint(point)
        && std::abs(point.x()) <= PathGeometry::MaximumCoordinate
        && std::abs(point.y()) <= PathGeometry::MaximumCoordinate;
}

QJsonArray pointToJson(const QPointF& point)
{
    return {point.x(), point.y()};
}

bool pointFromJson(const QJsonValue& value, QPointF* point)
{
    if (!point || !value.isArray()) {
        return false;
    }
    const QJsonArray array = value.toArray();
    if (array.size() != 2) {
        return false;
    }
    if (!array.at(0).isDouble() || !array.at(1).isDouble()) {
        return false;
    }
    const qreal x = array.at(0).toDouble(std::numeric_limits<double>::quiet_NaN());
    const qreal y = array.at(1).toDouble(std::numeric_limits<double>::quiet_NaN());
    const QPointF parsed(x, y);
    if (!boundedPoint(parsed)) {
        return false;
    }
    *point = parsed;
    return true;
}

} // namespace

QString pathOverflowModeToString(PathOverflowMode mode)
{
    Q_UNUSED(mode);
    return QStringLiteral("clip");
}

bool pathOverflowModeFromString(const QString& value, PathOverflowMode* mode)
{
    if (!mode || value.trimmed().compare(QStringLiteral("clip"), Qt::CaseInsensitive) != 0) {
        return false;
    }
    *mode = PathOverflowMode::Clip;
    return true;
}

bool PathNode::isFinite() const
{
    return boundedPoint(anchor) && boundedPoint(incomingHandle) && boundedPoint(outgoingHandle);
}

PathGeometry PathGeometry::makeDefault(qreal width, qreal baselineY)
{
    PathGeometry path;
    path.id = newPathId(QStringLiteral("path"));
    const qreal boundedWidth = std::isfinite(width)
        ? qBound<qreal>(1.0, width, MaximumCoordinate) : 1.0;
    const qreal boundedY = std::isfinite(baselineY)
        ? qBound<qreal>(-MaximumCoordinate, baselineY, MaximumCoordinate) : 0.0;
    PathNode first;
    first.id = path.id + QStringLiteral("-node-0");
    first.anchor = QPointF(0.0, boundedY);
    PathNode second;
    second.id = path.id + QStringLiteral("-node-1");
    second.anchor = QPointF(boundedWidth, boundedY);
    path.nodes = {first, second};
    return path;
}

int PathGeometry::segmentCount() const
{
    if (nodes.size() < 2) {
        return 0;
    }
    return closed ? nodes.size() : nodes.size() - 1;
}

int PathGeometry::indexOfNode(const QString& nodeId) const
{
    if (nodeId.isEmpty()) {
        return -1;
    }
    for (int index = 0; index < nodes.size(); ++index) {
        if (nodes.at(index).id == nodeId) {
            return index;
        }
    }
    return -1;
}

bool PathGeometry::isCubicSegment(int segmentIndex) const
{
    if (segmentIndex < 0 || segmentIndex >= segmentCount()) {
        return false;
    }
    const int startIndex = segmentIndex;
    const int endIndex = (segmentIndex + 1) % nodes.size();
    return nodes.at(startIndex).hasOutgoingHandle || nodes.at(endIndex).hasIncomingHandle;
}

bool PathGeometry::segmentControlPoints(int segmentIndex,
                                        QPointF* p0,
                                        QPointF* p1,
                                        QPointF* p2,
                                        QPointF* p3) const
{
    if (!p0 || !p1 || !p2 || !p3 || segmentIndex < 0 || segmentIndex >= segmentCount()) {
        return false;
    }
    const int endIndex = (segmentIndex + 1) % nodes.size();
    const PathNode& start = nodes.at(segmentIndex);
    const PathNode& end = nodes.at(endIndex);
    *p0 = start.anchor;
    *p1 = start.hasOutgoingHandle ? start.outgoingHandle : start.anchor;
    *p2 = end.hasIncomingHandle ? end.incomingHandle : end.anchor;
    *p3 = end.anchor;
    return true;
}

bool PathGeometry::splitSegment(int segmentIndex,
                                qreal t,
                                const QString& newNodeId)
{
    if (segmentIndex < 0 || segmentIndex >= segmentCount()
        || newNodeId.trimmed().isEmpty() || indexOfNode(newNodeId) >= 0
        || !std::isfinite(t)) {
        return false;
    }
    constexpr qreal endpointEpsilon = 1.0e-6;
    const qreal splitT = qBound(endpointEpsilon, t, 1.0 - endpointEpsilon);

    QPointF p0;
    QPointF p1;
    QPointF p2;
    QPointF p3;
    if (!segmentControlPoints(segmentIndex, &p0, &p1, &p2, &p3)) {
        return false;
    }

    const int endIndex = (segmentIndex + 1) % nodes.size();
    const bool cubic = isCubicSegment(segmentIndex);
    const QPointF newAnchor = cubic
        ? ([&] {
              const QPointF p01 = p0 * (1.0 - splitT) + p1 * splitT;
              const QPointF p12 = p1 * (1.0 - splitT) + p2 * splitT;
              const QPointF p23 = p2 * (1.0 - splitT) + p3 * splitT;
              const QPointF p012 = p01 * (1.0 - splitT) + p12 * splitT;
              const QPointF p123 = p12 * (1.0 - splitT) + p23 * splitT;
              return p012 * (1.0 - splitT) + p123 * splitT;
          }())
        : p0 * (1.0 - splitT) + p3 * splitT;

    PathNode inserted;
    inserted.id = newNodeId;
    inserted.anchor = newAnchor;

    if (cubic) {
        const QPointF p01 = p0 * (1.0 - splitT) + p1 * splitT;
        const QPointF p12 = p1 * (1.0 - splitT) + p2 * splitT;
        const QPointF p23 = p2 * (1.0 - splitT) + p3 * splitT;
        const QPointF p012 = p01 * (1.0 - splitT) + p12 * splitT;
        const QPointF p123 = p12 * (1.0 - splitT) + p23 * splitT;
        nodes[segmentIndex].outgoingHandle = p01;
        nodes[segmentIndex].hasOutgoingHandle = true;
        inserted.incomingHandle = p012;
        inserted.hasIncomingHandle = true;
        inserted.outgoingHandle = p123;
        inserted.hasOutgoingHandle = true;
        nodes[endIndex].incomingHandle = p23;
        nodes[endIndex].hasIncomingHandle = true;
    }

    // For the closed seam, segment N-1 ends at node 0, so the new node is
    // appended after node N-1 and before the implicit wrap to node 0.
    const int insertIndex = segmentIndex + 1;
    nodes.insert(insertIndex, inserted);
    return true;
}

bool PathGeometry::convertSegmentToCubic(int segmentIndex)
{
    if (segmentIndex < 0 || segmentIndex >= segmentCount()) {
        return false;
    }
    const int endIndex = (segmentIndex + 1) % nodes.size();
    if (isCubicSegment(segmentIndex)) {
        // A partially specified cubic is completed deterministically while an
        // existing cubic is left untouched. Handles on adjacent segments are
        // never changed here.
        bool changed = false;
        if (!nodes[segmentIndex].hasOutgoingHandle) {
            nodes[segmentIndex].outgoingHandle = nodes[segmentIndex].anchor
                + (nodes[endIndex].anchor - nodes[segmentIndex].anchor) / 3.0;
            nodes[segmentIndex].hasOutgoingHandle = true;
            changed = true;
        }
        if (!nodes[endIndex].hasIncomingHandle) {
            nodes[endIndex].incomingHandle = nodes[endIndex].anchor
                - (nodes[endIndex].anchor - nodes[segmentIndex].anchor) / 3.0;
            nodes[endIndex].hasIncomingHandle = true;
            changed = true;
        }
        return changed;
    }
    nodes[segmentIndex].outgoingHandle = nodes[segmentIndex].anchor
        + (nodes[endIndex].anchor - nodes[segmentIndex].anchor) / 3.0;
    nodes[segmentIndex].hasOutgoingHandle = true;
    nodes[endIndex].incomingHandle = nodes[endIndex].anchor
        - (nodes[endIndex].anchor - nodes[segmentIndex].anchor) / 3.0;
    nodes[endIndex].hasIncomingHandle = true;
    return true;
}

bool PathGeometry::convertSegmentToLine(int segmentIndex)
{
    if (segmentIndex < 0 || segmentIndex >= segmentCount()) {
        return false;
    }
    const int endIndex = (segmentIndex + 1) % nodes.size();
    const bool changed = nodes[segmentIndex].hasOutgoingHandle
        || nodes[endIndex].hasIncomingHandle
        || nodes[segmentIndex].outgoingHandle != QPointF()
        || nodes[endIndex].incomingHandle != QPointF();
    nodes[segmentIndex].hasOutgoingHandle = false;
    nodes[segmentIndex].outgoingHandle = QPointF();
    nodes[endIndex].hasIncomingHandle = false;
    nodes[endIndex].incomingHandle = QPointF();
    return changed;
}

QPainterPath PathGeometry::toPainterPath() const
{
    QPainterPath result;
    result.setFillRule(Qt::WindingFill);
    if (nodes.isEmpty()) {
        return result;
    }
    result.moveTo(nodes.front().anchor);
    for (int segment = 0; segment < segmentCount(); ++segment) {
        QPointF p0;
        QPointF p1;
        QPointF p2;
        QPointF p3;
        if (!segmentControlPoints(segment, &p0, &p1, &p2, &p3)) {
            continue;
        }
        if (isCubicSegment(segment)) {
            result.cubicTo(p1, p2, p3);
        } else {
            result.lineTo(p3);
        }
    }
    if (closed && nodes.size() >= 2) {
        result.closeSubpath();
    }
    return result;
}

void PathGeometry::reverseDirection()
{
    std::reverse(nodes.begin(), nodes.end());
    for (PathNode& node : nodes) {
        std::swap(node.incomingHandle, node.outgoingHandle);
        std::swap(node.hasIncomingHandle, node.hasOutgoingHandle);
    }
}

bool PathGeometry::validate(QString* error) const
{
    if (id.trimmed().isEmpty()) {
        pathError(QStringLiteral("Path has no stable identity."), error);
        return false;
    }
    if (nodes.size() < 2 || nodes.size() > MaximumNodes) {
        pathError(QStringLiteral("Path node count is outside the supported range."), error);
        return false;
    }
    if (closed && nodes.size() < 2) {
        pathError(QStringLiteral("A closed path requires at least two nodes."), error);
        return false;
    }
    QSet<QString> nodeIds;
    for (int index = 0; index < nodes.size(); ++index) {
        const PathNode& node = nodes.at(index);
        if (node.id.trimmed().isEmpty() || nodeIds.contains(node.id)) {
            pathError(QStringLiteral("Path contains a missing or duplicate node ID at node %1.")
                          .arg(index), error);
            return false;
        }
        if (!node.isFinite()) {
            pathError(QStringLiteral("Path node %1 contains non-finite or excessive coordinates.")
                          .arg(index), error);
            return false;
        }
        nodeIds.insert(node.id);
    }
    return true;
}

QJsonObject PathGeometry::toJson() const
{
    QJsonArray serializedNodes;
    for (const PathNode& node : nodes) {
        QJsonObject serialized;
        serialized.insert(QStringLiteral("id"), node.id);
        serialized.insert(QStringLiteral("anchor"), pointToJson(node.anchor));
        serialized.insert(QStringLiteral("incomingHandle"), pointToJson(node.incomingHandle));
        serialized.insert(QStringLiteral("outgoingHandle"), pointToJson(node.outgoingHandle));
        serialized.insert(QStringLiteral("hasIncomingHandle"), node.hasIncomingHandle);
        serialized.insert(QStringLiteral("hasOutgoingHandle"), node.hasOutgoingHandle);
        serializedNodes.append(serialized);
    }
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("closed"), closed},
        {QStringLiteral("nodes"), serializedNodes},
    };
}

bool PathGeometry::fromJson(const QJsonObject& object,
                            PathGeometry* path,
                            QString* error)
{
    if (!path) {
        pathError(QStringLiteral("Path output is null."), error);
        return false;
    }
    PathGeometry result;
    const QJsonValue idValue = object.value(QStringLiteral("id"));
    if (!idValue.isString()) {
        pathError(QStringLiteral("Path ID must be a string."), error);
        return false;
    }
    result.id = idValue.toString();
    const QJsonValue closedValue = object.value(QStringLiteral("closed"));
    if (!closedValue.isUndefined() && !closedValue.isBool()) {
        pathError(QStringLiteral("Path closed flag must be a boolean."), error);
        return false;
    }
    result.closed = closedValue.toBool(false);
    const QJsonValue nodesValue = object.value(QStringLiteral("nodes"));
    if (!nodesValue.isArray()) {
        pathError(QStringLiteral("Path nodes must be an array."), error);
        return false;
    }
    const QJsonArray serializedNodes = nodesValue.toArray();
    if (serializedNodes.size() < 2 || serializedNodes.size() > MaximumNodes) {
        pathError(QStringLiteral("Path node count is outside the supported range."), error);
        return false;
    }
    result.nodes.reserve(serializedNodes.size());
    for (int index = 0; index < serializedNodes.size(); ++index) {
        const QJsonValue value = serializedNodes.at(index);
        if (!value.isObject()) {
            pathError(QStringLiteral("Path node %1 is not an object.").arg(index), error);
            return false;
        }
        const QJsonObject serialized = value.toObject();
        PathNode node;
        const QJsonValue nodeIdValue = serialized.value(QStringLiteral("id"));
        if (!nodeIdValue.isString()) {
            pathError(QStringLiteral("Path node %1 ID must be a string.").arg(index), error);
            return false;
        }
        node.id = nodeIdValue.toString();
        if (!pointFromJson(serialized.value(QStringLiteral("anchor")), &node.anchor)
            || !pointFromJson(serialized.value(QStringLiteral("incomingHandle")), &node.incomingHandle)
            || !pointFromJson(serialized.value(QStringLiteral("outgoingHandle")), &node.outgoingHandle)) {
            pathError(QStringLiteral("Path node %1 contains invalid coordinates.").arg(index), error);
            return false;
        }
        const QJsonValue incomingValue = serialized.value(QStringLiteral("hasIncomingHandle"));
        const QJsonValue outgoingValue = serialized.value(QStringLiteral("hasOutgoingHandle"));
        if ((!incomingValue.isUndefined() && !incomingValue.isBool())
            || (!outgoingValue.isUndefined() && !outgoingValue.isBool())) {
            pathError(QStringLiteral("Path node %1 handle flags must be boolean.").arg(index), error);
            return false;
        }
        node.hasIncomingHandle = incomingValue.toBool(false);
        node.hasOutgoingHandle = outgoingValue.toBool(false);
        result.nodes.push_back(node);
    }
    if (!result.validate(error)) {
        return false;
    }
    *path = std::move(result);
    return true;
}

bool PathTypographyProperties::isFinite() const
{
    return std::isfinite(startOffset) && std::isfinite(baselineOffset)
        && std::abs(startOffset) <= PathGeometry::MaximumCoordinate
        && std::abs(baselineOffset) <= PathGeometry::MaximumCoordinate;
}

QJsonObject PathTypographyProperties::toJson() const
{
    return {
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("pathId"), pathId},
        {QStringLiteral("startOffset"), startOffset},
        {QStringLiteral("baselineOffset"), baselineOffset},
        {QStringLiteral("reverse"), reverse},
        {QStringLiteral("flip"), flip},
        {QStringLiteral("followTangent"), followTangent},
        {QStringLiteral("overflow"), pathOverflowModeToString(overflow)},
    };
}

bool PathTypographyProperties::fromJson(const QJsonObject& object,
                                        PathTypographyProperties* properties,
                                        QString* error)
{
    if (!properties) {
        pathError(QStringLiteral("Path typography output is null."), error);
        return false;
    }
    PathTypographyProperties result;
    const QJsonValue enabledValue = object.value(QStringLiteral("enabled"));
    const QJsonValue pathIdValue = object.value(QStringLiteral("pathId"));
    const QJsonValue startOffsetValue = object.value(QStringLiteral("startOffset"));
    const QJsonValue baselineOffsetValue = object.value(QStringLiteral("baselineOffset"));
    const QJsonValue reverseValue = object.value(QStringLiteral("reverse"));
    const QJsonValue flipValue = object.value(QStringLiteral("flip"));
    const QJsonValue tangentValue = object.value(QStringLiteral("followTangent"));
    if ((!enabledValue.isUndefined() && !enabledValue.isBool())
        || (!pathIdValue.isUndefined() && !pathIdValue.isString())
        || (!startOffsetValue.isUndefined() && !startOffsetValue.isDouble())
        || (!baselineOffsetValue.isUndefined() && !baselineOffsetValue.isDouble())
        || (!reverseValue.isUndefined() && !reverseValue.isBool())
        || (!flipValue.isUndefined() && !flipValue.isBool())
        || (!tangentValue.isUndefined() && !tangentValue.isBool())) {
        pathError(QStringLiteral("Path typography contains a field with the wrong JSON type."), error);
        return false;
    }
    result.enabled = enabledValue.toBool(false);
    result.pathId = pathIdValue.toString();
    result.startOffset = startOffsetValue.toDouble(0.0);
    result.baselineOffset = baselineOffsetValue.toDouble(0.0);
    result.reverse = reverseValue.toBool(false);
    result.flip = flipValue.toBool(false);
    result.followTangent = tangentValue.isUndefined() ? true : tangentValue.toBool();
    const QJsonValue overflowValue = object.value(QStringLiteral("overflow"));
    if (!overflowValue.isUndefined() && !overflowValue.isString()) {
        pathError(QStringLiteral("Path typography overflow must be a string."), error);
        return false;
    }
    const QString overflow = overflowValue.isUndefined()
        ? QStringLiteral("clip") : overflowValue.toString();
    if (!pathOverflowModeFromString(overflow, &result.overflow)) {
        pathError(QStringLiteral("Path typography contains an invalid overflow mode."), error);
        return false;
    }
    if (!result.isFinite()) {
        pathError(QStringLiteral("Path typography contains non-finite or excessive offsets."), error);
        return false;
    }
    if (result.enabled && result.pathId.isEmpty()) {
        pathError(QStringLiteral("Enabled path typography has no path reference."), error);
        return false;
    }
    *properties = std::move(result);
    return true;
}

} // namespace vt
