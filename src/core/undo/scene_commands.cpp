#include "core/undo/scene_commands.h"

#include <QVariant>

#include <utility>

namespace vt {

namespace {

constexpr int MoveObjectsCommandId = 200;

void insertObject(Layer* layer, const TextObject& object, int index)
{
    if (!layer) {
        return;
    }
    const int insertionIndex = qBound(0, index, static_cast<int>(layer->objects.size()));
    layer->objects.insert(layer->objects.begin() + insertionIndex,
                          std::make_unique<TextObject>(object));
}

} // namespace

AddTextObjectCommand::AddTextObjectCommand(Document& document,
                                           QString layerId,
                                           TextObject object,
                                           DocumentChangeCallback onChanged,
                                           QString description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_layerId(std::move(layerId))
    , m_object(std::move(object))
{
}

void AddTextObjectCommand::undo()
{
    if (Layer* layer = m_document.layerById(m_layerId)) {
        for (auto iterator = layer->objects.begin(); iterator != layer->objects.end(); ++iterator) {
            if (*iterator && (*iterator)->id == m_object.id) {
                layer->objects.erase(iterator);
                notifyChanged();
                return;
            }
        }
    }
}

void AddTextObjectCommand::redo()
{
    if (Layer* layer = m_document.layerById(m_layerId)) {
        if (!layer->objectById(m_object.id)) {
            insertObject(layer, m_object, static_cast<int>(layer->objects.size()));
            notifyChanged();
        }
    }
}

RemoveTextObjectCommand::RemoveTextObjectCommand(Document& document,
                                                 QString layerId,
                                                 TextObject object,
                                                 int index,
                                                 DocumentChangeCallback onChanged,
                                                 QString description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_layerId(std::move(layerId))
    , m_object(std::move(object))
    , m_index(index)
{
}

void RemoveTextObjectCommand::undo()
{
    insertObject(m_document.layerById(m_layerId), m_object, m_index);
    notifyChanged();
}

void RemoveTextObjectCommand::redo()
{
    if (Layer* layer = m_document.layerById(m_layerId)) {
        for (auto iterator = layer->objects.begin(); iterator != layer->objects.end(); ++iterator) {
            if (*iterator && (*iterator)->id == m_object.id) {
                layer->objects.erase(iterator);
                notifyChanged();
                return;
            }
        }
    }
}

MoveObjectsCommand::MoveObjectsCommand(Document& document,
                                       QStringList objectIds,
                                       QPointF delta,
                                       DocumentChangeCallback onChanged,
                                       QString description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_objectIds(std::move(objectIds))
    , m_delta(delta)
{
}

void MoveObjectsCommand::apply(const QPointF& delta)
{
    bool changed = false;
    for (const QString& objectId : m_objectIds) {
        if (TextObject* object = m_document.objectById(objectId)) {
            object->transform.position += delta;
            changed = true;
        }
    }
    if (changed) {
        notifyChanged();
    }
}

void MoveObjectsCommand::undo()
{
    apply(-m_delta);
}

void MoveObjectsCommand::redo()
{
    apply(m_delta);
}

int MoveObjectsCommand::id() const
{
    return MoveObjectsCommandId;
}

bool MoveObjectsCommand::mergeWith(const QUndoCommand* other)
{
    const auto* command = dynamic_cast<const MoveObjectsCommand*>(other);
    if (!command || command->m_objectIds != m_objectIds) {
        return false;
    }
    m_delta += command->m_delta;
    return true;
}

SetObjectTransformCommand::SetObjectTransformCommand(Document& document,
                                                     QString objectId,
                                                     ObjectTransform oldTransform,
                                                     ObjectTransform newTransform,
                                                     DocumentChangeCallback onChanged,
                                                     QString description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_objectId(std::move(objectId))
    , m_oldTransform(std::move(oldTransform))
    , m_newTransform(std::move(newTransform))
{
}

void SetObjectTransformCommand::undo()
{
    if (TextObject* object = m_document.objectById(m_objectId)) {
        object->transform = m_oldTransform;
        notifyChanged();
    }
}

void SetObjectTransformCommand::redo()
{
    if (TextObject* object = m_document.objectById(m_objectId)) {
        object->transform = m_newTransform;
        notifyChanged();
    }
}

AddLayerCommand::AddLayerCommand(Document& document,
                                 QString pageId,
                                 Layer layer,
                                 int index,
                                 DocumentChangeCallback onChanged,
                                 QString description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_pageId(std::move(pageId))
    , m_layer(std::move(layer))
    , m_index(index)
{
}

void AddLayerCommand::undo()
{
    if (Page* page = m_document.pageById(m_pageId)) {
        for (auto iterator = page->layers.begin(); iterator != page->layers.end(); ++iterator) {
            if (*iterator && (*iterator)->id == m_layer.id) {
                page->layers.erase(iterator);
                notifyChanged();
                return;
            }
        }
    }
}

void AddLayerCommand::redo()
{
    Page* page = m_document.pageById(m_pageId);
    if (page && !page->layerById(m_layer.id)) {
        const int insertionIndex = qBound(0, m_index, static_cast<int>(page->layers.size()));
        page->layers.insert(page->layers.begin() + insertionIndex,
                            std::make_unique<Layer>(m_layer));
        notifyChanged();
    }
}

RemoveLayerCommand::RemoveLayerCommand(Document& document,
                                       QString pageId,
                                       Layer layer,
                                       int index,
                                       DocumentChangeCallback onChanged,
                                       QString description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_pageId(std::move(pageId))
    , m_layer(std::move(layer))
    , m_index(index)
{
}

void RemoveLayerCommand::undo()
{
    if (Page* page = m_document.pageById(m_pageId)) {
        const int insertionIndex = qBound(0, m_index, static_cast<int>(page->layers.size()));
        page->layers.insert(page->layers.begin() + insertionIndex,
                            std::make_unique<Layer>(m_layer));
        notifyChanged();
    }
}

void RemoveLayerCommand::redo()
{
    if (Page* page = m_document.pageById(m_pageId)) {
        for (auto iterator = page->layers.begin(); iterator != page->layers.end(); ++iterator) {
            if (*iterator && (*iterator)->id == m_layer.id) {
                page->layers.erase(iterator);
                notifyChanged();
                return;
            }
        }
    }
}

SetLayerStateCommand::SetLayerStateCommand(Document& document,
                                           QString layerId,
                                           Property property,
                                           QVariant oldValue,
                                           QVariant newValue,
                                           DocumentChangeCallback onChanged,
                                           QString description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_layerId(std::move(layerId))
    , m_property(property)
    , m_oldValue(std::move(oldValue))
    , m_newValue(std::move(newValue))
{
}

void SetLayerStateCommand::apply(const QVariant& value)
{
    Layer* layer = m_document.layerById(m_layerId);
    if (!layer) {
        return;
    }
    switch (m_property) {
    case Property::Name:
        layer->name = value.toString();
        break;
    case Property::Visible:
        layer->visible = value.toBool();
        break;
    case Property::Locked:
        layer->locked = value.toBool();
        break;
    }
    notifyChanged();
}

void SetLayerStateCommand::undo()
{
    apply(m_oldValue);
}

void SetLayerStateCommand::redo()
{
    apply(m_newValue);
}

ReorderLayerCommand::ReorderLayerCommand(Document& document,
                                         QString pageId,
                                         int from,
                                         int to,
                                         DocumentChangeCallback onChanged,
                                         QString description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_pageId(std::move(pageId))
    , m_from(from)
    , m_to(to)
{
}

void ReorderLayerCommand::apply(int from, int to)
{
    Page* page = m_document.pageById(m_pageId);
    if (!page || from < 0 || from >= static_cast<int>(page->layers.size())
        || to < 0 || to >= static_cast<int>(page->layers.size()) || from == to) {
        return;
    }
    auto layer = std::move(page->layers[static_cast<size_t>(from)]);
    page->layers.erase(page->layers.begin() + from);
    page->layers.insert(page->layers.begin() + to, std::move(layer));
    notifyChanged();
}

void ReorderLayerCommand::undo()
{
    apply(m_to, m_from);
}

void ReorderLayerCommand::redo()
{
    apply(m_from, m_to);
}

AddPageCommand::AddPageCommand(Document& document,
                               Page page,
                               int index,
                               DocumentChangeCallback onChanged,
                               QString description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_page(std::move(page))
    , m_index(index)
{
}

void AddPageCommand::undo()
{
    for (auto iterator = m_document.pages.begin(); iterator != m_document.pages.end(); ++iterator) {
        if (*iterator && (*iterator)->id == m_page.id) {
            m_document.pages.erase(iterator);
            notifyChanged();
            return;
        }
    }
}

void AddPageCommand::redo()
{
    if (!m_document.pageById(m_page.id)) {
        const int insertionIndex = qBound(0, m_index, static_cast<int>(m_document.pages.size()));
        m_document.pages.insert(m_document.pages.begin() + insertionIndex,
                                std::make_unique<Page>(m_page));
        notifyChanged();
    }
}

RemovePageCommand::RemovePageCommand(Document& document,
                                     Page page,
                                     int index,
                                     DocumentChangeCallback onChanged,
                                     QString description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_page(std::move(page))
    , m_index(index)
{
}

void RemovePageCommand::undo()
{
    const int insertionIndex = qBound(0, m_index, static_cast<int>(m_document.pages.size()));
    m_document.pages.insert(m_document.pages.begin() + insertionIndex,
                            std::make_unique<Page>(m_page));
    notifyChanged();
}

void RemovePageCommand::redo()
{
    for (auto iterator = m_document.pages.begin(); iterator != m_document.pages.end(); ++iterator) {
        if (*iterator && (*iterator)->id == m_page.id) {
            m_document.pages.erase(iterator);
            notifyChanged();
            return;
        }
    }
}

SetCurrentPageCommand::SetCurrentPageCommand(Document& document,
                                             QString oldPageId,
                                             QString newPageId,
                                             QString oldLayerId,
                                             QString newLayerId,
                                             DocumentChangeCallback onChanged,
                                             QString description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_oldPageId(std::move(oldPageId))
    , m_newPageId(std::move(newPageId))
    , m_oldLayerId(std::move(oldLayerId))
    , m_newLayerId(std::move(newLayerId))
{
}

void SetCurrentPageCommand::apply(const QString& pageId, const QString& layerId)
{
    if (!m_document.pageById(pageId)) {
        return;
    }
    m_document.currentPageId = pageId;
    m_document.activeLayerId = layerId;
    notifyChanged();
}

void SetCurrentPageCommand::undo()
{
    apply(m_oldPageId, m_oldLayerId);
}

void SetCurrentPageCommand::redo()
{
    apply(m_newPageId, m_newLayerId);
}

SetPageStateCommand::SetPageStateCommand(Document& document,
                                         QString pageId,
                                         Property property,
                                         QVariant oldValue,
                                         QVariant newValue,
                                         DocumentChangeCallback onChanged,
                                         QString description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_pageId(std::move(pageId))
    , m_property(property)
    , m_oldValue(std::move(oldValue))
    , m_newValue(std::move(newValue))
{
}

void SetPageStateCommand::apply(const QVariant& value)
{
    Page* page = m_document.pageById(m_pageId);
    if (!page) {
        return;
    }
    switch (m_property) {
    case Property::Name:
        page->name = value.toString();
        break;
    case Property::Size:
        page->size = value.toSizeF();
        break;
    }
    notifyChanged();
}

void SetPageStateCommand::undo()
{
    apply(m_oldValue);
}

void SetPageStateCommand::redo()
{
    apply(m_newValue);
}

} // namespace vt
