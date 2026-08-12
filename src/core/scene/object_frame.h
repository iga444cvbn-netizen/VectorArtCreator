#pragma once

#include "core/document/document.h"

#include <QPolygonF>
#include <QRectF>
#include <QTransform>

namespace vt {

// The one authoritative bridge between object-local and page coordinates.
// position deliberately retains the legacy Phase 3 meaning: it is a page
// translation, while pivotLocal is the stable local centre used for rotation
// and scale.  Keeping that matrix preserves existing project appearance.
class ObjectFrame {
public:
    quint64 spatialRevision = 0;
    QRectF baseLocalBounds;
    QRectF currentLocalBounds;
    QPointF pivotLocal;
    QTransform localToPage;
    QTransform pageToLocal;

    [[nodiscard]] static ObjectFrame fromTransform(const ObjectTransform& transform,
                                                    const QRectF& baseLocalBounds,
                                                    const QRectF& currentLocalBounds = {});
    [[nodiscard]] QPointF pagePointToLocal(const QPointF& point) const;
    [[nodiscard]] QPointF localPointToPage(const QPointF& point) const;
    [[nodiscard]] QPointF pageVectorToLocal(const QPointF& vector) const;
    [[nodiscard]] QPointF localVectorToPage(const QPointF& vector) const;
    [[nodiscard]] QPolygonF orientedPageQuad() const;
    [[nodiscard]] QRectF pageAabb() const;

    // Strokes store a local circular radius.  The conversion uses the
    // equivalent-area scale (sqrt(|det(linear)|)); under non-uniform scale its
    // page footprint is intentionally an ellipse, rather than an unlabelled
    // average-scale approximation.
    [[nodiscard]] qreal pageRadiusToLocalEquivalentArea(qreal pageRadius) const;
};

} // namespace vt
