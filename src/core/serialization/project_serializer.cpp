#include "core/serialization/project_serializer.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>

#include <limits>
#include <utility>

namespace vt {

namespace {

bool identityError(const QString& message, QString* error)
{
    if (error) *error = message;
    return false;
}

// Current-schema IDs are persisted semantic identity, not optional hints.
// Validate the raw hierarchy before constructors can replace missing IDs with
// fresh defaults and accidentally turn corruption into a different project.
bool validateHierarchicalIdentity(const QJsonObject& root, QString* error)
{
    const QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
    QSet<QString> pageIds;
    QSet<QString> layerIds;
    QSet<QString> objectIds;
    QSet<QString> effectIds;
    QHash<QString, QSet<QString>> layerIdsByPage;
    QHash<QString, QSet<QString>> objectIdsByPage;

    for (int pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
        const QJsonObject page = pages.at(pageIndex).toObject();
        const QString pageId = page.value(QStringLiteral("id")).toString();
        if (pageId.isEmpty() || pageIds.contains(pageId)) {
            return identityError(
                QStringLiteral("Project contains a missing or duplicate page ID at pages[%1].id.")
                    .arg(pageIndex), error);
        }
        pageIds.insert(pageId);
        const QJsonArray layers = page.value(QStringLiteral("layers")).toArray();
        if (layers.isEmpty()) {
            return identityError(QStringLiteral("Current-schema page '%1' has no layer.").arg(pageId), error);
        }
        for (int layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
            const QJsonObject layer = layers.at(layerIndex).toObject();
            const QString layerId = layer.value(QStringLiteral("id")).toString();
            if (layerId.isEmpty() || layerIds.contains(layerId)) {
                return identityError(
                    QStringLiteral("Project contains a missing or duplicate layer ID at pages[%1].layers[%2].id.")
                        .arg(pageIndex).arg(layerIndex), error);
            }
            layerIds.insert(layerId);
            layerIdsByPage[pageId].insert(layerId);
            const QJsonArray objects = layer.value(QStringLiteral("objects")).toArray();
            for (int objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
                const QJsonObject object = objects.at(objectIndex).toObject();
                const QString objectId = object.value(QStringLiteral("id")).toString();
                if (objectId.isEmpty() || objectIds.contains(objectId)) {
                    return identityError(
                        QStringLiteral("Project contains a missing or duplicate object ID at pages[%1].layers[%2].objects[%3].id.")
                            .arg(pageIndex).arg(layerIndex).arg(objectIndex), error);
                }
                objectIds.insert(objectId);
                objectIdsByPage[pageId].insert(objectId);
                const QJsonArray effects = object.value(QStringLiteral("effects")).toArray();
                for (int effectIndex = 0; effectIndex < effects.size(); ++effectIndex) {
                    const QString effectId = effects.at(effectIndex).toObject()
                                                 .value(QStringLiteral("id")).toString();
                    if (effectId.isEmpty() || effectIds.contains(effectId)) {
                        return identityError(
                            QStringLiteral("Project contains a missing or duplicate effect ID at pages[%1].layers[%2].objects[%3].effects[%4].id.")
                                .arg(pageIndex).arg(layerIndex).arg(objectIndex).arg(effectIndex), error);
                    }
                    effectIds.insert(effectId);
                }
            }
        }
    }

    const QString currentPageId = root.value(QStringLiteral("currentPageId")).toString();
    if (!pageIds.contains(currentPageId)) {
        return identityError(QStringLiteral("Current-schema project references an unknown current page ID."), error);
    }
    const QString activeLayerId = root.value(QStringLiteral("activeLayerId")).toString();
    if (!layerIdsByPage.value(currentPageId).contains(activeLayerId)) {
        return identityError(QStringLiteral("Current-schema project active layer is not on its current page."), error);
    }
    const QString activeObjectId = root.value(QStringLiteral("activeObjectId")).toString();
    if (!activeObjectId.isEmpty() && !objectIdsByPage.value(currentPageId).contains(activeObjectId)) {
        return identityError(QStringLiteral("Current-schema project active object is not on its current page."), error);
    }
    return true;
}

struct ResourceBudgetTracker {
    qint64 layers = 0;
    qint64 objects = 0;
    qint64 effects = 0;
    qint64 sourceUtf16 = 0;
    qint64 maskStrokes = 0;
    qint64 maskPoints = 0;
    qint64 deformationStrokes = 0;
    qint64 deformationSamples = 0;
    qint64 estimatedWork = 0;
};

bool resourceError(const QString& path,
                   const QString& resource,
                   qint64 actual,
                   qint64 maximum,
                   QString* error)
{
    if (error) {
        *error = QStringLiteral("Project resource limit exceeded at %1: %2 is %3; maximum is %4.")
                     .arg(path, resource)
                     .arg(actual)
                     .arg(maximum);
    }
    return false;
}

bool checkLimit(qint64 actual,
                qint64 maximum,
                const QString& path,
                const QString& resource,
                QString* error)
{
    return actual <= maximum
        || resourceError(path, resource, actual, maximum, error);
}

qint64 saturatedAdd(qint64 left, qint64 right)
{
    if (right > 0 && left > std::numeric_limits<qint64>::max() - right) {
        return std::numeric_limits<qint64>::max();
    }
    return left + right;
}

qint64 saturatedMultiply(qint64 left, qint64 right)
{
    if (left <= 0 || right <= 0) return 0;
    if (left > std::numeric_limits<qint64>::max() / right) {
        return std::numeric_limits<qint64>::max();
    }
    return left * right;
}

bool validateTextObjectResources(const QJsonObject& object,
                                 const QString& path,
                                 const ProjectResourceLimits& limits,
                                 ResourceBudgetTracker* tracker,
                                 QString* error)
{
    const qint64 sourceUnits = object.value(QStringLiteral("sourceText")).toString().size();
    if (!checkLimit(sourceUnits, limits.maximumSourceUtf16PerObject,
                    path + QStringLiteral(".sourceText"), QStringLiteral("UTF-16 code units"), error)) {
        return false;
    }
    tracker->sourceUtf16 = saturatedAdd(tracker->sourceUtf16, sourceUnits);
    if (!checkLimit(tracker->sourceUtf16, limits.maximumSourceUtf16,
                    path + QStringLiteral(".sourceText"),
                    QStringLiteral("aggregate UTF-16 code units"), error)) {
        return false;
    }

    const QJsonArray effects = object.value(QStringLiteral("effects")).toArray();
    if (!checkLimit(effects.size(), limits.maximumEffectsPerObject,
                    path + QStringLiteral(".effects"), QStringLiteral("effects per object"), error)) {
        return false;
    }
    tracker->effects = saturatedAdd(tracker->effects, effects.size());
    if (!checkLimit(tracker->effects, limits.maximumEffects,
                    path + QStringLiteral(".effects"), QStringLiteral("aggregate effects"), error)) {
        return false;
    }

    qint64 objectMaskPoints = 0;
    for (int effectIndex = 0; effectIndex < effects.size(); ++effectIndex) {
        const QString effectPath = path + QStringLiteral(".effects[%1]").arg(effectIndex);
        const QJsonArray strokes = effects.at(effectIndex).toObject()
                                       .value(QStringLiteral("mask")).toArray();
        if (!checkLimit(strokes.size(), limits.maximumMaskStrokesPerEffect,
                        effectPath + QStringLiteral(".mask"),
                        QStringLiteral("mask strokes per effect"), error)) {
            return false;
        }
        tracker->maskStrokes = saturatedAdd(tracker->maskStrokes, strokes.size());
        if (!checkLimit(tracker->maskStrokes, limits.maximumMaskStrokes,
                        effectPath + QStringLiteral(".mask"),
                        QStringLiteral("aggregate mask strokes"), error)) {
            return false;
        }
        for (int strokeIndex = 0; strokeIndex < strokes.size(); ++strokeIndex) {
            const QString strokePath = effectPath
                + QStringLiteral(".mask[%1].points").arg(strokeIndex);
            const QJsonArray points = strokes.at(strokeIndex).toObject()
                                          .value(QStringLiteral("points")).toArray();
            if (!checkLimit(points.size(), limits.maximumMaskPointsPerStroke,
                            strokePath, QStringLiteral("mask points per stroke"), error)) {
                return false;
            }
            objectMaskPoints = saturatedAdd(objectMaskPoints, points.size());
            tracker->maskPoints = saturatedAdd(tracker->maskPoints, points.size());
            if (!checkLimit(tracker->maskPoints, limits.maximumMaskPoints,
                            strokePath, QStringLiteral("aggregate mask points"), error)) {
                return false;
            }
        }
    }

    qint64 objectDeformationSamples = 0;
    const QJsonArray deformationStrokes = object.value(QStringLiteral("deformation"))
                                              .toObject()
                                              .value(QStringLiteral("strokes"))
                                              .toArray();
    if (!checkLimit(deformationStrokes.size(), limits.maximumDeformationStrokesPerObject,
                    path + QStringLiteral(".deformation.strokes"),
                    QStringLiteral("deformation strokes per object"), error)) {
        return false;
    }
    tracker->deformationStrokes = saturatedAdd(
        tracker->deformationStrokes, deformationStrokes.size());
    if (!checkLimit(tracker->deformationStrokes, limits.maximumDeformationStrokes,
                    path + QStringLiteral(".deformation.strokes"),
                    QStringLiteral("aggregate deformation strokes"), error)) {
        return false;
    }
    for (int strokeIndex = 0; strokeIndex < deformationStrokes.size(); ++strokeIndex) {
        const QJsonObject serializedStroke = deformationStrokes.at(strokeIndex).toObject();
        const QString samplesPath = path
            + QStringLiteral(".deformation.strokes[%1].samples").arg(strokeIndex);
        if (serializedStroke.value(QStringLiteral("coordinateSpace")).toString()
            == QStringLiteral("pageInput")) {
            if (error) {
                *error = QStringLiteral(
                    "Transient page-input coordinates cannot be persisted at %1.coordinateSpace.")
                             .arg(path + QStringLiteral(".deformation.strokes[%1]")
                                             .arg(strokeIndex));
            }
            return false;
        }
        const QJsonArray samples = serializedStroke.value(QStringLiteral("samples")).toArray();
        if (!checkLimit(samples.size(), limits.maximumDeformationSamplesPerStroke,
                        samplesPath, QStringLiteral("deformation samples per stroke"), error)) {
            return false;
        }
        objectDeformationSamples = saturatedAdd(objectDeformationSamples, samples.size());
        tracker->deformationSamples = saturatedAdd(tracker->deformationSamples, samples.size());
        if (!checkLimit(tracker->deformationSamples, limits.maximumDeformationSamples,
                        samplesPath, QStringLiteral("aggregate deformation samples"), error)) {
            return false;
        }
    }

    // The expensive paths scale approximately with glyph/piece count multiplied
    // by ordered effects, mask segments, and deformation samples. This rejects
    // adversarial products whose individual child arrays all remain legal.
    const qint64 geometryUnits = qMax<qint64>(1, sourceUnits);
    const qint64 nestedUnits = saturatedAdd(
        saturatedAdd(effects.size(), objectMaskPoints), objectDeformationSamples);
    const qint64 objectWork = saturatedMultiply(geometryUnits, saturatedAdd(1, nestedUnits));
    tracker->estimatedWork = saturatedAdd(tracker->estimatedWork, objectWork);
    return checkLimit(tracker->estimatedWork, limits.maximumEstimatedWork,
                      path, QStringLiteral("aggregate estimated geometry work units"), error);
}

bool validateObjectArrayResources(const QJsonArray& objects,
                                  const QString& path,
                                  const ProjectResourceLimits& limits,
                                  ResourceBudgetTracker* tracker,
                                  QString* error)
{
    if (!checkLimit(objects.size(), limits.maximumObjectsPerLayer,
                    path, QStringLiteral("objects per layer"), error)) {
        return false;
    }
    tracker->objects = saturatedAdd(tracker->objects, objects.size());
    if (!checkLimit(tracker->objects, limits.maximumObjects,
                    path, QStringLiteral("aggregate objects"), error)) {
        return false;
    }
    for (int objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
        if (!validateTextObjectResources(objects.at(objectIndex).toObject(),
                                         path + QStringLiteral("[%1]").arg(objectIndex),
                                         limits, tracker, error)) {
            return false;
        }
    }
    return true;
}

bool validateProjectResources(const QJsonObject& root,
                              int version,
                              const ProjectResourceLimits& limits,
                              QString* error)
{
    ResourceBudgetTracker tracker;
    const QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
    if (version >= 4 && !pages.isEmpty()) {
        if (!checkLimit(pages.size(), limits.maximumPages,
                        QStringLiteral("pages"), QStringLiteral("pages"), error)) {
            return false;
        }
        for (int pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
            const QString pagePath = QStringLiteral("pages[%1]").arg(pageIndex);
            const QJsonArray layers = pages.at(pageIndex).toObject()
                                          .value(QStringLiteral("layers")).toArray();
            if (!checkLimit(layers.size(), limits.maximumLayersPerPage,
                            pagePath + QStringLiteral(".layers"),
                            QStringLiteral("layers per page"), error)) {
                return false;
            }
            tracker.layers = saturatedAdd(tracker.layers, layers.size());
            if (!checkLimit(tracker.layers, limits.maximumLayers,
                            pagePath + QStringLiteral(".layers"),
                            QStringLiteral("aggregate layers"), error)) {
                return false;
            }
            for (int layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
                const QString objectsPath = pagePath
                    + QStringLiteral(".layers[%1].objects").arg(layerIndex);
                const QJsonArray objects = layers.at(layerIndex).toObject()
                                               .value(QStringLiteral("objects")).toArray();
                if (!validateObjectArrayResources(
                        objects, objectsPath, limits, &tracker, error)) {
                    return false;
                }
            }
        }
        return true;
    }

    tracker.layers = 1;
    if (!checkLimit(tracker.layers, limits.maximumLayers,
                    QStringLiteral("objects"), QStringLiteral("aggregate layers"), error)) {
        return false;
    }
    return validateObjectArrayResources(root.value(QStringLiteral("objects")).toArray(),
                                        QStringLiteral("objects"), limits, &tracker, error);
}

QJsonObject serializeTextObject(const TextObject& textObject)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("text"));
    object.insert(QStringLiteral("id"), textObject.id);
    object.insert(QStringLiteral("sourceText"), textObject.sourceText);
    object.insert(QStringLiteral("font"), textObject.font.toJson());
    object.insert(QStringLiteral("typography"), textObject.typography.toJson(textObject.fill));
    object.insert(QStringLiteral("effects"), textObject.effects.toJson());
    object.insert(QStringLiteral("effectStackStrength"), textObject.effectStackStrength);
    object.insert(QStringLiteral("deformation"), textObject.deformation.toJson());
    object.insert(QStringLiteral("transform"), textObject.transform.toJson());
    object.insert(QStringLiteral("visible"), textObject.visible);
    object.insert(QStringLiteral("futureData"), textObject.futureData);
    return object;
}

