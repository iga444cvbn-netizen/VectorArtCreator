#pragma once

#include "core/geometry/vector_geometry.h"
#include "core/scene/scene_geometry.h"

#include <QString>

class QGraphicsProxyWidget;

namespace vt::test {

[[nodiscard]] bool isFinite(const QPointF& point);
[[nodiscard]] bool isFinite(const QRectF& rect);
[[nodiscard]] bool isFinite(const QTransform& transform);
[[nodiscard]] bool hasFiniteGeometry(const VectorGeometry& geometry, QString* error = nullptr);
[[nodiscard]] bool approximatelyEqual(const QPointF& actual, const QPointF& expected,
                                      qreal tolerance = 1.0);

// Verifies the native editing proxy by independently mapping non-collinear
// points through ObjectFrame and view coordinates. It intentionally permits
// editor padding, but catches detached overlays separated by hundreds of pixels.
[[nodiscard]] bool editorOverlayAttached(const QGraphicsProxyWidget* proxy,
                                         const SceneObjectGeometry& object,
                                         const QTransform& documentToView,
                                         QString* error = nullptr,
                                         qreal tolerance = 8.0);

} // namespace vt::test
