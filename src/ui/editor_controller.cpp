#include "ui/editor_controller.h"

#include "core/effects/effect.h"
#include "core/scene/scene_evaluator.h"
#include "core/serialization/project_serializer.h"

#include <QtConcurrentRun>

#include <QFontDatabase>
#include <QStandardPaths>

#include <cmath>
#include <utility>

namespace vt {

namespace {

QString defaultPresetDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/presets");
}

bool nearlyEqual(double left, double right)
{
    return std::abs(left - right) < 1.0e-12;
}

bool effectStacksEqual(const EffectStack& left, const EffectStack& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (int index = 0; index < left.size(); ++index) {
        const Effect* leftEffect = left.at(index);
        const Effect* rightEffect = right.at(index);
        if (!leftEffect || !rightEffect
            || leftEffect->typeId() != rightEffect->typeId()
            || leftEffect->enabled != rightEffect->enabled) {
            return false;
        }

        const QVector<EffectParameter> leftParameters = leftEffect->parameterDefinitions();
        const QVector<EffectParameter> rightParameters = rightEffect->parameterDefinitions();
        if (leftParameters.size() != rightParameters.size()) {
            return false;
        }
        for (int parameterIndex = 0; parameterIndex < leftParameters.size(); ++parameterIndex) {
            if (leftParameters[parameterIndex].id != rightParameters[parameterIndex].id
                || !nearlyEqual(leftParameters[parameterIndex].value,
                                rightParameters[parameterIndex].value)) {
                return false;
            }
        }
    }
    return true;
}

std::optional<EffectParameter> findEffectParameter(const Effect& effect, const QString& id)
{
    const QVector<EffectParameter> parameters = effect.parameterDefinitions();
    for (const EffectParameter& parameter : parameters) {
        if (parameter.id == id) {
            return parameter;
        }
    }
    return std::nullopt;
}

} // namespace

EditorController::EditorController(QObject* parent)
    : QObject(parent)
    , m_presetManager(defaultPresetDirectory())
{
    m_selectionModel = new SelectionModel(this);
    connect(m_selectionModel, &SelectionModel::selectionChanged, this, [this] {
        if (!m_selectionModel->activeObjectId().isEmpty()) {
            if (TextObject* object = m_document.objectById(m_selectionModel->activeObjectId())) {
                m_document.activeObjectId = object->id;
            }
        }
        emit documentChanged();
        emit sceneChanged();
    });
    m_undoStack.setClean();
    synchronizeSelectionWithDocument();
    rebuildScene();
}

Document& EditorController::document()
{
    return m_document;
}

const Document& EditorController::document() const
{
    return m_document;
}

const VectorGeometry& EditorController::geometry() const
{
    return m_geometry;
}

const SceneGeometry& EditorController::sceneGeometry() const
{
    return m_sceneGeometry;
}

SelectionModel* EditorController::selectionModel()
{
    return m_selectionModel;
}

const SelectionModel* EditorController::selectionModel() const
{
    return m_selectionModel;
}

QUndoStack* EditorController::undoStack()
{
    return &m_undoStack;
}

bool EditorController::isModified() const
{
    return !m_undoStack.isClean();
}

QStringList EditorController::fontFamilies() const
{
    return QFontDatabase::families();
}

QStringList EditorController::fontStyles(const QString& family) const
{
    if (family.trimmed().isEmpty()) {
        return {};
    }
    return QFontDatabase::styles(family);
}

QStringList EditorController::presetNames(QString* error) const
{
    return m_presetManager.listPresetNames(error);
}

QString EditorController::presetDirectory() const
{
    return m_presetManager.directoryPath();
}

TextObject* EditorController::activeObject()
{
    if (m_selectionModel && !m_selectionModel->activeObjectId().isEmpty()) {
        if (TextObject* object = m_document.objectById(m_selectionModel->activeObjectId())) {
            m_document.activeObjectId = object->id;
            return object;
        }
    }
    return &m_document.primaryTextObject();
}