bool deserializeTextObject(const QJsonObject& object,
                           TextObject* textObject,
                           int formatVersion,
                           QString* error)
{
    if (object.value(QStringLiteral("type")).toString() != QStringLiteral("text")) {
        if (error) {
            *error = QStringLiteral("Project contains an unsupported object type.");
        }
        return false;
    }

    TextObject result;
    result.id = object.value(QStringLiteral("id")).toString(result.id);
    result.sourceText = object.value(QStringLiteral("sourceText")).toString();
    result.font = FontDescriptor::fromJson(object.value(QStringLiteral("font")).toObject());
    result.typography = TypographyProperties::fromJson(
        object.value(QStringLiteral("typography")).toObject(), &result.fill, formatVersion);
    if (object.value(QStringLiteral("transform")).isObject()) {
        result.transform = ObjectTransform::fromJson(
            object.value(QStringLiteral("transform")).toObject());
    }
    result.visible = object.value(QStringLiteral("visible")).toBool(result.visible);
    result.futureData = object.value(QStringLiteral("futureData")).toObject();
    if (formatVersion >= 6) {
        result.effectStackStrength = qBound<qreal>(0.0,
            object.value(QStringLiteral("effectStackStrength")).toDouble(1.0), 2.0);
    }

    const QJsonValue effectValue = object.value(QStringLiteral("effects"));
    if (!effectValue.isArray()) {
        if (error) {
            *error = QStringLiteral("Text object is missing its effect stack array.");
        }
        return false;
    }
    QString effectError;
    if (formatVersion < 4) {
        // Flat schemas treated IDs as incidental. Parse their ordered effects
        // first; the migration path below replaces every identity by a stable
        // path-derived value, including missing or colliding legacy IDs.
        const QJsonArray serializedEffects = effectValue.toArray();
        for (int index = 0; index < serializedEffects.size(); ++index) {
            if (!serializedEffects.at(index).isObject()) {
                effectError = QStringLiteral("Effect entry %1 is not an object.").arg(index);
                break;
            }
            std::unique_ptr<Effect> effect = effectFromJson(
                serializedEffects.at(index).toObject(), &effectError);
            if (!effect) break;
            result.effects.append(std::move(effect));
        }
    } else {
        result.effects = EffectStack::fromJson(effectValue.toArray(), &effectError);
    }
    if (!effectError.isEmpty()) {
        if (error) {
            *error = effectError;
        }
        return false;
    }

    const QJsonValue deformationValue = object.value(QStringLiteral("deformation"));
    if (!deformationValue.isUndefined()) {
        if (!deformationValue.isObject()) {
            if (error) {
                *error = QStringLiteral("Text object deformation data is not an object.");
            }
            return false;
        }
        QString deformationError;
        if (!ManualDeformation::fromJson(
                deformationValue.toObject(), &result.deformation, &deformationError)) {
            if (error) {
                *error = deformationError;
            }
            return false;
        }
    }

    *textObject = std::move(result);
    return true;
}

