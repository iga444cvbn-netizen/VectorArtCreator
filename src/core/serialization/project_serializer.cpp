#include "core/serialization/project_serializer.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonParseError>
#include <QSaveFile>

#include <utility>

namespace vt {

namespace {

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
    result.effects = EffectStack::fromJson(effectValue.toArray(), &effectError);
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

QJsonObject ProjectSerializer::textObjectToJson(const TextObject& object)
{
    return serializeTextObject(object);
}

bool ProjectSerializer::textObjectFromJson(const QJsonObject& json,
                                           TextObject* object,
                                           QString* error)
{
    return deserializeTextObject(json, object, Document::CurrentFormatVersion, error);
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

    const QJsonValue pagesValue = root.value(QStringLiteral("pages"));
    const QJsonValue objectsValue = root.value(QStringLiteral("objects"));
    if ((!pagesValue.isArray() || pagesValue.toArray().isEmpty())
        && (!objectsValue.isArray() || objectsValue.toArray().isEmpty())) {
        if (error) {
            *error = QStringLiteral("Project does not contain pages or legacy text objects.");
        }
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
        page->name = QStringLiteral("Page 1");
        page->layers.clear();
        auto layer = std::make_unique<Layer>();
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
    if (!result.layerById(result.activeLayerId)) {
        result.activeLayerId = result.pages.front()->layers.front()->id;
    }
    if (!result.objectById(result.activeObjectId)) {
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
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot open project for writing: %1").arg(file.errorString());
        }
        return false;
    }

    const QByteArray data = toJson(document).toJson(QJsonDocument::Indented);
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