const TextObject* EditorController::activeObject() const
{
    if (m_selectionModel && !m_selectionModel->activeObjectId().isEmpty()) {
        if (const TextObject* object = m_document.objectById(m_selectionModel->activeObjectId())) {
            return object;
        }
    }
    return &m_document.primaryTextObject();
}

QStringList EditorController::selectedObjectIds() const
{
    return m_selectionModel ? m_selectionModel->selectedObjectIds() : QStringList();
}

void EditorController::refreshFonts()
{
    m_textEngine.clearCache();
    rebuildScene();
    emit fontsChanged(fontFamilies());
    emit statusMessageChanged(QStringLiteral("System font list refreshed."));
}

void EditorController::newDocument()
{
    m_document = Document();
    m_previewStroke.reset();
    m_undoStack.clear();
    m_undoStack.setClean();
    m_selectionModel->clear();
    synchronizeSelectionWithDocument();
    m_textEngine.clearCache();
    rebuildScene();
    emit documentChanged();
    emit statusMessageChanged(QStringLiteral("New project created."));
}

void EditorController::setText(const QString& text)
{
    const TextObject& object = *activeObject();
    if (object.sourceText == text) {
        return;
    }
    m_undoStack.push(new SetTextCommand(
        m_document,
        object.sourceText,
        text,
        [this] { onCommandChanged(); }));
}

void EditorController::setFontFamily(const QString& family)
{
    const TextObject& object = *activeObject();
    if (object.font.family == family) {
        return;
    }
    m_undoStack.push(new SetFontFamilyCommand(
        m_document,
        object.font.family,
        family,
        [this] { onCommandChanged(); }));
}

void EditorController::setFontStyle(const QString& styleName)
{
    const TextObject& object = *activeObject();
    if (object.font.styleName == styleName) {
        return;
    }
    m_undoStack.push(new SetFontStyleCommand(
        m_document,
        object.font.styleName,
        styleName,
        [this] { onCommandChanged(); }));
}

void EditorController::setFontWeight(int weight)
{
    const TextObject& object = *activeObject();
    if (object.font.weight == weight) {
        return;
    }
    m_undoStack.push(new SetFontWeightCommand(
        m_document,
        object.font.weight,
        weight,
        [this] { onCommandChanged(); }));
}

void EditorController::setFontSize(qreal pointSize)
{
    const qreal boundedSize = qBound<qreal>(1.0, pointSize, 2000.0);
    const qreal oldSize = activeObject()->typography.fontSize;
    if (nearlyEqual(oldSize, boundedSize)) {
        return;
    }
    m_undoStack.push(new SetFontSizeCommand(
        m_document,
        oldSize,
        boundedSize,
        [this] { onCommandChanged(); }));
}

void EditorController::setTracking(qreal trackingEm)
{
    const qreal boundedTracking = qBound<qreal>(-1.0, trackingEm, 1.0);
    const qreal oldTracking = activeObject()->typography.trackingEm;
    if (nearlyEqual(oldTracking, boundedTracking)) {
        return;
    }
    m_undoStack.push(new SetTrackingCommand(
        m_document,
        oldTracking,
        boundedTracking,
        [this] { onCommandChanged(); }));
}

void EditorController::setFillColor(const QColor& color)
{
    if (!color.isValid()) {
        return;
    }
    const QColor oldColor = activeObject()->fill;
    if (oldColor == color) {
        return;
    }
    m_undoStack.push(new SetFillColorCommand(
        m_document,
        oldColor,
        color,
        [this] { onCommandChanged(); }));
}

void EditorController::selectObject(const QString& objectId, bool additive)
{
    const SceneObjectGeometry* sceneObject = m_sceneGeometry.objectById(objectId);
    if (!sceneObject || !sceneObject->visible || sceneObject->locked) {
        return;
    }
    if (additive) {
        m_selectionModel->add(objectId);
    } else {
        m_selectionModel->selectSingle(objectId);
    }
    m_document.activeObjectId = objectId;
    m_document.activeLayerId = sceneObject->layerId;
    emit sceneChanged();
}

