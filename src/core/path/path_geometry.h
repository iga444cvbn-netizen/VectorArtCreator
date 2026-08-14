#pragma once

#include <QJsonObject>
#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QVector>

namespace vt {

// Paths are deliberately kept independent from QPainterPath.  QPainterPath is
// only a derived representation used by rendering, hit testing, and the
// bounded arc-length sampler.
enum class PathOverflowMode {
    Clip,
};

[[nodiscard]] QString pathOverflowModeToString(PathOverflowMode mode);
[[nodiscard]] bool pathOverflowModeFromString(const QString& value,
                                               PathOverflowMode* mode);

struct PathNode {
    QString id;
    QPointF anchor;
    QPointF incomingHandle;
    QPointF outgoingHandle;
    bool hasIncomingHandle = false;
    bool hasOutgoingHandle = false;

    [[nodiscard]] bool isFinite() const;
    friend bool operator==(const PathNode&, const PathNode&) = default;
};

struct PathGeometry {
    static constexpr int MaximumNodes = 4096;
    static constexpr qreal MaximumCoordinate = 1.0e9;

    QString id;
    QVector<PathNode> nodes;
    bool closed = false;

    [[nodiscard]] static PathGeometry makeDefault(qreal width,
                                                   qreal baselineY = 0.0);
    [[nodiscard]] int segmentCount() const;
    [[nodiscard]] int indexOfNode(const QString& nodeId) const;
    [[nodiscard]] bool isCubicSegment(int segmentIndex) const;
    [[nodiscard]] bool segmentControlPoints(int segmentIndex,
                                             QPointF* p0,
                                             QPointF* p1,
                                             QPointF* p2,
                                             QPointF* p3) const;
    // Segment editing operates on the actual cubic control points. Splitting
    // uses de Casteljau subdivision and therefore preserves the rendered
    // curve, including the closed-path seam.
    [[nodiscard]] bool splitSegment(int segmentIndex,
                                    qreal t,
                                    const QString& newNodeId);
    [[nodiscard]] bool convertSegmentToCubic(int segmentIndex);
    [[nodiscard]] bool convertSegmentToLine(int segmentIndex);
    [[nodiscard]] QPainterPath toPainterPath() const;

    // Reversal preserves every node identity and is involutive: calling it
    // twice restores the exact semantic node/handle ordering.
    void reverseDirection();

    [[nodiscard]] bool validate(QString* error = nullptr) const;
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static bool fromJson(const QJsonObject& object,
                                       PathGeometry* path,
                                       QString* error = nullptr);

    friend bool operator==(const PathGeometry&, const PathGeometry&) = default;
};

struct PathTypographyProperties {
    bool enabled = false;
    QString pathId;
    qreal startOffset = 0.0;
    qreal baselineOffset = 0.0;
    bool reverse = false;
    bool flip = false;
    bool followTangent = true;
    PathOverflowMode overflow = PathOverflowMode::Clip;

    [[nodiscard]] bool isFinite() const;
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static bool fromJson(const QJsonObject& object,
                                       PathTypographyProperties* properties,
                                       QString* error = nullptr);

    friend bool operator==(const PathTypographyProperties&, const PathTypographyProperties&) = default;
};

using PathLayoutSettings = PathTypographyProperties;
using TextPath = PathGeometry;

} // namespace vt