QJsonObject layerToJson(const Layer& layer)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), layer.id);
    object.insert(QStringLiteral("name"), layer.name);
    object.insert(QStringLiteral("visible"), layer.visible);
    object.insert(QStringLiteral("locked"), layer.locked);
    QJsonArray objects;
    for (const auto& textObject : layer.objects) {
        if (textObject) {
            objects.append(serializeTextObject(*textObject));
        }
    }
    object.insert(QStringLiteral("objects"), objects);
    return object;
}

bool layerFromJson(const QJsonObject& object, Layer* layer, int formatVersion, QString* error)
{
    if (!layer) {
        return false;
    }
    Layer result;
    result.id = object.value(QStringLiteral("id")).toString(result.id);
    result.name = object.value(QStringLiteral("name")).toString(result.name);
    result.visible = object.value(QStringLiteral("visible")).toBool(result.visible);
    result.locked = object.value(QStringLiteral("locked")).toBool(result.locked);
    result.objects.clear();

    const QJsonValue objectsValue = object.value(QStringLiteral("objects"));
    if (!objectsValue.isArray()) {
        if (error) {
            *error = QStringLiteral("Layer is missing its object array.");
        }
        return false;
    }
    const QJsonArray objects = objectsValue.toArray();
    for (int index = 0; index < objects.size(); ++index) {
        if (!objects.at(index).isObject()) {
            if (error) {
                *error = QStringLiteral("Layer object %1 is not an object.").arg(index);
            }
            return false;
        }
        auto textObject = std::make_unique<TextObject>();
        if (!deserializeTextObject(objects.at(index).toObject(), textObject.get(), formatVersion, error)) {
            return false;
        }
        result.objects.push_back(std::move(textObject));
    }
    *layer = std::move(result);
    return true;
}