void EditorController::toggleObjectSelection(const QString& objectId)
{
    const SceneObjectGeometry* sceneObject = m_sceneGeometry.objectById(objectId);
    if (!sceneObject || !sceneObject->visible || sceneObject->locked) {
        return;
    }
    m_selectionModel->toggle(objectId);
    if (!m_selectionModel->activeObjectId().isEmpty()) {
        m_document.activeObjectId = m_selectionModel->activeObjectId();
    }
    emit sceneChanged();
}

void EditorController::clearSelection()
{
    m_selectionModel->clear();
    emit sceneChanged();
}

void EditorController::selectObjectsInRect(const QRectF& rect, bool additive)
{
    QStringList ids = additive ? m_selectionModel->selectedObjectIds() : QStringList();
    for (const SceneObjectGeometry& sceneObject : m_sceneGeometry.objects) {
        if (sceneObject.visible && !sceneObject.locked && rect.intersects(sceneObject.visualBounds)) {
            if (!ids.contains(sceneObject.objectId)) {
                ids.push_back(sceneObject.objectId);
            }
        }
    }
    m_selectionModel->setSelectedObjectIds(ids, ids.value(0));
    m_document.activeObjectId = m_selectionModel->activeObjectId();
    emit sceneChanged();
}

void EditorController::createTextObject(const QPointF& position, const QString& text)
{
    Layer* layer = m_document.activeLayer();
    if (!layer) {
        return;
    }
    TextObject object;
    if (!text.isEmpty()) {
        object.sourceText = text;
    }
    object.transform.position = position;
    const QString objectId = object.id;
    m_undoStack.push(new AddTextObjectCommand(
        m_document, layer->id, object, [this] { onCommandChanged(); }));
    m_document.activeObjectId = objectId;
    m_selectionModel->selectSingle(objectId);
    emit documentChanged();
}

void EditorController::deleteSelectedObjects()
{
    const QStringList ids = selectedObjectIds();
    if (ids.isEmpty()) {
        return;
    }
    m_undoStack.beginMacro(QStringLiteral("Delete objects"));
    for (const QString& objectId : ids) {
        TextObject* object = m_document.objectById(objectId);
        if (!object) {
            continue;
        }
        QString layerId;
        int index = -1;
        for (const auto& page : m_document.pages) {
            if (!page) {
                continue;
            }
            for (const auto& layer : page->layers) {
                if (!layer) {
                    continue;
                }
                for (int objectIndex = 0; objectIndex < static_cast<int>(layer->objects.size()); ++objectIndex) {
                    if (layer->objects[static_cast<size_t>(objectIndex)]
                        && layer->objects[static_cast<size_t>(objectIndex)]->id == objectId) {
                        layerId = layer->id;
                        index = objectIndex;
                        break;
                    }
                }
                if (index >= 0) {
                    break;
                }
            }
            if (index >= 0) {
                break;
            }
        }
        if (index >= 0) {
            m_undoStack.push(new RemoveTextObjectCommand(
                m_document, layerId, *object, index, [this] { onCommandChanged(); }));
        }
    }
    m_undoStack.endMacro();
    m_selectionModel->clear();
    synchronizeSelectionWithDocument();
    emit documentChanged();
}

void EditorController::duplicateSelectedObjects()
{
    const QStringList ids = selectedObjectIds();
    if (ids.isEmpty()) {
        return;
    }
    QStringList duplicateIds;
    m_undoStack.beginMacro(QStringLiteral("Duplicate objects"));
    for (const QString& objectId : ids) {
        const TextObject* source = m_document.objectById(objectId);
        if (!source) {
            continue;
        }
        QString layerId;
        for (const auto& page : m_document.pages) {
            if (!page) {
                continue;
            }
            for (const auto& layer : page->layers) {
                if (layer && layer->objectById(objectId)) {
                    layerId = layer->id;
                    break;
                }
            }
            if (!layerId.isEmpty()) {
                break;
            }
        }
        if (layerId.isEmpty()) {
            continue;
        }
        TextObject duplicate(*source);
        duplicate.id = createStableId(QStringLiteral("text"));
        duplicate.transform.position += QPointF(24.0, 24.0);
        duplicateIds.push_back(duplicate.id);
        m_undoStack.push(new AddTextObjectCommand(
            m_document, layerId, duplicate, [this] { onCommandChanged(); },
            QStringLiteral("Duplicate text object")));
    }
    m_undoStack.endMacro();
    m_selectionModel->setSelectedObjectIds(duplicateIds, duplicateIds.value(0));
    m_document.activeObjectId = duplicateIds.value(0);
    emit documentChanged();
}

