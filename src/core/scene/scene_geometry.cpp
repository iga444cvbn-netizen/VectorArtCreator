#include "core/scene/scene_geometry.h"

namespace vt {

void SceneGeometry::recomputeBounds()
{
    QRectF computed;
    bool hasBounds = false;
    for (const SceneObjectGeometry& object : objects) {
        if (!object.visible || object.geometry.bounds.isEmpty()) {
            continue;
        }
        if (!hasBounds) {
            computed = object.geometry.bounds;
            hasBounds = true;
        } else {
            computed = computed.united(object.geometry.bounds);
        }
    }
    bounds = hasBounds ? computed : QRectF(QPointF(0.0, 0.0), pageSize);
}

const SceneObjectGeometry* SceneGeometry::objectById(const QString& objectId) const
{
    for (const SceneObjectGeometry& object : objects) {
        if (object.objectId == objectId) {
            return &object;
        }
    }
    return nullptr;
}

SceneObjectGeometry* SceneGeometry::objectById(const QString& objectId)
{
    for (SceneObjectGeometry& object : objects) {
        if (object.objectId == objectId) {
            return &object;
        }
    }
    return nullptr;
}

QPainterPath SceneGeometry::combinedPath() const
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for (const SceneObjectGeometry& object : objects) {
        if (object.visible) {
            path.addPath(object.geometry.combinedPath());
        }
    }
    return path;
}

bool SceneGeometry::hasVisibleGeometry() const
{
    for (const SceneObjectGeometry& object : objects) {
        if (object.visible && object.geometry.hasVisibleGeometry()) {
            return true;
        }
    }
    return false;
}

} // namespace vt