QJsonObject pageToJson(const Page& page)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), page.id);
    object.insert(QStringLiteral("name"), page.name);
    object.insert(QStringLiteral("width"), page.size.width());
    object.insert(QStringLiteral("height"), page.size.height());
    object.insert(QStringLiteral("background"), page.background.name(QColor::HexArgb));
    QJsonArray layers;
    for (const auto& layer : page.layers) {
        if (layer) {
            layers.append(layerToJson(*layer));
        }
    }
    object.insert(QStringLiteral("layers"), layers);
    return object;
}

bool pageFromJson(const QJsonObject& object, Page* page, int formatVersion, QString* error)
{
    if (!page) {
        return false;
    }
    Page result;
    result.id = object.value(QStringLiteral("id")).toString(result.id);
    result.name = object.value(QStringLiteral("name")).toString(result.name);
    result.size = QSizeF(object.value(QStringLiteral("width")).toDouble(result.size.width()),
                         object.value(QStringLiteral("height")).toDouble(result.size.height()));
    const QColor background(object.value(QStringLiteral("background")).toString());
    if (background.isValid()) {
        result.background = background;
    }
    result.layers.clear();

    const QJsonValue layersValue = object.value(QStringLiteral("layers"));
    if (!layersValue.isArray()) {
        if (error) {
            *error = QStringLiteral("Page is missing its layer array.");
        }
        return false;
    }
    const QJsonArray layers = layersValue.toArray();
    for (int index = 0; index < layers.size(); ++index) {
        if (!layers.at(index).isObject()) {
            if (error) {
                *error = QStringLiteral("Page layer %1 is not an object.").arg(index);
            }
            return false;
        }
        auto layer = std::make_unique<Layer>();
        if (!layerFromJson(layers.at(index).toObject(), layer.get(), formatVersion, error)) {
            return false;
        }
        result.layers.push_back(std::move(layer));
    }
    if (result.layers.empty()) {
        result.layers.push_back(std::make_unique<Layer>());
    }
    *page = std::move(result);
    return true;
}

} // namespace