void EditorController::moveSelectedObjects(const QPointF& delta)
{
    QStringList ids = selectedObjectIds();
    if (ids.isEmpty() && activeObject()) {
        ids.push_back(activeObject()->id);
    }
    QStringList movableIds;
    for (const QString& id : ids) {
        bool locked = false;
        for (const auto& page : m_document.pages) {
            if (!page || page->id != m_document.currentPageId) {
                continue;
            }
            for (const auto& layer : page->layers) {
                if (layer && layer->objectById(id)) {
                    locked = layer->locked;
                    break;
                }
            }
        }
        if (m_document.objectById(id) && !locked) {
            movableIds.push_back(id);
        }
    }
    if (movableIds.isEmpty() || delta.isNull()) {
        return;
    }
    m_undoStack.push(new MoveObjectsCommand(
        m_document, movableIds, delta, [this] { onCommandChanged(); }));
}

void EditorController::nudgeSelectedObjects(const QPointF& delta)
{
    moveSelectedObjects(delta);
}

void EditorController::setObjectTransform(const QString& objectId, const ObjectTransform& transform)
{
    TextObject* object = m_document.objectById(objectId);
    if (!object || object->transform.toJson() == transform.toJson()) {
        return;
    }
    m_undoStack.push(new SetObjectTransformCommand(
        m_document, objectId, object->transform, transform, [this] { onCommandChanged(); }));
}

void EditorController::addPage()
{
    Page page;
    page.name = QStringLiteral("Page %1").arg(m_document.pages.size() + 1);
    const QString pageId = page.id;
    const QString layerId = page.layers.empty() ? QString() : page.layers.front()->id;
    const QString oldPageId = m_document.currentPageId;
    const QString oldLayerId = m_document.activeLayerId;
    m_undoStack.beginMacro(QStringLiteral("Add page"));
    m_undoStack.push(new AddPageCommand(
        m_document, page, static_cast<int>(m_document.pages.size()), [this] { onCommandChanged(); }));
    m_undoStack.push(new SetCurrentPageCommand(
        m_document, oldPageId, pageId, oldLayerId, layerId, [this] { onCommandChanged(); }));
    m_undoStack.endMacro();
    m_document.activeObjectId.clear();
    synchronizeSelectionWithDocument();
    emit documentChanged();
}

void EditorController::duplicateCurrentPage()
{
    const Page* current = m_document.currentPage();
    if (!current) {
        return;
    }
    Page duplicate(*current);
    duplicate.id = createStableId(QStringLiteral("page"));
    duplicate.name = current->name + QStringLiteral(" copy");
    for (const auto& layer : duplicate.layers) {
        if (!layer) {
            continue;
        }
        layer->id = createStableId(QStringLiteral("layer"));
        for (const auto& object : layer->objects) {
            if (object) {
                object->id = createStableId(QStringLiteral("text"));
            }
        }
    }
    const QString pageId = duplicate.id;
    const QString layerId = duplicate.layers.empty() ? QString() : duplicate.layers.front()->id;
    const QString oldPageId = m_document.currentPageId;
    const QString oldLayerId = m_document.activeLayerId;
    m_undoStack.beginMacro(QStringLiteral("Duplicate page"));
    m_undoStack.push(new AddPageCommand(
        m_document, duplicate, static_cast<int>(m_document.pages.size()), [this] { onCommandChanged(); }));
    m_undoStack.push(new SetCurrentPageCommand(
        m_document, oldPageId, pageId, oldLayerId, layerId, [this] { onCommandChanged(); }));
    m_undoStack.endMacro();
    synchronizeSelectionWithDocument();
    emit documentChanged();
}

