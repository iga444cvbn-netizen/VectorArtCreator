#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QTransform>
#include <QVector>

namespace vt {

struct GeometryPiece {
    QPainterPath path;
    int sourceGlyphIndex = -1;
    int sourceClusterStart = -1;
    int sourceClusterLength = 1;
    int sourceLineIndex = 0;
    QPointF anchor;
    // Immutable source-layout anchor. Effects may move anchor, but never this
    // reference used by later effect normalization/order semantics.
    QPointF originalAnchor;
};

class VectorGeometry {
public:
    QVector<GeometryPiece> pieces;
    // Immutable base-local normalization bounds; current geometry bounds are
    // tracked separately in bounds.
    QRectF referenceBounds;
    QRectF bounds;
    qreal referenceHeight = 1.0;

    void setReferenceBounds(const QRectF& value);
    void recomputeBounds();
    void translatePiece(int index, const QPointF& delta);
    void transformPiece(int index, const QTransform& transform);
    void transformAll(const QTransform& transform);

    [[nodiscard]] QPainterPath combinedPath() const;
    [[nodiscard]] bool hasVisibleGeometry() const;
};

} // namespace vt