ProjectResourceLimits ProjectSerializer::resourceLimits()
{
    return {};
}

namespace {
bool validateInputSize(qint64 bytes,
                       qint64 maximum,
                       const QString& context,
                       QString* error)
{
    if (bytes >= 0 && bytes <= maximum) return true;
    if (error) {
        *error = QStringLiteral("%1 input is %2 bytes; maximum is %3 bytes.")
                     .arg(context)
                     .arg(bytes)
                     .arg(maximum);
    }
    return false;
}
} // namespace

bool ProjectSerializer::validateProjectInputSize(qint64 bytes, QString* error)
{
    return validateInputSize(
        bytes, resourceLimits().maximumProjectInputBytes, QStringLiteral("Project"), error);
}

bool ProjectSerializer::validateClipboardInputSize(qint64 bytes, QString* error)
{
    return validateInputSize(
        bytes, resourceLimits().maximumClipboardInputBytes, QStringLiteral("Clipboard object"), error);
}

QJsonObject ProjectSerializer::textObjectToJson(const TextObject& object)
{
    return serializeTextObject(object);
}

bool ProjectSerializer::textObjectFromJson(const QJsonObject& json,
                                           TextObject* object,
                                           QString* error)
{
    ResourceBudgetTracker tracker;
    if (!validateTextObjectResources(
            json, QStringLiteral("object"), resourceLimits(), &tracker, error)) {
        return false;
    }
    return deserializeTextObject(json, object, Document::CurrentFormatVersion, error);
}