void EditorController::removeCurrentPage()
{
    if (m_document.pages.size() <= 1) {
        publishError(QStringLiteral("A project must keep at least one page."));
        return;
    }
    Page* current = m_document.currentPage();
    if (!current) {
        return;
    }
    const QString removedId = current->id;
    int index = 0;
    for (; index < static_cast<int>(m_document.pages.size()); ++index) {
        if (m_document.pages[static_cast<size_t>(index)]
            && m_document.pages[static_cast<size_t>(index)]->id == removedId) {
            break;
        }
    }
    const int replacementIndex = index > 0 ? index - 1 : 1;
    Page* replacement = m_document.pages[static_cast<size_t>(replacementIndex)].get();
    m_document.currentPageId = replacement->id;
    m_document.activeLayerId = replacement->layers.empty() ? QString() : replacement->layers.front()->id;
    m_document.activeObjectId.clear();
    m_undoStack.push(new RemovePageCommand(
        m_document, *current, index, [this] { onCommandChanged(); }));
    synchronizeSelectionWithDocument();
    emit documentChanged();
}

void EditorController::switchPage(const QString& pageId)
{
    Page* page = m_document.pageById(pageId);
    if (!page || pageId == m_document.currentPageId) {
        return;
    }
    const QString oldPageId = m_document.currentPageId;
    const QString oldLayerId = m_document.activeLayerId;
    const QString newLayerId = page->layers.empty() ? QString() : page->layers.front()->id;
    m_undoStack.push(new SetCurrentPageCommand(
        m_document, oldPageId, pageId, oldLayerId, newLayerId, [this] { onCommandChanged(); }));
    m_document.activeObjectId.clear();
    synchronizeSelectionWithDocument();
    emit documentChanged();
}

void EditorController::addLayer()
{
    Page* page = m_document.currentPage();
    if (!page) {
        return;
    }
    Layer layer;
    layer.name = QStringLiteral("Layer %1").arg(page->layers.size() + 1);
    const QString layerId = layer.id;
    m_undoStack.push(new AddLayerCommand(
        m_document, page->id, layer, static_cast<int>(page->layers.size()), [this] { onCommandChanged(); }));
    m_document.activeLayerId = layerId;
    m_document.activeObjectId.clear();
    m_selectionModel->clear();
    emit documentChanged();
}

void EditorController::removeActiveLayer()
{
    Page* page = m_document.currentPage();
    Layer* layer = m_document.activeLayer();
    if (!page || !layer || page->layers.size() <= 1) {
        publishError(QStringLiteral("A page must keep at least one layer."));
        return;
    }
    int index = 0;
    for (; index < static_cast<int>(page->layers.size()); ++index) {
        if (page->layers[static_cast<size_t>(index)]
            && page->layers[static_cast<size_t>(index)]->id == layer->id) {
            break;
        }
    }
    const int replacementIndex = index > 0 ? index - 1 : 1;
    const QString replacementId = page->layers[static_cast<size_t>(replacementIndex)]->id;
    const Layer removed = *layer;
    m_undoStack.push(new RemoveLayerCommand(
        m_document, page->id, removed, index, [this] { onCommandChanged(); }));
    m_document.activeLayerId = replacementId;
    m_document.activeObjectId.clear();
    m_selectionModel->clear();
    emit documentChanged();
}

void EditorController::renameActiveLayer(const QString& name)
{
    Layer* layer = m_document.activeLayer();
    if (!layer || layer->name == name.trimmed() || name.trimmed().isEmpty()) {
        return;
    }
    m_undoStack.push(new SetLayerStateCommand(
        m_document, layer->id, SetLayerStateCommand::Property::Name,
        layer->name, name.trimmed(), [this] { onCommandChanged(); }));
}

