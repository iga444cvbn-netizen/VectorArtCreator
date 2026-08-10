#include "core/scene/object_frame.h"

#include <cmath>

namespace vt {

ObjectFrame ObjectFrame::fromTransform(const ObjectTransform& transform,
                                       const QRectF& baseBounds,
                                       const QRectF& currentBounds)
{
    ObjectFrame frame;
    frame.baseLocalBounds = baseBounds;
    frame.currentLocalBounds = currentBounds.isNull() ? baseBounds : currentBounds;
    frame.pivotLocal = transform.hasPivot ? transform.pivotLocal : baseBounds.center();
    const qreal scaleX = ObjectTransform::clampScale(transform.scale.x());
    const qreal scaleY = ObjectTransform::clampScale(transform.scale.y());
    QTransform matrix;
    matrix.translate(transform.position.x(), transform.position.y());
    matrix.translate(frame.pivotLocal.x(), frame.pivotLocal.y());
    matrix.rotate(transform.rotation);
    matrix.scale(scaleX, scaleY);
    matrix.translate(-frame.pivotLocal.x(), -frame.pivotLocal.y());
    frame.localToPage = matrix;
    bool invertible = false;
    frame.pageToLocal = matrix.inverted(&invertible);
    if (!invertible) {
        frame.pageToLocal = QTransform();
    }
    return frame;
}

QPointF ObjectFrame::pagePointToLocal(const QPointF& point) const { return pageToLocal.map(point); }
QPointF ObjectFrame::localPointToPage(const QPointF& point) const { return localToPage.map(point); }

QPointF ObjectFrame::pageVectorToLocal(const QPointF& vector) const
{
    return pageToLocal.map(vector) - pageToLocal.map(QPointF());
}

QPointF ObjectFrame::localVectorToPage(const QPointF& vector) const
{
    return localToPage.map(vector) - localToPage.map(QPointF());
}

QPolygonF ObjectFrame::orientedPageQuad() const
{
    const QRectF bounds = currentLocalBounds;
    return {localPointToPage(bounds.topLeft()), localPointToPage(bounds.topRight()),
            localPointToPage(bounds.bottomRight()), localPointToPage(bounds.bottomLeft())};
}

QRectF ObjectFrame::pageAabb() const { return localToPage.mapRect(currentLocalBounds); }

qreal ObjectFrame::pageRadiusToLocalEquivalentArea(qreal pageRadius) const
{
    const QPointF x = localVectorToPage(QPointF(1.0, 0.0));
    const QPointF y = localVectorToPage(QPointF(0.0, 1.0));
    const qreal determinant = std::abs(x.x() * y.y() - x.y() * y.x());
    return pageRadius / qMax<qreal>(0.0001, std::sqrt(determinant));
}

} // namespace vt