bool ProjectSerializer::validateResourceBudget(const QJsonDocument& json,
                                               const ProjectResourceLimits& limits,
                                               QString* error)
{
    if (json.isArray()) {
        ResourceBudgetTracker tracker;
        return validateObjectArrayResources(
            json.array(), QStringLiteral("objects"), limits, &tracker, error);
    }
    if (!json.isObject()) {
        if (error) *error = QStringLiteral("Serialized resource budget input must be an object or array.");
        return false;
    }
    const QJsonObject root = json.object();
    return validateProjectResources(
        root, root.value(QStringLiteral("formatVersion")).toInt(-1), limits, error);
}

QJsonDocument ProjectSerializer::toJson(const Document& document)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("vectorTypographyProject"));
    root.insert(QStringLiteral("formatVersion"), Document::CurrentFormatVersion);

    QJsonObject metadata = document.metadata;
    metadata.insert(QStringLiteral("title"), document.title);
    metadata.insert(QStringLiteral("createdAt"), document.createdAt.toString(Qt::ISODateWithMs));
    metadata.insert(QStringLiteral("modifiedAt"), document.modifiedAt.toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("metadata"), metadata);
    root.insert(QStringLiteral("resources"), document.resources);

    root.insert(QStringLiteral("currentPageId"), document.currentPageId);
    root.insert(QStringLiteral("activeLayerId"), document.activeLayerId);
    root.insert(QStringLiteral("activeObjectId"), document.activeObjectId);

    QJsonArray pages;
    for (const auto& page : document.pages) {
        if (page) {
            pages.append(pageToJson(*page));
        }
    }
    root.insert(QStringLiteral("pages"), pages);

    // Keep a read-only compatibility projection for tools which produced
    // version 1-3 migration fixtures. New projects are authoritative in the
    // page/layer hierarchy above; this array is never used when loading a v4+
    // document.
    QJsonArray legacyObjects;
    if (const Page* page = document.currentPage()) {
        if (!page->layers.empty() && page->layers.front()) {
            for (const auto& textObject : page->layers.front()->objects) {
                if (textObject) {
                    legacyObjects.append(serializeTextObject(*textObject));
                }
            }
        }
    }
    root.insert(QStringLiteral("objects"), legacyObjects);
    return QJsonDocument(root);
}