void EditorController::setActiveLayerVisible(bool visible)
{
    Layer* layer = m_document.activeLayer();
    if (!layer || layer->visible == visible) {
        return;
    }
    m_undoStack.push(new SetLayerStateCommand(
        m_document, layer->id, SetLayerStateCommand::Property::Visible,
        layer->visible, visible, [this] { onCommandChanged(); }));
}

void EditorController::setActiveLayerLocked(bool locked)
{
    Layer* layer = m_document.activeLayer();
    if (!layer || layer->locked == locked) {
        return;
    }
    m_undoStack.push(new SetLayerStateCommand(
        m_document, layer->id, SetLayerStateCommand::Property::Locked,
        layer->locked, locked, [this] { onCommandChanged(); }));
}

void EditorController::switchLayer(const QString& layerId)
{
    if (!m_document.layerById(layerId) || layerId == m_document.activeLayerId) {
        return;
    }
    m_undoStack.push(new SetCurrentPageCommand(
        m_document, m_document.currentPageId, m_document.currentPageId,
        m_document.activeLayerId, layerId, [this] { onCommandChanged(); },
        QStringLiteral("Switch layer")));
    m_document.activeObjectId.clear();
    synchronizeSelectionWithDocument();
    emit documentChanged();
}

void EditorController::addEffect(const QString& typeId)
{
    std::unique_ptr<Effect> effect = createEffect(typeId);
    if (!effect) {
        publishError(QStringLiteral("Cannot add unknown effect '%1'.").arg(typeId));
        return;
    }
    const QString description = QStringLiteral("Add %1 effect").arg(effect->displayName());
    const int index = activeObject()->effects.size();
    m_undoStack.push(new AddEffectCommand(
        m_document,
        index,
        std::move(effect),
        [this] { onCommandChanged(); },
        description));
}

void EditorController::removeEffect(int index)
{
    const Effect* effect = activeObject()->effects.at(index);
    if (!effect) {
        return;
    }
    m_undoStack.push(new RemoveEffectCommand(
        m_document,
        index,
        effect->clone(),
        [this] { onCommandChanged(); }));
}

void EditorController::moveEffect(int from, int to)
{
    const EffectStack& effects = activeObject()->effects;
    if (from < 0 || from >= effects.size() || to < 0 || to >= effects.size() || from == to) {
        return;
    }
    m_undoStack.push(new ReorderEffectCommand(
        m_document,
        from,
        to,
        [this] { onCommandChanged(); }));
}

void EditorController::setEffectEnabled(int index, bool enabled)
{
    Effect* effect = activeObject()->effects.at(index);
    if (!effect || effect->enabled == enabled) {
        return;
    }
    m_undoStack.push(new SetEffectEnabledCommand(
        m_document,
        index,
        effect->enabled,
        enabled,
        [this] { onCommandChanged(); }));
}

void EditorController::setEffectParameter(int index, const QString& parameterId, double value)
{
    Effect* effect = activeObject()->effects.at(index);
    if (!effect) {
        return;
    }
    const std::optional<EffectParameter> oldParameter = findEffectParameter(*effect, parameterId);
    if (!oldParameter.has_value()) {
        return;
    }

    std::unique_ptr<Effect> candidate = effect->clone();
    if (!candidate || !candidate->setParameter(parameterId, value)) {
        return;
    }
    const std::optional<EffectParameter> newParameter = findEffectParameter(*candidate, parameterId);
    if (!newParameter.has_value() || nearlyEqual(oldParameter->value, newParameter->value)) {
        return;
    }

    m_undoStack.push(new SetEffectParameterCommand(
        m_document,
        index,
        parameterId,
        oldParameter->value,
        newParameter->value,
        [this] { onCommandChanged(); }));
}

void EditorController::setEffectScope(int index, const EffectScope& scope)
{
    Effect* effect = activeObject()->effects.at(index);
    if (!effect || effect->scope.toJson() == scope.toJson()) {
        return;
    }
    m_undoStack.push(new SetEffectScopeCommand(
        m_document, index, effect->scope, scope, [this] { onCommandChanged(); }));
}

