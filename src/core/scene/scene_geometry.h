#pragma once

#include "core/geometry/vector_geometry.h"
#include "core/scene/object_frame.h"

#include <QColor>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QSizeF>
#include <QVector>

namespace vt {

struct SceneObjectGeometry {
    QString objectId;
    QString pageId;
    QString layerId;
    QString sourceText;
    VectorGeometry geometry;
    QColor fill;
    QRectF visualBounds;
    // visualBounds is retained for broad-phase compatibility only.  Frame is
    // the authoritative selection and input coordinate contract.
    ObjectFrame frame;
    bool visible = true;
    bool locked = false;
    QString warning;
    QString error;
};

class SceneGeometry {
public:
    QString pageId;
    QSizeF pageSize;
    QColor pageBackground;
    QVector<SceneObjectGeometry> objects;
    QRectF bounds;

    void recomputeBounds();
    [[nodiscard]] const SceneObjectGeometry* objectById(const QString& objectId) const;
    [[nodiscard]] SceneObjectGeometry* objectById(const QString& objectId);
    [[nodiscard]] QPainterPath combinedPath() const;
    [[nodiscard]] bool hasVisibleGeometry() const;
};

} // namespace vt
