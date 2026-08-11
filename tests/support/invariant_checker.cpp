#include "tests/support/invariant_checker.h"

#include "tests/support/geometry_assertions.h"

#include <QSet>

#include <cmath>

namespace vt::test {

InvariantReport checkInvariants(const Document& document, const SceneGeometry* scene,
                                const QStringList& selectedObjectIds,
                                const QString& activeObjectId, const QString& editingObjectId)
{
    InvariantReport report;
    QSet<QString> pages, layers, objects, effects;
    for (const auto& page : document.pages) {
        if (!page || page->id.isEmpty() || pages.contains(page->id)) {
            report.failures << QStringLiteral("duplicate or invalid page id");
            continue;
        }
        pages.insert(page->id);
        for (const auto& layer : page->layers) {
            if (!layer || layer->id.isEmpty() || layers.contains(layer->id)) {
                report.failures << QStringLiteral("duplicate or invalid layer id");
                continue;
            }
            layers.insert(layer->id);
            for (const auto& object : layer->objects) {
                if (!object || object->id.isEmpty() || objects.contains(object->id)) {
                    report.failures << QStringLiteral("duplicate or invalid object id");
                    continue;
                }
                objects.insert(object->id);
                const ObjectTransform& transform = object->transform;
                if (!isFinite(transform.position) || !isFinite(transform.pivotLocal)
                    || !std::isfinite(transform.rotation) || !isFinite(transform.scale)
                    || qAbs(transform.scale.x()) < ObjectTransform::MinimumScale
                    || qAbs(transform.scale.y()) < ObjectTransform::MinimumScale) {
                    report.failures << QStringLiteral("invalid transform for %1").arg(object->id);
                }
                if (!object->effects.hasUniqueInstanceIds()) {
                    report.failures << QStringLiteral("duplicate effect id on %1").arg(object->id);
                }
                for (int index = 0; index < object->effects.size(); ++index) {
                    const Effect* effect = object->effects.at(index);
                    if (!effect || effect->instanceId.isEmpty() || effects.contains(effect->instanceId)) {
                        report.failures << QStringLiteral("duplicate or invalid effect id");
                    } else {
                        effects.insert(effect->instanceId);
                    }
                    if (effect && (!std::isfinite(effect->masterStrength)
                                   || effect->scope.start < 0 || effect->scope.end < effect->scope.start)) {
                        report.failures << QStringLiteral("invalid effect state on %1").arg(object->id);
                    }
                }
            }
        }
    }
    if (!pages.contains(document.currentPageId) || !layers.contains(document.activeLayerId)) {
        report.failures << QStringLiteral("current page or active layer is absent");
    }
    for (const QString& id : selectedObjectIds) {
        if (!objects.contains(id)) report.failures << QStringLiteral("stale selection %1").arg(id);
    }
    if (!activeObjectId.isEmpty() && !objects.contains(activeObjectId)) {
        report.failures << QStringLiteral("stale active object %1").arg(activeObjectId);
    }
    if (!editingObjectId.isEmpty() && !objects.contains(editingObjectId)) {
        report.failures << QStringLiteral("stale editing object %1").arg(editingObjectId);
    }
    if (scene) {
        if (scene->pageId != document.currentPageId || !isFinite(scene->bounds)) {
            report.failures << QStringLiteral("stale or invalid published scene");
        }
        for (const SceneObjectGeometry& object : scene->objects) {
            if (!objects.contains(object.objectId)) report.failures << QStringLiteral("scene object missing from document");
            QString geometryError;
            if (!hasFiniteGeometry(object.geometry, &geometryError)) {
                report.failures << QStringLiteral("invalid scene geometry: %1").arg(geometryError);
            }
        }
    }
    return report;
}

} // namespace vt::test
