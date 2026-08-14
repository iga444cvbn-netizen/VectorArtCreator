#include "core/scene/scene_evaluator.h"

#include "core/text/text_engine.h"
#include "core/path/path_layout.h"

#include <QCryptographicHash>
#include <QHash>
#include <QJsonDocument>

#include <utility>
#include <atomic>

namespace vt {

namespace {

bool applyObjectTransform(VectorGeometry* geometry,
                          const ObjectFrame& frame,
                          const WorkControl& work)
{
    if (!geometry) {
        return false;
    }
    for (int index = 0; index < geometry->pieces.size(); ++index) {
        if (!work.consume(qMax(1, geometry->pieces.at(index).path.elementCount()))) {
            return false;
        }
        geometry->transformPiece(index, frame.localToPage);
    }
    geometry->recomputeBounds();
    return true;
}

qint64 geometryWorkUnits(const VectorGeometry& geometry)
{
    qint64 units = 1;
    for (const GeometryPiece& piece : geometry.pieces) {
        units += qMax(1, piece.path.elementCount());
    }
    return units;
}

struct CachedObjectStages {
    QByteArray shapingKey;
    ShapedText shaped;
    QByteArray baseKey;
    VectorGeometry baseGeometry;
    QByteArray pathKey;
    VectorGeometry pathGeometry;
    QByteArray effectKey;
    VectorGeometry effectGeometry;
    QByteArray deformationKey;
    VectorGeometry deformationGeometry;
};

thread_local TextEngine workerTextEngine;
thread_local QHash<QString, CachedObjectStages> workerCache;
thread_local quint64 workerFontEpoch = 0;
std::atomic<quint64> globalFontEpoch{1};

QByteArray hashKey(const QString& value)
{
    return QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha1);
}

QByteArray hashKey(const QByteArray& value)
{
    return QCryptographicHash::hash(value, QCryptographicHash::Sha1);
}

QByteArray shapingKey(const TextObject& object)
{
    QByteArray key;
    key += QByteArrayLiteral("fontEpoch=");
    key += QByteArray::number(globalFontEpoch.load(std::memory_order_acquire));
    key += '\0';
    key += QByteArrayLiteral("text=");
    key += object.sourceText.toUtf8();
    key += '\0';
    key += QByteArrayLiteral("family=");
    key += object.font.family.toUtf8();
    key += '\0';
    key += QByteArrayLiteral("style=");
    key += object.font.styleName.toUtf8();
    key += '\0';
    key += QByteArrayLiteral("weight=");
    key += QByteArray::number(object.font.weight);
    key += '\0';
    key += QByteArrayLiteral("italic=");
    key += object.font.italic ? '1' : '0';
    key += '\0';
    key += QByteArrayLiteral("underline=");
    key += object.font.underline ? '1' : '0';
    key += '\0';
    key += QByteArrayLiteral("strikeOut=");
    key += object.font.strikeOut ? '1' : '0';
    key += '\0';
    key += QByteArrayLiteral("fontSize=");
    key += QByteArray::number(object.typography.fontSize, 'g', 16);
    key += '\0';
    key += QByteArrayLiteral("trackingEm=");
    key += QByteArray::number(object.typography.trackingEm, 'g', 16);
    key += '\0';
    key += QByteArrayLiteral("lineSpacing=");
    key += QByteArray::number(object.typography.lineSpacing, 'g', 16);
    return hashKey(key);
}

QByteArray effectsKey(const TextObject& object)
{
    QByteArray key = QJsonDocument(object.effects.toJson()).toJson(QJsonDocument::Compact);
    key += QByteArrayLiteral("|stackStrength=");
    key += QByteArray::number(object.effectStackStrength, 'g', 16);
    return hashKey(key);
}

QByteArray pathLayoutKey(const TextObject& object)
{
    QByteArray key = QJsonDocument(object.pathLayout.toJson())
                         .toJson(QJsonDocument::Compact);
    key += QByteArrayLiteral("|path=");
    if (object.path.has_value()) {
        key += QJsonDocument(object.path->toJson()).toJson(QJsonDocument::Compact);
    }
    return hashKey(key);
}

QByteArray deformationKey(const TextObject& object)
{
    return hashKey(QJsonDocument(object.deformation.toJson()).toJson(QJsonDocument::Compact));
}

SceneObjectGeometry evaluateObjectTask(const QString& pageId,
                                       const QString& layerId,
                                       const TextObject& object,
                                       bool locked,
                                       quint64 spatialRevision,
                                       const WorkControl& work)
{
    SceneObjectGeometry evaluated;
    evaluated.spatialRevision = spatialRevision;
    evaluated.objectId = object.id;
    evaluated.pageId = pageId;
    evaluated.layerId = layerId;
    evaluated.sourceText = object.sourceText;
    evaluated.transform = object.transform;
    evaluated.fill = object.fill;
    evaluated.visible = object.visible;
    evaluated.locked = locked;
    if (!work.consume()) {
        return evaluated;
    }

    const quint64 fontEpoch = globalFontEpoch.load(std::memory_order_acquire);
    if (workerFontEpoch != fontEpoch) {
        workerTextEngine.clearCache();
        workerCache.clear();
        workerFontEpoch = fontEpoch;
    }
    if (workerCache.size() > 64) {
        workerCache.clear();
    }
    CachedObjectStages& cache = workerCache[object.id];
    const QByteArray currentShapingKey = SceneEvaluator::shapingCacheKey(object);
    if (cache.shapingKey != currentShapingKey) {
        ShapedText shaped = workerTextEngine.shape(object, work);
        if (!work.isRunning()) {
            return evaluated;
        }
        cache.shaped = std::move(shaped);
        cache.shapingKey = currentShapingKey;
        cache.baseKey.clear();
        cache.pathKey.clear();
        cache.effectKey.clear();
        cache.deformationKey.clear();
    }
    evaluated.warning = cache.shaped.warning;
    evaluated.error = cache.shaped.error;

    const QByteArray currentBaseKey = hashKey(currentShapingKey
                                               + QByteArray::number(object.typography.fontSize));
    if (cache.baseKey != currentBaseKey) {
        VectorGeometry baseGeometry = GlyphGeometryBuilder::build(
            cache.shaped,
            object.typography.fontSize,
            object.font.underline,
            object.font.strikeOut,
            work);
        if (!work.isRunning()) {
            return evaluated;
        }
        cache.baseGeometry = std::move(baseGeometry);
        cache.baseKey = currentBaseKey;
        cache.pathKey.clear();
        cache.effectKey.clear();
        cache.deformationKey.clear();
    }

    const QByteArray currentPathKey = hashKey(cache.baseKey + pathLayoutKey(object));
    if (cache.pathKey != currentPathKey) {
        VectorGeometry pathGeometry = cache.baseGeometry;
        if (object.pathLayout.enabled) {
            if (!object.path.has_value()
                || object.pathLayout.pathId.isEmpty()
                || object.path->id != object.pathLayout.pathId) {
                evaluated.error = QStringLiteral("Path typography references a missing or unrelated path.");
                cache.pathKey.clear();
                return evaluated;
            }
            QString pathError;
            if (!PathLayoutEngine::apply(&pathGeometry,
                                         cache.shaped,
                                         *object.path,
                                         object.pathLayout,
                                         &pathError,
                                         work)) {
                if (!work.isRunning()) {
                    cache.pathKey.clear();
                    return evaluated;
                }
                evaluated.error = pathError.isEmpty()
                    ? QStringLiteral("Path typography could not be evaluated.")
                    : pathError;
                cache.pathKey.clear();
                return evaluated;
            }
        }
        if (!work.isRunning()) {
            cache.pathKey.clear();
            return evaluated;
        }
        cache.pathGeometry = std::move(pathGeometry);
        cache.pathKey = currentPathKey;
        cache.effectKey.clear();
        cache.deformationKey.clear();
    }

    const QByteArray currentEffectKey = hashKey(cache.pathKey + effectsKey(object));
    if (cache.effectKey != currentEffectKey) {
        if (!work.consume(geometryWorkUnits(cache.pathGeometry))) {
            return evaluated;
        }
        cache.effectGeometry = cache.pathGeometry;
        object.effects.apply(cache.effectGeometry, object.effectStackStrength, work);
        if (!work.isRunning()) {
            cache.effectKey.clear();
            return evaluated;
        }
        cache.effectKey = currentEffectKey;
        cache.deformationKey.clear();
    }

    const QByteArray currentDeformationKey = hashKey(cache.effectKey + deformationKey(object));
    if (cache.deformationKey != currentDeformationKey) {
        if (!work.consume(geometryWorkUnits(cache.effectGeometry))) {
            return evaluated;
        }
        cache.deformationGeometry = cache.effectGeometry;
        object.deformation.apply(cache.deformationGeometry, work);
        if (!work.isRunning()) {
            cache.deformationKey.clear();
            return evaluated;
        }
        cache.deformationKey = currentDeformationKey;
    }

    // Empty text is still an object.  Its local frame is deliberately
    // independent from glyph visibility so it can be selected, moved and
    // edited later.
    QRectF baseBounds = cache.pathGeometry.referenceBounds;
    if (baseBounds.isNull() || baseBounds.isEmpty()) {
        baseBounds = QRectF(0.0,
                            -object.typography.fontSize * 0.8,
                            qMax<qreal>(120.0, object.typography.fontSize * 2.0),
                            qMax<qreal>(36.0, object.typography.fontSize * 1.2));
    }
    evaluated.frame = ObjectFrame::fromTransform(object.transform,
                                                  baseBounds,
                                                  cache.deformationGeometry.bounds);
    evaluated.frame.spatialRevision = spatialRevision;
    if (!work.consume(geometryWorkUnits(cache.deformationGeometry))) {
        evaluated.frame.spatialRevision = 0;
        return evaluated;
    }
    evaluated.geometry = cache.deformationGeometry;
    if (!applyObjectTransform(&evaluated.geometry, evaluated.frame, work)) {
        evaluated.geometry = {};
        evaluated.frame.spatialRevision = 0;
        return evaluated;
    }
    evaluated.visualBounds = evaluated.frame.pageAabb();
    return evaluated;
}

} // namespace

