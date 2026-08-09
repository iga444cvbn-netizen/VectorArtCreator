#include "core/scene/scene_evaluator.h"

#include "core/text/text_engine.h"

namespace vt {

namespace {

void applyObjectTransform(VectorGeometry* geometry, const ObjectTransform& transform)
{
    if (!geometry) {
        return;
    }
    const QPointF center = geometry->referenceBounds.center();
    QTransform objectTransform;
    Q_UNUSED(objectTransform.translate(transform.position.x(), transform.position.y()));
    Q_UNUSED(objectTransform.translate(center.x(), center.y()));
    Q_UNUSED(objectTransform.rotate(transform.rotation));
    Q_UNUSED(objectTransform.scale(transform.scale.x(), transform.scale.y()));
    Q_UNUSED(objectTransform.translate(-center.x(), -center.y()));
    geometry->transformAll(objectTransform);
}

} // namespace

VectorGeometry SceneEvaluator::evaluateObject(const TextObject& object,
                                               QString* warning,
                                               QString* error)
{
    TextEngine textEngine;
    const ShapedText shaped = textEngine.shape(object);
    if (warning) {
        *warning = shaped.warning;
    }
    if (error) {
        *error = shaped.error;
    }

    VectorGeometry geometry = GlyphGeometryBuilder::build(shaped, object.typography.fontSize);
    object.effects.apply(geometry);
    object.deformation.apply(geometry);
    applyObjectTransform(&geometry, object.transform);
    return geometry;
}

SceneGeometry SceneEvaluator::evaluate(const Page& page)
{
    SceneGeometry result;
    result.pageId = page.id;
    result.pageSize = page.size;
    result.pageBackground = page.background;

    for (const auto& layer : page.layers) {
        if (!layer) {
            continue;
        }
        for (const auto& object : layer->objects) {
            if (!object) {
                continue;
            }
            SceneObjectGeometry evaluated;
            evaluated.objectId = object->id;
            evaluated.pageId = page.id;
            evaluated.layerId = layer->id;
            evaluated.sourceText = object->sourceText;
            evaluated.fill = object->fill;
            evaluated.visible = object->visible && layer->visible;
            evaluated.locked = layer->locked;
            evaluated.geometry = evaluateObject(*object, &evaluated.warning, &evaluated.error);
            evaluated.visualBounds = evaluated.geometry.bounds;
            result.objects.push_back(std::move(evaluated));
        }
    }
    result.recomputeBounds();
    return result;
}

} // namespace vt