void EditorController::setEffectMasterStrength(int index, double strength)
{
    Effect* effect = activeObject()->effects.at(index);
    if (!effect) {
        return;
    }
    const double bounded = qBound(0.0, strength, 1.0);
    if (nearlyEqual(effect->masterStrength, bounded)) {
        return;
    }
    m_undoStack.push(new SetEffectMasterStrengthCommand(
        m_document, index, effect->masterStrength, bounded, [this] { onCommandChanged(); }));
}

void EditorController::addDeformationStroke(const DeformationStroke& stroke)
{
    if (stroke.samples.isEmpty() || !std::isfinite(stroke.radius) || stroke.radius <= 0.0) {
        return;
    }
    m_previewStroke.reset();
    const int index = activeObject()->deformation.strokes.size();
    m_undoStack.push(new AddDeformationStrokeCommand(
        m_document,
        index,
        stroke,
        [this] { onCommandChanged(); }));
}

void EditorController::setDeformationPreview(const DeformationStroke& stroke)
{
    if (stroke.samples.isEmpty()) {
        clearDeformationPreview();
        return;
    }
    m_previewStroke = stroke;
    rebuildScene();
}

void EditorController::clearDeformationPreview()
{
    if (!m_previewStroke.has_value()) {
        return;
    }
    m_previewStroke.reset();
    rebuildScene();
}

void EditorController::clearDeformation()
{
    const ManualDeformation before = activeObject()->deformation;
    if (before.strokes.isEmpty()) {
        return;
    }
    m_previewStroke.reset();
    m_undoStack.push(new ClearDeformationCommand(
        m_document,
        before,
        [this] { onCommandChanged(); }));
}

void EditorController::setDeformationEnabled(bool enabled)
{
    const bool oldEnabled = activeObject()->deformation.enabled;
    if (oldEnabled == enabled) {
        return;
    }
    m_undoStack.push(new SetDeformationEnabledCommand(
        m_document,
        oldEnabled,
        enabled,
        [this] { onCommandChanged(); }));
}

void EditorController::setDeformationStrength(qreal strength)
{
    const qreal boundedStrength = qBound<qreal>(0.0, strength, 4.0);
    const qreal oldStrength = activeObject()->deformation.strength;
    if (nearlyEqual(oldStrength, boundedStrength)) {
        return;
    }
    m_undoStack.push(new SetDeformationStrengthCommand(
        m_document,
        oldStrength,
        boundedStrength,
        [this] { onCommandChanged(); }));
}

bool EditorController::savePreset(const QString& name, QString* error)
{
    Preset preset;
    preset.name = name.trimmed();
    preset.effects = activeObject()->effects;
    const bool saved = m_presetManager.savePreset(preset, error);
    if (saved) {
        emit statusMessageChanged(QStringLiteral("Preset '%1' saved.").arg(preset.name));
    }
    return saved;
}

bool EditorController::applyPreset(const QString& name, QString* error)
{
    Preset preset;
    if (!m_presetManager.loadPreset(name, &preset, error)) {
        return false;
    }

    const EffectStack before = activeObject()->effects;
    if (!effectStacksEqual(before, preset.effects)) {
        m_undoStack.push(new ApplyPresetCommand(
            m_document,
            before,
            preset.effects,
            [this] { onCommandChanged(); },
            QStringLiteral("Apply preset '%1'").arg(name)));
    }
    emit statusMessageChanged(QStringLiteral("Preset '%1' applied.").arg(name));
    return true;
}

bool EditorController::deletePreset(const QString& name, QString* error)
{
    const bool deleted = m_presetManager.deletePreset(name, error);
    if (deleted) {
        emit statusMessageChanged(QStringLiteral("Preset '%1' deleted.").arg(name));
    }
    return deleted;
}

bool EditorController::saveProject(const QString& filePath, QString* error)
{
    if (!ProjectSerializer::saveToFile(m_document, filePath, error)) {
        return false;
    }
    m_undoStack.setClean();
    emit documentChanged();
    emit statusMessageChanged(QStringLiteral("Project saved: %1").arg(filePath));
    return true;
}

