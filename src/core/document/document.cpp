#include "core/document/document.h"

#include <QFont>
#include <QJsonValue>
#include <QUuid>

#include <stdexcept>
#include <utility>

namespace vt {

QString createStableId(const QString& prefix)
{
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return prefix.isEmpty() ? uuid : prefix + QStringLiteral("-") + uuid;
}

QJsonObject ObjectTransform::toJson() const
{
    return {
        {QStringLiteral("x"), position.x()},
        {QStringLiteral("y"), position.y()},
        {QStringLiteral("rotation"), rotation},
        {QStringLiteral("scaleX"), scale.x()},
        {QStringLiteral("scaleY"), scale.y()},
    };
}

ObjectTransform ObjectTransform::fromJson(const QJsonObject& object)
{
    ObjectTransform transform;
    transform.position = QPointF(object.value(QStringLiteral("x")).toDouble(),
                                 object.value(QStringLiteral("y")).toDouble());
    transform.rotation = object.value(QStringLiteral("rotation")).toDouble(transform.rotation);
    transform.scale = QPointF(object.value(QStringLiteral("scaleX")).toDouble(transform.scale.x()),
                              object.value(QStringLiteral("scaleY")).toDouble(transform.scale.y()));
    if (qFuzzyIsNull(transform.scale.x())) {
        transform.scale.setX(1.0);
    }
    if (qFuzzyIsNull(transform.scale.y())) {
        transform.scale.setY(1.0);
    }
    return transform;
}

TextObject::TextObject()
    : id(createStableId(QStringLiteral("text")))
{
    const QFont defaultFont;
    font.family = defaultFont.family();
    font.styleName = defaultFont.styleName();
    font.weight = defaultFont.weight();
}

TextObject::TextObject(const TextObject& other)
    : id(other.id)
    , sourceText(other.sourceText)
    , font(other.font)
    , typography(other.typography)
    , fill(other.fill)
    , effects(other.effects)
    , deformation(other.deformation)
    , transform(other.transform)
    , visible(other.visible)
    , futureData(other.futureData)
{
}

TextObject& TextObject::operator=(const TextObject& other)
{
    if (this == &other) {
        return *this;
    }
    id = other.id;
    sourceText = other.sourceText;
    font = other.font;
    typography = other.typography;
    fill = other.fill;
    effects = other.effects;
    deformation = other.deformation;
    transform = other.transform;
    visible = other.visible;
    futureData = other.futureData;
    return *this;
}

Layer::Layer()
    : id(createStableId(QStringLiteral("layer")))
{
}

Layer::Layer(const Layer& other)
    : id(other.id)
    , name(other.name)
    , visible(other.visible)
    , locked(other.locked)
{
    objects.reserve(other.objects.size());
    for (const auto& object : other.objects) {
        if (object) {
            objects.push_back(std::make_unique<TextObject>(*object));
        }
    }
}

Layer& Layer::operator=(const Layer& other)
{
    if (this == &other) {
        return *this;
    }
    Layer copy(other);
    *this = std::move(copy);
    return *this;
}

TextObject* Layer::objectById(const QString& objectId)
{
    for (const auto& object : objects) {
        if (object && object->id == objectId) {
            return object.get();
        }
    }
    return nullptr;
}

const TextObject* Layer::objectById(const QString& objectId) const
{
    for (const auto& object : objects) {
        if (object && object->id == objectId) {
            return object.get();
        }
    }
    return nullptr;
}

Page::Page()
    : id(createStableId(QStringLiteral("page")))
{
    layers.push_back(std::make_unique<Layer>());
}

Page::Page(const Page& other)
    : id(other.id)
    , name(other.name)
    , size(other.size)
    , background(other.background)
{
    layers.reserve(other.layers.size());
    for (const auto& layer : other.layers) {
        if (layer) {
            layers.push_back(std::make_unique<Layer>(*layer));
        }
    }
}

Page& Page::operator=(const Page& other)
{
    if (this == &other) {
        return *this;
    }
    Page copy(other);
    *this = std::move(copy);
    return *this;
}

Layer* Page::layerById(const QString& layerId)
{
    for (const auto& layer : layers) {
        if (layer && layer->id == layerId) {
            return layer.get();
        }
    }
    return nullptr;
}

const Layer* Page::layerById(const QString& layerId) const
{
    for (const auto& layer : layers) {
        if (layer && layer->id == layerId) {
            return layer.get();
        }
    }
    return nullptr;
}

Document::Document()
    : createdAt(QDateTime::currentDateTimeUtc())
    , modifiedAt(createdAt)
{
    ensureDefaultScene(false);
}

Document::Document(const Document& other)
    : formatVersion(other.formatVersion)
    , title(other.title)
    , createdAt(other.createdAt)
    , modifiedAt(other.modifiedAt)
    , currentPageId(other.currentPageId)
    , activeLayerId(other.activeLayerId)
    , activeObjectId(other.activeObjectId)
    , metadata(other.metadata)
    , resources(other.resources)
{
    pages.reserve(other.pages.size());
    for (const auto& page : other.pages) {
        if (page) {
            pages.push_back(std::make_unique<Page>(*page));
        }
    }
    ensureDefaultScene(false);
}

Document& Document::operator=(const Document& other)
{
    if (this == &other) {
        return *this;
    }

    Document copy(other);
    *this = std::move(copy);
    return *this;
}

TextObject& Document::primaryTextObject()
{
    ensureDefaultScene(true);
    Layer* layer = activeLayer();
    if (!activeObjectId.isEmpty()) {
        if (TextObject* selected = objectById(activeObjectId)) {
            return *selected;
        }
    }
    if (layer && !layer->objects.empty() && layer->objects.front()) {
        activeObjectId = layer->objects.front()->id;
        return *layer->objects.front();
    }
    throw std::runtime_error("Document could not create a primary text object");
}

const TextObject& Document::primaryTextObject() const
{
    if (!activeObjectId.isEmpty()) {
        if (const TextObject* selected = objectById(activeObjectId)) {
            return *selected;
        }
    }
    const Layer* layer = activeLayer();
    if (!layer || layer->objects.empty() || !layer->objects.front()) {
        throw std::runtime_error("Document has no primary text object");
    }
    return *layer->objects.front();
}

Page* Document::currentPage()
{
    if (pages.empty()) {
        ensureDefaultScene(false);
    }
    for (const auto& page : pages) {
        if (page && page->id == currentPageId) {
            return page.get();
        }
    }
    return pages.empty() ? nullptr : pages.front().get();
}

const Page* Document::currentPage() const
{
    for (const auto& page : pages) {
        if (page && page->id == currentPageId) {
            return page.get();
        }
    }
    return pages.empty() ? nullptr : pages.front().get();
}

Page* Document::pageById(const QString& pageId)
{
    for (const auto& page : pages) {
        if (page && page->id == pageId) {
            return page.get();
        }
    }
    return nullptr;
}

const Page* Document::pageById(const QString& pageId) const
{
    for (const auto& page : pages) {
        if (page && page->id == pageId) {
            return page.get();
        }
    }
    return nullptr;
}

Layer* Document::activeLayer()
{
    Page* page = currentPage();
    if (!page) {
        return nullptr;
    }
    if (Layer* layer = page->layerById(activeLayerId)) {
        return layer;
    }
    if (page->layers.empty()) {
        page->layers.push_back(std::make_unique<Layer>());
    }
    activeLayerId = page->layers.front()->id;
    return page->layers.front().get();
}

const Layer* Document::activeLayer() const
{
    const Page* page = currentPage();
    if (!page) {
        return nullptr;
    }
    if (const Layer* layer = page->layerById(activeLayerId)) {
        return layer;
    }
    return page->layers.empty() ? nullptr : page->layers.front().get();
}

Layer* Document::layerById(const QString& layerId)
{
    for (const auto& page : pages) {
        if (page) {
            if (Layer* layer = page->layerById(layerId)) {
                return layer;
            }
        }
    }
    return nullptr;
}

const Layer* Document::layerById(const QString& layerId) const
{
    for (const auto& page : pages) {
        if (page) {
            if (const Layer* layer = page->layerById(layerId)) {
                return layer;
            }
        }
    }
    return nullptr;
}

TextObject* Document::objectById(const QString& objectId)
{
    for (const auto& page : pages) {
        if (!page) {
            continue;
        }
        for (const auto& layer : page->layers) {
            if (layer) {
                if (TextObject* object = layer->objectById(objectId)) {
                    return object;
                }
            }
        }
    }
    return nullptr;
}

const TextObject* Document::objectById(const QString& objectId) const
{
    for (const auto& page : pages) {
        if (!page) {
            continue;
        }
        for (const auto& layer : page->layers) {
            if (layer) {
                if (const TextObject* object = layer->objectById(objectId)) {
                    return object;
                }
            }
        }
    }
    return nullptr;
}

QVector<TextObject*> Document::objectsOnCurrentPage()
{
    QVector<TextObject*> result;
    Page* page = currentPage();
    if (!page) {
        return result;
    }
    for (const auto& layer : page->layers) {
        if (!layer) {
            continue;
        }
        for (const auto& object : layer->objects) {
            if (object) {
                result.push_back(object.get());
            }
        }
    }
    return result;
}

QVector<const TextObject*> Document::objectsOnCurrentPage() const
{
    QVector<const TextObject*> result;
    const Page* page = currentPage();
    if (!page) {
        return result;
    }
    for (const auto& layer : page->layers) {
        if (!layer) {
            continue;
        }
        for (const auto& object : layer->objects) {
            if (object) {
                result.push_back(object.get());
            }
        }
    }
    return result;
}

bool Document::hasObjects() const
{
    for (const auto& page : pages) {
        if (!page) {
            continue;
        }
        for (const auto& layer : page->layers) {
            if (layer && !layer->objects.empty()) {
                return true;
            }
        }
    }
    return false;
}

void Document::ensureDefaultScene(bool withTextObject)
{
    if (pages.empty()) {
        pages.push_back(std::make_unique<Page>());
    }
    if (currentPageId.isEmpty() || !pageById(currentPageId)) {
        currentPageId = pages.front()->id;
    }
    Page* page = currentPage();
    if (!page) {
        return;
    }
    if (page->layers.empty()) {
        page->layers.push_back(std::make_unique<Layer>());
    }
    if (activeLayerId.isEmpty() || !page->layerById(activeLayerId)) {
        activeLayerId = page->layers.front()->id;
    }
    if (withTextObject) {
        Layer* layer = activeLayer();
        if (layer && layer->objects.empty()) {
            layer->objects.push_back(std::make_unique<TextObject>());
        }
        if (layer && !layer->objects.empty() && !layer->objectById(activeObjectId)) {
            activeObjectId = layer->objects.front()->id;
        }
    }
}

void Document::touchModified()
{
    modifiedAt = QDateTime::currentDateTimeUtc();
}

} // namespace vt
