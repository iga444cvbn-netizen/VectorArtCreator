#include "tests/support/geometry_assertions.h"

#include <QGraphicsProxyWidget>
#include <QLineF>
#include <QPolygonF>

#include <cmath>

namespace vt::test {

bool isFinite(const QPointF& point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool isFinite(const QRectF& rect)
{
    return std::isfinite(rect.x()) && std::isfinite(rect.y())
        && std::isfinite(rect.width()) && std::isfinite(rect.height());
}

bool isFinite(const QTransform& transform)
{
    return std::isfinite(transform.m11()) && std::isfinite(transform.m12())
        && std::isfinite(transform.m13()) && std::isfinite(transform.m21())
        && std::isfinite(transform.m22()) && std::isfinite(transform.m23())
        && std::isfinite(transform.m31()) && std::isfinite(transform.m32())
        && std::isfinite(transform.m33());
}

bool hasFiniteGeometry(const VectorGeometry& geometry, QString* error)
{
    if (!isFinite(geometry.bounds) || !isFinite(geometry.referenceBounds)
        || !std::isfinite(geometry.referenceHeight)) {
        if (error) *error = QStringLiteral("non-finite geometry bounds");
        return false;
    }
    for (const GeometryPiece& piece : geometry.pieces) {
        if (!isFinite(piece.anchor) || !isFinite(piece.originalAnchor)
            || (piece.hasEffectReferenceAnchor && !isFinite(piece.effectReferenceAnchor))
            || (piece.hasEffectReferenceProgress
                && (!std::isfinite(piece.effectReferenceProgress)
                    || piece.effectReferenceProgress < -1.0e-6
                    || piece.effectReferenceProgress > 1.000001))
            || !std::isfinite(piece.opacityMultiplier)
            || piece.opacityMultiplier < -1.0e-6 || piece.opacityMultiplier > 1.000001) {
            if (error) *error = QStringLiteral("non-finite geometry piece metadata");
            return false;
        }
        for (int index = 0; index < piece.path.elementCount(); ++index) {
            const auto element = piece.path.elementAt(index);
            if (!std::isfinite(element.x) || !std::isfinite(element.y)) {
                if (error) *error = QStringLiteral("non-finite path element %1").arg(index);
                return false;
            }
        }
    }
    return true;
}

bool approximatelyEqual(const QPointF& actual, const QPointF& expected, qreal tolerance)
{
    return QLineF(actual, expected).length() <= tolerance;
}

bool editorOverlayAttached(const QGraphicsProxyWidget* proxy,
                           const SceneObjectGeometry& object,
                           const QTransform& documentToView,
                           QString* error,
                           qreal tolerance)
{
    if (!proxy) {
        if (error) *error = QStringLiteral("native text editor proxy is missing");
        return false;
    }
    const QRectF local = object.frame.baseLocalBounds.isEmpty()
        ? QRectF(0.0, 0.0, 120.0, 36.0) : object.frame.baseLocalBounds;
    QTransform localOffset;
    localOffset.translate(local.x(), local.y());
    const QTransform expectedTransform = documentToView * object.frame.localToPage * localOffset;
    const QTransform actualTransform = proxy->transform();
    if (!isFinite(actualTransform) || !isFinite(expectedTransform)) {
        if (error) *error = QStringLiteral("editor or expected overlay transform is non-finite");
        return false;
    }
    const QPointF actualOrigin = actualTransform.map(QPointF());
    const QPointF expectedOrigin = expectedTransform.map(QPointF());
    if (!approximatelyEqual(actualOrigin, expectedOrigin, tolerance)
        || !approximatelyEqual(actualTransform.map(QPointF(1.0, 0.0)),
                               expectedTransform.map(QPointF(1.0, 0.0)), tolerance)
        || !approximatelyEqual(actualTransform.map(QPointF(0.0, 1.0)),
                               expectedTransform.map(QPointF(0.0, 1.0)), tolerance)) {
        if (error) {
            *error = QStringLiteral("editor detached: expected origin (%1,%2), actual (%3,%4)")
                .arg(expectedOrigin.x()).arg(expectedOrigin.y())
                .arg(actualOrigin.x()).arg(actualOrigin.y());
        }
        return false;
    }
    return true;
}

} // namespace vt::test
