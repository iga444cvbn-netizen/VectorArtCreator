#include "tests/support/invariant_checker.h"

#include "tests/support/geometry_assertions.h"

#include "core/effects/effect_registry.h"

#include <QHash>
#include <QSet>
#include <QTransform>

#include <cmath>

namespace vt::test {
namespace {

bool finiteTransform(const QTransform& transform)
{
    return std::isfinite(transform.m11()) && std::isfinite(transform.m12())
        && std::isfinite(transform.m13()) && std::isfinite(transform.m21())
        && std::isfinite(transform.m22()) && std::isfinite(transform.m23())
        && std::isfinite(transform.m31()) && std::isfinite(transform.m32())
        && std::isfinite(transform.m33());
}

bool sameTransform(const ObjectTransform& left, const ObjectTransform& right)
{
    return left.position == right.position && left.rotation == right.rotation
        && left.scale == right.scale && left.pivotLocal == right.pivotLocal
        && left.hasPivot == right.hasPivot;
}

} // namespace

InvariantReport checkInvariants(const Document& document, const SceneGeometry* scene,
                                const QStringList& selectedObjectIds,
                                const QString& activeObjectId, const QString& editingObjectId)
{
    InvariantReport report;
    QSet<QString> pages, layers, objects, effects;
    QHash<QString, QString> pageForLayer;
    QHash<QString, QString> pageForObject;
    QHash<QString, QString> layerForObject;
    QHash<QString, const TextObject*> objectById;
    const Page* currentPage = nullptr;
    for (const auto& page : document.pages) {
        if (!page || page->id.isEmpty() || pages.contains(page->id)) {
            report.failures << QStringLiteral("duplicate or invalid page id");
            continue;
        }
        pages.insert(page->id);
        if (page->id == document.currentPageId) currentPage = page.get();
        if (!std::isfinite(page->size.width()) || !std::isfinite(page->size.height())
            || page->size.width() <= 0.0 || page->size.height() <= 0.0
            || !page->background.isValid()) {
            report.failures << QStringLiteral("invalid page state on %1").arg(page->id);
        }
        for (const auto& layer : page->layers) {
            if (!layer || layer->id.isEmpty() || layers.contains(layer->id)) {
                report.failures << QStringLiteral("duplicate or invalid layer id");
                continue;
            }
            layers.insert(layer->id);
            pageForLayer.insert(layer->id, page->id);
            for (const auto& object : layer->objects) {
                if (!object || object->id.isEmpty() || objects.contains(object->id)) {
                    report.failures << QStringLiteral("duplicate or invalid object id");
                    continue;
                }
                objects.insert(object->id);
                pageForObject.insert(object->id, page->id);
                layerForObject.insert(object->id, layer->id);
                objectById.insert(object->id, object.get());
                const ObjectTransform& transform = object->transform;
                if (!isFinite(transform.position) || !isFinite(transform.pivotLocal)
                    || !std::isfinite(transform.rotation) || !isFinite(transform.scale)
                    || qAbs(transform.scale.x()) < ObjectTransform::MinimumScale
                    || qAbs(transform.scale.y()) < ObjectTransform::MinimumScale) {
                    report.failures << QStringLiteral("invalid transform for %1").arg(object->id);
                }
                if (!std::isfinite(object->typography.fontSize)
                    || object->typography.fontSize <= 0.0
                    || !std::isfinite(object->typography.trackingEm)
                    || !std::isfinite(object->typography.lineSpacing)
                    || object->typography.lineSpacing <= 0.0
                    || !std::isfinite(object->effectStackStrength)
                    || object->effectStackStrength < 0.0 || object->effectStackStrength > 2.0
                    || !object->fill.isValid()) {
                    report.failures << QStringLiteral("invalid persistent object state on %1").arg(object->id);
                }
                if (!std::isfinite(object->deformation.strength)
                    || object->deformation.strength < 0.0
                    || object->deformation.strength > 4.0) {
                    report.failures << QStringLiteral("invalid deformation strength on %1").arg(object->id);
                }
                for (int strokeIndex = 0; strokeIndex < object->deformation.strokes.size(); ++strokeIndex) {
                    const DeformationStroke& stroke = object->deformation.strokes.at(strokeIndex);
                    bool validStroke = std::isfinite(stroke.radius)
                        && stroke.radius >= 0.01 && stroke.radius <= 100000.0
                        && std::isfinite(stroke.strength)
                        && stroke.strength >= 0.0 && stroke.strength <= 4.0
                        && std::isfinite(stroke.hardness)
                        && stroke.hardness >= 0.0 && stroke.hardness <= 1.0
                        && !stroke.samples.isEmpty() && stroke.samples.size() <= 4096
                        && stroke.coordinateSpace != DeformationCoordinateSpace::PageInput;
                    for (const BrushSample& sample : stroke.samples) {
                        validStroke = validStroke && isFinite(sample.position) && isFinite(sample.delta)
                            && std::isfinite(sample.pressure)
                            && sample.pressure >= 0.0 && sample.pressure <= 4.0;
                    }
                    if (!validStroke) {
                        report.failures << QStringLiteral("invalid deformation stroke %1 on %2")
                                               .arg(strokeIndex).arg(object->id);
                    }
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
                                   || effect->masterStrength < 0.0 || effect->masterStrength > 3.0
                                   || effect->scope.start < 0 || effect->scope.end < effect->scope.start
                                   || (effect->scope.kind == EffectScopeKind::TextRange
                                       && effect->scope.end > object->sourceText.size()))) {
                        report.failures << QStringLiteral("invalid effect state on %1").arg(object->id);
                    }
                    if (effect) {
                        const EffectDescriptor* descriptor =
                            EffectRegistry::instance().descriptor(effect->typeId());
                        if (!descriptor || descriptor->domain != effect->domain()) {
                            report.failures << QStringLiteral("effect descriptor mismatch on %1")
                                                   .arg(object->id);
                        } else {
                            if (!descriptor->supportsMask
                                && (!effect->maskStrokes.isEmpty() || effect->maskInverted)) {
                                report.failures << QStringLiteral("unsupported effect mask on %1 effect %2")
                                                       .arg(object->id, effect->instanceId);
                            }
                            if (!descriptor->supportsTextRange
                                && effect->scope.kind == EffectScopeKind::TextRange) {
                                report.failures << QStringLiteral("unsupported text range on %1 effect %2")
                                                       .arg(object->id, effect->instanceId);
                            }
                        }
                        for (const EffectParameter& parameter : effect->parameterDefinitions()) {
                            if (!std::isfinite(parameter.value) || !std::isfinite(parameter.minimum)
                                || !std::isfinite(parameter.maximum) || !std::isfinite(parameter.step)
                                || parameter.minimum > parameter.maximum
                                || parameter.value < parameter.minimum || parameter.value > parameter.maximum) {
                                report.failures << QStringLiteral("invalid effect parameter %1 on %2")
                                                       .arg(parameter.id, object->id);
                            }
                        }
                        for (int maskIndex = 0; maskIndex < effect->maskStrokes.size(); ++maskIndex) {
                            const EffectMaskStroke& mask = effect->maskStrokes.at(maskIndex);
                            bool validMask = std::isfinite(mask.radius) && mask.radius > 0.0
                                && std::isfinite(mask.opacity) && mask.opacity >= 0.0 && mask.opacity <= 1.0
                                && std::isfinite(mask.hardness) && mask.hardness >= 0.0 && mask.hardness <= 1.0;
                            for (const QPointF& point : mask.points) validMask = validMask && isFinite(point);
                            if (!validMask) {
                                report.failures << QStringLiteral("invalid effect mask %1 on %2")
                                                       .arg(maskIndex).arg(object->id);
                            }
                        }
                    }
                }
            }
        }
    }
    if (!currentPage) {
        report.failures << QStringLiteral("current page is absent");
    }
    if (!layers.contains(document.activeLayerId)) {
        report.failures << QStringLiteral("active layer is absent");
    } else if (pageForLayer.value(document.activeLayerId) != document.currentPageId) {
        report.failures << QStringLiteral("active layer is not on current page");
    }
    if (!document.activeObjectId.isEmpty()) {
        if (!objects.contains(document.activeObjectId)) {
            report.failures << QStringLiteral("document active object is absent");
        } else if (pageForObject.value(document.activeObjectId) != document.currentPageId) {
            report.failures << QStringLiteral("document active object is not on current page");
        }
    }
    for (const QString& id : selectedObjectIds) {
        if (!objects.contains(id) || pageForObject.value(id) != document.currentPageId) {
            report.failures << QStringLiteral("stale or nonlocal selection %1").arg(id);
        }
    }
    if (!activeObjectId.isEmpty()
        && (!objects.contains(activeObjectId)
            || pageForObject.value(activeObjectId) != document.currentPageId)) {
        report.failures << QStringLiteral("stale or nonlocal active object %1").arg(activeObjectId);
    }
    if (!activeObjectId.isEmpty() && !selectedObjectIds.contains(activeObjectId)) {
        report.failures << QStringLiteral("active object is not in the selected set %1")
                               .arg(activeObjectId);
    }
    if (!editingObjectId.isEmpty()
        && (!objects.contains(editingObjectId)
            || pageForObject.value(editingObjectId) != document.currentPageId)) {
        report.failures << QStringLiteral("stale or nonlocal editing object %1").arg(editingObjectId);
    }
    if (scene) {
        if (scene->pageId != document.currentPageId || !isFinite(scene->bounds)) {
            report.failures << QStringLiteral("stale or invalid published scene");
        }
        QSet<QString> sceneObjectIds;
        for (const SceneObjectGeometry& object : scene->objects) {
            const TextObject* source = objectById.value(object.objectId, nullptr);
            if (sceneObjectIds.contains(object.objectId)) {
                report.failures << QStringLiteral("duplicate scene object %1").arg(object.objectId);
            }
            sceneObjectIds.insert(object.objectId);
            if (object.spatialRevision != scene->spatialRevision
                || object.frame.spatialRevision != scene->spatialRevision) {
                report.failures << QStringLiteral("scene/frame revision mismatch on %1")
                                       .arg(object.objectId);
            }
            if (!source || pageForObject.value(object.objectId) != scene->pageId) {
                report.failures << QStringLiteral("scene object missing from current document page");
            } else {
                const Layer* sourceLayer = currentPage ? currentPage->layerById(layerForObject.value(object.objectId)) : nullptr;
                if (object.layerId != layerForObject.value(object.objectId)
                    || object.pageId != scene->pageId || object.sourceText != source->sourceText
                    || !sameTransform(object.transform, source->transform)
                    || object.fill != source->fill || !source->visible
                    || !sourceLayer || !sourceLayer->visible
                    || object.locked != sourceLayer->locked) {
                    report.failures << QStringLiteral("scene object semantics do not match document %1")
                                           .arg(object.objectId);
                }
            }
            QString geometryError;
            if (!hasFiniteGeometry(object.geometry, &geometryError)) {
                report.failures << QStringLiteral("invalid scene geometry: %1").arg(geometryError);
            }
            if (!isFinite(object.visualBounds) || !isFinite(object.frame.baseLocalBounds)
                || !isFinite(object.frame.currentLocalBounds) || !isFinite(object.frame.pivotLocal)
                || !finiteTransform(object.frame.localToPage)
                || !finiteTransform(object.frame.pageToLocal)) {
                report.failures << QStringLiteral("invalid object frame for %1").arg(object.objectId);
            }
        }
        // A complete scene must include every visible object on the current
        // page. Partial/cancelled evaluation deliberately has no such promise.
        if (scene->evaluationStatus == EvaluationStatus::Complete && currentPage) {
            for (const auto& layer : currentPage->layers) {
                if (!layer || !layer->visible) continue;
                for (const auto& object : layer->objects) {
                    if (object && object->visible && !sceneObjectIds.contains(object->id)) {
                        report.failures << QStringLiteral("visible document object missing from complete scene %1")
                                               .arg(object->id);
                    }
                }
            }
        }
    }
    return report;
}

} // namespace vt::test