QByteArray SceneEvaluator::shapingCacheKey(const TextObject& object)
{
    return shapingKey(object);
}

void SceneEvaluator::invalidateFontCaches()
{
    globalFontEpoch.fetch_add(1, std::memory_order_acq_rel);
}

quint64 SceneEvaluator::fontCacheEpoch()
{
    return globalFontEpoch.load(std::memory_order_acquire);
}

ObjectFrame SceneEvaluator::evaluateObjectFrame(const TextObject& object,
                                                quint64 spatialRevision,
                                                const WorkControl& work)
{
    return evaluateObjectTask({}, {}, object, false, spatialRevision, work).frame;
}

SceneGeometry SceneEvaluator::evaluate(const Page& page,
                                       quint64 spatialRevision,
                                       const WorkControl& work)
{
    SceneGeometry result;
    result.spatialRevision = spatialRevision;
    result.pageId = page.id;
    result.pageSize = page.size;
    result.pageBackground = page.background;

    struct Task {
        QString layerId;
        const TextObject* object = nullptr;
        bool locked = false;
    };
    bool interruptedDuringCollection = false;
    QVector<Task> tasks;
    for (const auto& layer : page.layers) {
        if (!work.consume()) {
            interruptedDuringCollection = true;
            break;
        }
        if (!layer || !layer->visible) {
            continue;
        }
        for (const auto& object : layer->objects) {
            if (!work.consume()) {
                interruptedDuringCollection = true;
                break;
            }
            if (!object || !object->visible) {
                continue;
            }
            tasks.push_back({layer->id, object.get(), layer->locked});
        }
        if (interruptedDuringCollection) {
            break;
        }
    }
    // EditorController schedules a complete page evaluation.  Do not queue
    // child tasks and synchronously wait on the same global pool here: a
    // constrained pool can otherwise starve itself.  Object evaluation stays
    // ordered and sequential within that outer worker.
    for (const Task& task : tasks) {
        if (!work.consume() || !task.object) {
            interruptedDuringCollection = true;
            break;
        }
        result.objects.push_back(evaluateObjectTask(
            page.id, task.layerId, *task.object, task.locked, spatialRevision, work));
        if (!work.isRunning()) {
            break;
        }
    }
    if (interruptedDuringCollection || !work.isRunning()) {
        result.objects.clear();
        result.bounds = {};
        result.evaluationStatus = work.status() == WorkControlStatus::Cancelled
            ? EvaluationStatus::Cancelled
            : EvaluationStatus::BudgetExceeded;
        result.evaluationMessage = work.interruptionMessage();
        return result;
    }
    if (!work.consume(qMax(1, result.objects.size()))) {
        result.objects.clear();
        result.evaluationStatus = work.status() == WorkControlStatus::Cancelled
            ? EvaluationStatus::Cancelled
            : EvaluationStatus::BudgetExceeded;
        result.evaluationMessage = work.interruptionMessage();
        return result;
    }
    result.recomputeBounds();
    return result;
}

} // namespace vt
