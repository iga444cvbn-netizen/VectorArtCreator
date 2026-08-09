#include "core/geometry/vector_geometry.h"

namespace vt {

void VectorGeometry::setReferenceBounds(const QRectF& value)
{
    referenceBounds = value;
    referenceHeight = qMax<qreal>(1.0, value.height());
}

void VectorGeometry::recomputeBounds()
{
    QRectF computed;
    bool hasBounds = false;

    for (const GeometryPiece& piece : pieces) {
        if (piece.path.isEmpty()) {
            continue;
        }
        const QRectF pieceBounds = piece.path.boundingRect();
        if (!hasBounds) {
            computed = pieceBounds;
            hasBounds = true;
        } else {
            computed = computed.united(pieceBounds);
        }
    }

    bounds = hasBounds ? computed : referenceBounds;
}

void VectorGeometry::translatePiece(int index, const QPointF& delta)
{
    if (index < 0 || index >= pieces.size()) {
        return;
    }
    QTransform transform;
    Q_UNUSED(transform.translate(delta.x(), delta.y()));
    transformPiece(index, transform);
}

void VectorGeometry::transformPiece(int index, const QTransform& transform)
{
    if (index < 0 || index >= pieces.size()) {
        return;
    }
    GeometryPiece& piece = pieces[index];
    piece.path = transform.map(piece.path);
    piece.anchor = transform.map(piece.anchor);
}

void VectorGeometry::transformAll(const QTransform& transform)
{
    for (GeometryPiece& piece : pieces) {
        piece.path = transform.map(piece.path);
        piece.anchor = transform.map(piece.anchor);
        piece.originalAnchor = transform.map(piece.originalAnchor);
    }
    if (!referenceBounds.isEmpty()) {
        referenceBounds = transform.map(referenceBounds).boundingRect();
    }
    recomputeBounds();
    referenceHeight = qMax<qreal>(1.0, referenceBounds.height());
}

QPainterPath VectorGeometry::combinedPath() const
{
    QPainterPath result;
    result.setFillRule(Qt::WindingFill);
    for (const GeometryPiece& piece : pieces) {
        result.addPath(piece.path);
    }
    return result;
}

bool VectorGeometry::hasVisibleGeometry() const
{
    for (const GeometryPiece& piece : pieces) {
        if (!piece.path.isEmpty()) {
            return true;
        }
    }
    return false;
}

} // namespace vt