bool ProjectSerializer::fromJson(const QJsonDocument& json, Document* document, QString* error)
{
    if (!document || !json.isObject()) {
        if (error) {
            *error = QStringLiteral("Project JSON must contain an object at its root.");
        }
        return false;
    }

    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("vectorTypographyProject")) {
        if (error) {
            *error = QStringLiteral("The file is not a Vector Typography project.");
        }
        return false;
    }

    const int version = root.value(QStringLiteral("formatVersion")).toInt(-1);
    if (version < 1 || version > Document::CurrentFormatVersion) {
        if (error) {
            *error = QStringLiteral("Unsupported project format version %1.").arg(version);
        }
        return false;
    }

    if (!validateProjectResources(root, version, resourceLimits(), error)) {
        return false;
    }

    const QJsonValue pagesValue = root.value(QStringLiteral("pages"));
    const QJsonValue objectsValue = root.value(QStringLiteral("objects"));
    if ((!pagesValue.isArray() || pagesValue.toArray().isEmpty())
        && (!objectsValue.isArray() || objectsValue.toArray().isEmpty())) {
        if (error) {
            *error = QStringLiteral("Project does not contain pages or legacy text objects.");
        }
        return false;
    }

    if (version >= 4 && !validateHierarchicalIdentity(root, error)) {
        return false;
    }

    Document result;
    // Loading an older project migrates it into the current in-memory schema;
    // the next save writes the current format version.
    result.formatVersion = Document::CurrentFormatVersion;
    result.pages.clear();
    const QJsonObject metadata = root.value(QStringLiteral("metadata")).toObject();
    result.metadata = metadata;
    result.title = metadata.value(QStringLiteral("title")).toString(result.title);
    result.createdAt = QDateTime::fromString(
        metadata.value(QStringLiteral("createdAt")).toString(), Qt::ISODateWithMs);
    result.modifiedAt = QDateTime::fromString(
        metadata.value(QStringLiteral("modifiedAt")).toString(), Qt::ISODateWithMs);
    if (!result.createdAt.isValid()) {
        result.createdAt = QDateTime::currentDateTimeUtc();
    }
    if (!result.modifiedAt.isValid()) {
        result.modifiedAt = result.createdAt;
    }
    result.resources = root.value(QStringLiteral("resources")).toObject();

    if (version >= 4 && pagesValue.isArray() && !pagesValue.toArray().isEmpty()) {
        const QJsonArray pages = pagesValue.toArray();
        for (int index = 0; index < pages.size(); ++index) {
            if (!pages.at(index).isObject()) {
                if (error) {
                    *error = QStringLiteral("Project page %1 is not an object.").arg(index);
                }
                return false;
            }
            auto page = std::make_unique<Page>();
            if (!pageFromJson(pages.at(index).toObject(), page.get(), version, error)) {
                return false;
            }
            result.pages.push_back(std::move(page));
        }
        result.currentPageId = root.value(QStringLiteral("currentPageId"))
                                   .toString(result.pages.front()->id);
        result.activeLayerId = root.value(QStringLiteral("activeLayerId"))
                                   .toString(result.pages.front()->layers.front()->id);
        result.activeObjectId = root.value(QStringLiteral("activeObjectId")).toString();
    } else {
        // Version 1-3 stored a flat objects[] array. Preserve its order by
        // placing every object in one migrated page and layer.
        auto page = std::make_unique<Page>();
        page->id = QStringLiteral("migrated-v%1-page-0").arg(version);
        page->name = QStringLiteral("Page 1");
        page->layers.clear();
        auto layer = std::make_unique<Layer>();
        layer->id = QStringLiteral("migrated-v%1-page-0-layer-0").arg(version);
        layer->name = QStringLiteral("Layer 1");
        const QJsonArray objects = objectsValue.toArray();
        for (int index = 0; index < objects.size(); ++index) {
            if (!objects.at(index).isObject()) {
                if (error) {
                    *error = QStringLiteral("Project object %1 is not an object.").arg(index);
                }
                return false;
            }
            auto textObject = std::make_unique<TextObject>();
            if (!deserializeTextObject(objects.at(index).toObject(), textObject.get(), version, error)) {
                return false;
            }
            textObject->id = QStringLiteral("migrated-v%1-page-0-layer-0-object-%2")
                                 .arg(version)
                                 .arg(index);
            for (int effectIndex = 0; effectIndex < textObject->effects.size(); ++effectIndex) {
                if (Effect* effect = textObject->effects.at(effectIndex)) {
                    effect->instanceId = QStringLiteral(
                        "migrated-v%1-page-0-layer-0-object-%2-effect-%3")
                                             .arg(version)
                                             .arg(index)
                                             .arg(effectIndex);
                }
            }
            layer->objects.push_back(std::move(textObject));
        }
        const QString migratedLayerId = layer->id;
        const QString migratedObjectId = layer->objects.empty() || !layer->objects.front()
            ? QString()
            : layer->objects.front()->id;
        page->layers.push_back(std::move(layer));
        const QString migratedPageId = page->id;
        result.pages.push_back(std::move(page));
        result.currentPageId = migratedPageId;
        result.activeLayerId = migratedLayerId;
        result.activeObjectId = migratedObjectId;
    }

    if (result.pages.empty()) {
        if (error) {
            *error = QStringLiteral("Project does not contain a usable page.");
        }
        return false;
    }

    if (!result.pageById(result.currentPageId)) {
        result.currentPageId = result.pages.front()->id;
    }
    Page* loadedCurrentPage = result.pageById(result.currentPageId);
    if (!loadedCurrentPage) {
        result.currentPageId = result.pages.front()->id;
        loadedCurrentPage = result.pages.front().get();
    }
    if (!loadedCurrentPage->layerById(result.activeLayerId)) {
        result.activeLayerId = loadedCurrentPage->layers.front()->id;
    }
    bool activeObjectIsLocal = result.activeObjectId.isEmpty();
    if (!result.activeObjectId.isEmpty()) {
        for (const auto& layer : loadedCurrentPage->layers) {
            if (layer && layer->objectById(result.activeObjectId)) {
                activeObjectIsLocal = true;
                break;
            }
        }
    }
    if (!activeObjectIsLocal) {
        result.activeObjectId.clear();
        if (const Layer* layer = result.activeLayer()) {
            if (!layer->objects.empty() && layer->objects.front()) {
                result.activeObjectId = layer->objects.front()->id;
            }
        }
    }

    *document = std::move(result);
    return true;
}

bool ProjectSerializer::saveToFile(const Document& document, const QString& filePath, QString* error)
{
    const QJsonDocument json = toJson(document);
    if (!validateHierarchicalIdentity(json.object(), error)) {
        return false;
    }
    if (!validateProjectResources(
            json.object(), Document::CurrentFormatVersion, resourceLimits(), error)) {
        return false;
    }
    const QByteArray data = json.toJson(QJsonDocument::Indented);
    if (!validateProjectInputSize(data.size(), error)) {
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot open project for writing: %1").arg(file.errorString());
        }
        return false;
    }

    if (file.write(data) != data.size()) {
        if (error) {
            *error = QStringLiteral("Could not write the complete project file: %1").arg(file.errorString());
        }
        return false;
    }
    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("Could not commit project file: %1").arg(file.errorString());
        }
        return false;
    }
    return true;
}

bool ProjectSerializer::loadFromFile(const QString& filePath, Document* document, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot open project: %1").arg(file.errorString());
        }
        return false;
    }

    if (!validateProjectInputSize(file.size(), error)) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("Project JSON is invalid: %1").arg(parseError.errorString());
        }
        return false;
    }
    return fromJson(json, document, error);
}

} // namespace vt