bool EditorController::openProject(const QString& filePath, QString* error)
{
    Document loaded;
    if (!ProjectSerializer::loadFromFile(filePath, &loaded, error)) {
        return false;
    }
    m_document = std::move(loaded);
    m_previewStroke.reset();
    m_undoStack.clear();
    m_undoStack.setClean();
    m_textEngine.clearCache();
    m_selectionModel->clear();
    synchronizeSelectionWithDocument();
    rebuildScene();
    emit documentChanged();
    emit statusMessageChanged(QStringLiteral("Project opened: %1").arg(filePath));
    return true;
}

bool EditorController::exportSvg(const QString& filePath, QString* error) const
{
    const Page* page = m_document.currentPage();
    if (!page) {
        if (error) {
            *error = QStringLiteral("The project has no current page.");
        }
        return false;
    }
    const SceneGeometry scene = SceneEvaluator::evaluate(*page);
    return m_svgExporter.exportScene(m_document, scene, filePath, error);
}

void EditorController::onCommandChanged()
{
    rebuildScene();
    emit documentChanged();
}

void EditorController::rebuildScene()
{
    const Page* currentPage = m_document.currentPage();
    if (!currentPage) {
        return;
    }
    Page snapshot = *currentPage;
    if (m_previewStroke.has_value()) {
        for (const auto& layer : snapshot.layers) {
            if (!layer) {
                continue;
            }
            if (TextObject* object = layer->objectById(m_document.activeObjectId)) {
                object->deformation.strokes.push_back(*m_previewStroke);
                break;
            }
        }
    }
    const quint64 generation = ++m_evaluationGeneration;
    auto* watcher = new QFutureWatcher<SceneGeometry>(this);
    m_evaluationWatchers.push_back(watcher);
    connect(watcher, &QFutureWatcher<SceneGeometry>::finished, this, [this, watcher, generation] {
        SceneGeometry scene = watcher->result();
        m_evaluationWatchers.removeOne(watcher);
        watcher->deleteLater();
        if (generation != m_evaluationGeneration) {
            return;
        }
        publishSceneResult(std::move(scene), generation);
    });
    watcher->setFuture(QtConcurrent::run([snapshot] {
        return SceneEvaluator::evaluate(snapshot);
    }));
}

void EditorController::publishSceneResult(SceneGeometry scene, quint64 generation)
{
    if (generation != m_evaluationGeneration) {
        return;
    }
    m_sceneGeometry = std::move(scene);
    if (const SceneObjectGeometry* active = m_sceneGeometry.objectById(m_document.activeObjectId)) {
        m_geometry = active->geometry;
        if (!active->error.isEmpty()) {
            publishError(active->error);
        } else if (!active->warning.isEmpty()) {
            emit statusMessageChanged(active->warning);
        } else {
            emit statusMessageChanged(QStringLiteral("Ready."));
        }
    } else {
        m_geometry = VectorGeometry();
        emit statusMessageChanged(QStringLiteral("Ready."));
    }
    emit sceneChanged();
}

void EditorController::synchronizeSelectionWithDocument()
{
    if (!m_selectionModel) {
        return;
    }
    bool activeIsOnCurrentPage = false;
    if (const Page* page = m_document.currentPage()) {
        for (const auto& layer : page->layers) {
            if (layer && layer->objectById(m_document.activeObjectId)) {
                activeIsOnCurrentPage = true;
                break;
            }
        }
    }
    if (m_document.activeObjectId.isEmpty() || !activeIsOnCurrentPage) {
        TextObject& object = m_document.primaryTextObject();
        m_document.activeObjectId = object.id;
    }
    m_selectionModel->setSelectedObjectIds(
        m_document.activeObjectId.isEmpty() ? QStringList() : QStringList{m_document.activeObjectId},
        m_document.activeObjectId);
}

void EditorController::publishError(const QString& message)
{
    emit statusMessageChanged(QStringLiteral("Error: %1").arg(message));
}

} // namespace vt
