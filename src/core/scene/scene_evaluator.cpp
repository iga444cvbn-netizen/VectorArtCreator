#include "core/scene/scene_evaluator.h"

#include "core/text/text_engine.h"

#include <QtConcurrentRun>

#include <QCryptographicHash>
#include <QFuture>
#include <QHash>
#include <QJsonDocument>

#include <utility>

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

struct CachedObjectStages {
    QByteArray shapingKey;
    ShapedText shaped;
    QByteArray baseKey;
    VectorGeometry baseGeometry;
    QByteArray effectKey;
    VectorGeometry effectGeometry;
    QByteArray deformationKey;
    VectorGeometry deformationGeometry;
};

thread_local TextEngine workerTextEngine;
thread_local QHash<QString, CachedObjectStages> workerCache;

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
    return hashKey(QJsonDocument(object.effects.toJson()).toJson(QJsonDocument::Compact));
}

QByteArray deformationKey(const TextObject& object)
{
    return hashKey(QJsonDocument(object.deformation.toJson()).toJson(QJsonDocument::Compact));
}

SceneObjectGeometry evaluateObjectTask(const QString& pageId,
                                       const QString& layerId,
                                       const TextObject& object,
                                       bool locked)
{
    SceneObjectGeometry evaluated;
    evaluated.objectId = object.id;
    evaluated.pageId = pageId;
    evaluated.layerId = layerId;
    evaluated.sourceText = object.sourceText;
    evaluated.fill = object.fill;
    evaluated.visible = object.visible;
    evaluated.locked = locked;

    if (workerCache.size() > 64) {
        workerCache.clear();
    }
    CachedObjectStages& cache = workerCache[object.id];
    const QByteArray currentShapingKey = SceneEvaluator::shapingCacheKey(object);
    if (cache.shapingKey != currentShapingKey) {
        cache.shaped = workerTextEngine.shape(object);
        cache.shapingKey = currentShapingKey;
        cache.baseKey.clear();
        cache.effectKey.clear();
        cache.deformationKey.clear();
    }
    evaluated.warning = cache.shaped.warning;
    evaluated.error = cache.shaped.error;

    const QByteArray currentBaseKey = hashKey(currentShapingKey
                                               + QByteArray::number(object.typography.fontSize));
    if (cache.baseKey != currentBaseKey) {
        cache.baseGeometry = GlyphGeometryBuilder::build(cache.shaped, object.typography.fontSize);
        cache.baseKey = currentBaseKey;
        cache.effectKey.clear();
        cache.deformationKey.clear();
    }

    const QByteArray currentEffectKey = hashKey(cache.baseKey + effectsKey(object));
    if (cache.effectKey != currentEffectKey) {
        cache.effectGeometry = cache.baseGeometry;
        object.effects.apply(cache.effectGeometry);
        cache.effectKey = currentEffectKey;
        cache.deformationKey.clear();
    }

    const QByteArray currentDeformationKey = hashKey(cache.effectKey + deformationKey(object));
    if (cache.deformationKey != currentDeformationKey) {
        cache.deformationGeometry = cache.effectGeometry;
        object.deformation.apply(cache.deformationGeometry);
        cache.deformationKey = currentDeformationKey;
    }

    evaluated.geometry = cache.deformationGeometry;
    applyObjectTransform(&evaluated.geometry, object.transform);
    evaluated.visualBounds = evaluated.geometry.bounds;
    return evaluated;
}

} // namespace

QByteArray SceneEvaluator::shapingCacheKey(const TextObject& object)
{
    return shapingKey(object);
}

VectorGeometry SceneEvaluator::evaluateObject(const TextObject& object,
                                               QString* warning,
                                               QString* error)
{
    const SceneObjectGeometry evaluated = evaluateObjectTask({}, {}, object, false);
    if (warning) {
        *warning = evaluated.warning;
    }
    if (error) {
        *error = evaluated.error;
    }
    return evaluated.geometry;
}

SceneGeometry SceneEvaluator::evaluate(const Page& page)
{
    SceneGeometry result;
    result.pageId = page.id;
    result.pageSize = page.size;
    result.pageBackground = page.background;

    struct Task {
        QString layerId;
        TextObject object;
        bool locked = false;
    };
    QVector<Task> tasks;
    for (const auto& layer : page.layers) {
        if (!layer || !layer->visible) {
            continue;
        }
        for (const auto& object : layer->objects) {
            if (!object || !object->visible) {
                continue;
            }
            tasks.push_back({layer->id, *object, layer->locked});
        }
    }
    QVector<QFuture<SceneObjectGeometry>> futures;
    futures.reserve(tasks.size());
    for (const Task& task : tasks) {
        futures.push_back(QtConcurrent::run([pageId = page.id, task] {
            return evaluateObjectTask(pageId, task.layerId, task.object, task.locked);
        }));
    }
    result.objects.reserve(futures.size());
    for (QFuture<SceneObjectGeometry>& future : futures) {
        result.objects.push_back(future.result());
    }
    result.recomputeBounds();
    return result;
}

} // namespace vt
