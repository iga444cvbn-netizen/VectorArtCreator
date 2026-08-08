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
    QPointF anchor;
    QPointF originalAnchor;
};

class VectorGeometry {
public:
    QVector<GeometryPiece> pieces;
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
