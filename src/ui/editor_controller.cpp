#include "ui/editor_controller.h"

#include "core/effects/effect.h"
#include "core/scene/scene_evaluator.h"
#include "core/scene/object_frame.h"
#include "core/serialization/project_serializer.h"

#include <QtConcurrentRun>

#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineF>
#include <QMimeData>
#include <QStandardPaths>
#include <QUuid>
#include <QFontDatabase>

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
    return QJsonDocument(left.toJson()).toJson(QJsonDocument::Compact)
        == QJsonDocument(right.toJson()).toJson(QJsonDocument::Compact);
}

FontDescriptor resolvedExactStyle(FontDescriptor descriptor, const QString& family, const QString& style)
{
    descriptor.family = family;
    descriptor.styleName = style;
    const QFont resolved = QFontDatabase::font(family, style, 12);
    descriptor.weight = resolved.weight();
    descriptor.italic = resolved.italic();
    return descriptor;
}

QRectF localReferenceBounds(const TextObject& object)
{
    TextEngine engine;
    const ShapedText shaped = engine.shape(object);
    return GlyphGeometryBuilder::build(shaped, object.typography.fontSize,
                                       object.font.underline, object.font.strikeOut).referenceBounds;
}

Layer* currentPageLayerForObject(Document& document, const QString& objectId)
{
    Page* page = document.currentPage();
    if (!page) {
        return nullptr;
    }
    for (const auto& layer : page->layers) {
        if (layer && layer->objectById(objectId)) {
            return layer.get();
        }
    }
    return nullptr;
}

const Layer* currentPageLayerForObject(const Document& document, const QString& objectId)
{
    const Page* page = document.currentPage();
    if (!page) {
        return nullptr;
    }
    for (const auto& layer : page->layers) {
        if (layer && layer->objectById(objectId)) {
            return layer.get();
        }
    }
    return nullptr;
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
    , m_presetCatalog(m_presetManager)
{
    m_selectionModel = new SelectionModel(this);
    connect(m_selectionModel, &SelectionModel::selectionChanged, this, [this] {
        const QString previousObjectId = m_document.activeObjectId;
        if (!m_selectionModel->activeObjectId().isEmpty()) {
            if (TextObject* object = m_document.objectById(m_selectionModel->activeObjectId())) {
                m_document.activeObjectId = object->id;
            }
        } else {
            m_document.activeObjectId.clear();
        }
        if (previousObjectId != m_document.activeObjectId) {
            m_selectedEffectId.clear();
            emit selectedEffectChanged(m_selectedEffectId);
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
            if (const Layer* layer = currentPageLayerForObject(m_document, object->id);
                layer && layer->visible && !layer->locked) {
                m_document.activeObjectId = object->id;
                return object;
            }
        }
    }
    return nullptr;
}

const TextObject* EditorController::activeObject() const
{
    if (m_selectionModel && !m_selectionModel->activeObjectId().isEmpty()) {
        if (const TextObject* object = m_document.objectById(m_selectionModel->activeObjectId())) {
            if (const Layer* layer = currentPageLayerForObject(m_document, object->id);
                layer && layer->visible && !layer->locked) {
                return object;
            }
        }
    }
    return nullptr;
}

TextObject* EditorController::editableActiveObject()
{
    return activeObject();
}

const TextObject* EditorController::editableActiveObject() const
{
    return activeObject();
}

EditorTool EditorController::tool() const
{
    return m_toolState.tool();
}

BrushTarget EditorController::brushTarget() const
{
    return m_toolState.target();
}

qreal EditorController::brushRadius() const
{
    return m_brushRadius;
}

qreal EditorController::brushStrength() const
{
    return m_brushStrength;
}

qreal EditorController::brushHardness() const
{
    return m_brushHardness;
}

bool EditorController::maskRestoreMode() const
{
    return m_maskRestore;
}

QString EditorController::selectedEffectId() const
{
    return m_selectedEffectId;
}

QStringList EditorController::selectedObjectIds() const
{
    return m_selectionModel ? m_selectionModel->selectedObjectIds() : QStringList();
}

void EditorController::refreshFonts()
{
    m_textEngine.clearCache();
    SceneEvaluator::invalidateFontCaches();
    rebuildScene();
    emit fontsChanged(fontFamilies());
    emit statusMessageChanged(QStringLiteral("System font list refreshed."));
}

void EditorController::setTool(EditorTool tool)
{
    if (m_toolState.tool() == tool) {
        emit toolChanged(tool);
        return;
    }
    m_toolState.setTool(tool);
    emit toolChanged(tool);
    if (const std::optional<BrushMode> mode = m_toolState.brushMode()) {
        emit brushSettingsChanged(*mode,
                                  m_toolState.target(),
                                  m_brushRadius,
                                  m_brushStrength,
                                  m_brushHardness);
    }
}

void EditorController::setBrushSettings(BrushTarget target,
                                        qreal radius,
                                        qreal strength,
                                        qreal hardness)
{
    m_toolState.setTarget(target);
    m_brushRadius = qBound<qreal>(1.0, radius, 100000.0);
    m_brushStrength = qBound<qreal>(0.0, strength, 4.0);
    m_brushHardness = qBound<qreal>(0.0, hardness, 1.0);
    if (const std::optional<BrushMode> mode = m_toolState.brushMode()) {
        emit brushSettingsChanged(*mode,
                                  m_toolState.target(),
                                  m_brushRadius,
                                  m_brushStrength,
                                  m_brushHardness);
    }
}

void EditorController::setMaskBrushSettings(qreal radius,
                                            qreal opacity,
                                            qreal hardness,
                                            bool restore)
{
    m_maskRadius = qBound<qreal>(1.0, radius, 100000.0);
    m_maskOpacity = qBound<qreal>(0.0, opacity, 1.0);
    m_maskHardness = qBound<qreal>(0.0, hardness, 1.0);
    m_maskRestore = restore;
    emit maskSettingsChanged(m_maskRadius, m_maskOpacity, m_maskHardness, m_maskRestore);
}

void EditorController::setMaskRestoreMode(bool restore)
{
    m_maskRestore = restore;
    emit maskSettingsChanged(m_maskRadius, m_maskOpacity, m_maskHardness, m_maskRestore);
}

void EditorController::setSelectedEffectId(const QString& effectId)
{
    if (m_selectedEffectId == effectId) {
        return;
    }
    m_selectedEffectId = effectId;
    emit selectedEffectChanged(m_selectedEffectId);
}

void EditorController::newDocument()
{
    m_document = Document();
    m_previewStroke.reset();
    m_previewObjectId.clear();
    m_previewEffectMask.reset();
    m_previewEffectMaskObjectId.clear();
    m_previewEffectMaskEffectId.clear();
    m_undoStack.clear();
    m_undoStack.setClean();
    m_selectionModel->clear();
    synchronizeSelectionWithDocument();
    m_selectedEffectId.clear();
    m_textEngine.clearCache();
    rebuildScene();
    emit documentChanged();
    emit statusMessageChanged(QStringLiteral("New project created."));
}

void EditorController::setText(const QString& text)
{
    const TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    if (object->sourceText == text) {
        return;
    }
    m_undoStack.push(new SetTextCommand(
        m_document,
        object->sourceText,
        text,
        [this] { onCommandChanged(); }));
}

QVector<PresetCatalogEntry> EditorController::presetCatalogEntries(QString* diagnostics) const
{
    return m_presetCatalog.entries(diagnostics);
}

void EditorController::setEffectStackStrength(qreal strength)
{
    TextObject* object = editableActiveObject();
    if (!object) return;
    const qreal bounded = qBound<qreal>(0.0, strength, 2.0);
    if (nearlyEqual(object->effectStackStrength, bounded)) return;
    if (m_effectStackStrengthGestureActive && object->id == m_effectStackStrengthGestureObjectId) {
        object->effectStackStrength = bounded;
        onCommandChanged();
        return;
    }
    m_undoStack.push(new SetEffectStackStrengthCommand(
        m_document, object->id, object->effectStackStrength, bounded, [this] { onCommandChanged(); }));
}

void EditorController::beginEffectStackStrengthGesture()
{
    TextObject* object = editableActiveObject();
    if (!object) return;
    m_effectStackStrengthGestureActive = true;
    m_effectStackStrengthGestureObjectId = object->id;
    m_effectStackStrengthGestureStart = object->effectStackStrength;
}

void EditorController::endEffectStackStrengthGesture()
{
    if (!m_effectStackStrengthGestureActive) return;
    const QString objectId = m_effectStackStrengthGestureObjectId;
    const qreal start = m_effectStackStrengthGestureStart;
    m_effectStackStrengthGestureActive = false;
    m_effectStackStrengthGestureObjectId.clear();
    TextObject* object = m_document.objectById(objectId);
    if (!object || nearlyEqual(start, object->effectStackStrength)) return;
    const qreal final = object->effectStackStrength;
    object->effectStackStrength = start;
    m_undoStack.push(new SetEffectStackStrengthCommand(m_document, objectId, start, final,
        [this] { onCommandChanged(); }));
}

void EditorController::setTextRange(int start, int end)
{
    if (!editableActiveObject()) {
        return;
    }
    m_selectionModel->setTextRange(start, end);
}

void EditorController::clearTextRange()
{
    m_selectionModel->clearTextRange();
}

void EditorController::setFontFamily(const QString& family)
{
    const TextObject* object = editableActiveObject();
    if (!object || family.isEmpty()) {
        return;
    }
    const QString style = QFontDatabase::styles(family).value(0);
    const FontDescriptor replacement = resolvedExactStyle(object->font, family, style);
    if (replacement == object->font) return;
    m_undoStack.push(new SetFontDescriptorCommand(m_document, object->font, replacement,
                                                  [this] { onCommandChanged(); },
                                                  QStringLiteral("Change font family")));
}

void EditorController::setFontStyle(const QString& styleName)
{
    const TextObject* object = editableActiveObject();
    if (!object || styleName.isEmpty()) {
        return;
    }
    const FontDescriptor replacement = resolvedExactStyle(object->font, object->font.family, styleName);
    if (replacement == object->font) return;
    m_undoStack.push(new SetFontDescriptorCommand(m_document, object->font, replacement,
                                                  [this] { onCommandChanged(); },
                                                  QStringLiteral("Change font style")));
}

void EditorController::setFontWeight(int weight)
{
    const TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    FontDescriptor replacement = object->font;
    replacement.weight = qBound(0, weight, 1000);
    replacement.styleName.clear();
    if (replacement == object->font) return;
    m_undoStack.push(new SetFontDescriptorCommand(m_document, object->font, replacement,
                                                  [this] { onCommandChanged(); },
                                                  QStringLiteral("Change font weight")));
}

void EditorController::setFontItalic(bool italic)
{
    const TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    FontDescriptor replacement = object->font;
    replacement.italic = italic;
    replacement.styleName.clear();
    if (replacement == object->font) return;
    m_undoStack.push(new SetFontDescriptorCommand(m_document, object->font, replacement,
                                                  [this] { onCommandChanged(); },
                                                  QStringLiteral("Toggle italic")));
}

void EditorController::setFontUnderline(bool underline)
{
    const TextObject* object = editableActiveObject();
    if (!object || object->font.underline == underline) {
        return;
    }
    m_undoStack.push(new SetFontDecorationCommand(m_document,
                                                   FontDecoration::Underline,
                                                   object->font.underline,
                                                   underline,
                                                   [this] { onCommandChanged(); }));
}

void EditorController::setFontStrikeOut(bool strikeOut)
{
    const TextObject* object = editableActiveObject();
    if (!object || object->font.strikeOut == strikeOut) {
        return;
    }
    m_undoStack.push(new SetFontDecorationCommand(m_document,
                                                   FontDecoration::StrikeOut,
                                                   object->font.strikeOut,
                                                   strikeOut,
                                                   [this] { onCommandChanged(); }));
}

void EditorController::setFontSize(qreal pointSize)
{
    const qreal boundedSize = qBound<qreal>(1.0, pointSize, 2000.0);
    const TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    const qreal oldSize = object->typography.fontSize;
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
    const TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    const qreal oldTracking = object->typography.trackingEm;
    if (nearlyEqual(oldTracking, boundedTracking)) {
        return;
    }
    m_undoStack.push(new SetTrackingCommand(
        m_document,
        oldTracking,
        boundedTracking,
        [this] { onCommandChanged(); }));
}

void EditorController::setLineSpacing(qreal lineSpacing)
{
    const TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    const qreal bounded = qBound<qreal>(0.5, lineSpacing, 3.0);
    const qreal oldValue = object->typography.lineSpacing;
    if (nearlyEqual(oldValue, bounded)) {
        return;
    }
    m_undoStack.push(new SetLineSpacingCommand(
        m_document, oldValue, bounded, [this] { onCommandChanged(); }));
}

void EditorController::setFillColor(const QColor& color)
{
    if (!color.isValid()) {
        return;
    }
    const TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    const QColor oldColor = object->fill;
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

QString EditorController::createTextObject(const QPointF& position, const QString& text)
{
    Layer* layer = m_document.activeLayer();
    if (!layer || !layer->visible || layer->locked) {
        publishError(QStringLiteral("The active layer is locked or hidden."));
        return {};
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
    return objectId;
}

void EditorController::cancelNewTextObject(const QString& objectId)
{
    if (objectId.isEmpty()) {
        return;
    }
    const TextObject* object = m_document.objectById(objectId);
    const Layer* layer = currentPageLayerForObject(m_document, objectId);
    if (!object || !layer || !layer->visible || layer->locked || !object->sourceText.isEmpty()) {
        return;
    }

    if (m_undoStack.index() == m_undoStack.count()
        && m_undoStack.count() > 0
        && m_undoStack.undoText() == QStringLiteral("Create text object")) {
        m_undoStack.undo();
        m_selectionModel->remove(objectId);
        synchronizeSelectionWithDocument();
        emit documentChanged();
        return;
    }
    deleteObject(objectId);
}

void EditorController::deleteObject(const QString& objectId)
{
    Layer* layer = currentPageLayerForObject(m_document, objectId);
    if (!layer || !layer->visible || layer->locked) {
        return;
    }
    TextObject* object = layer->objectById(objectId);
    if (!object) {
        return;
    }
    int index = -1;
    for (int objectIndex = 0; objectIndex < static_cast<int>(layer->objects.size()); ++objectIndex) {
        if (layer->objects[static_cast<size_t>(objectIndex)]
            && layer->objects[static_cast<size_t>(objectIndex)]->id == objectId) {
            index = objectIndex;
            break;
        }
    }
    if (index < 0) {
        return;
    }
    m_undoStack.push(new RemoveTextObjectCommand(
        m_document, layer->id, *object, index, [this] { onCommandChanged(); }));
    m_selectionModel->remove(objectId);
    synchronizeSelectionWithDocument();
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
        Layer* layer = currentPageLayerForObject(m_document, objectId);
        if (!layer || !layer->visible || layer->locked) {
            continue;
        }
        TextObject* object = layer->objectById(objectId);
        int index = -1;
        for (int objectIndex = 0; objectIndex < static_cast<int>(layer->objects.size()); ++objectIndex) {
            if (layer->objects[static_cast<size_t>(objectIndex)]
                && layer->objects[static_cast<size_t>(objectIndex)]->id == objectId) {
                index = objectIndex;
                break;
            }
        }
        if (object && index >= 0) {
            m_undoStack.push(new RemoveTextObjectCommand(
                m_document, layer->id, *object, index, [this] { onCommandChanged(); }));
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
        const Layer* layer = currentPageLayerForObject(m_document, objectId);
        if (!layer || !layer->visible || layer->locked) {
            continue;
        }
        const TextObject* source = layer->objectById(objectId);
        if (!source) {
            continue;
        }
        TextObject duplicate(*source);
        duplicate.id = createStableId(QStringLiteral("text"));
        duplicate.transform.position += QPointF(24.0, 24.0);
        duplicateIds.push_back(duplicate.id);
        m_undoStack.push(new AddTextObjectCommand(
            m_document, layer->id, duplicate, [this] { onCommandChanged(); },
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
    if (ids.isEmpty()) {
        if (const TextObject* object = editableActiveObject()) {
            ids.push_back(object->id);
        }
    }
    moveObjects(ids, delta);
}

void EditorController::moveObjects(const QStringList& objectIds, const QPointF& delta)
{
    QStringList movableIds;
    for (const QString& id : objectIds) {
        const Layer* layer = currentPageLayerForObject(m_document, id);
        if (!movableIds.contains(id) && layer && layer->objectById(id)
            && layer->visible && !layer->locked) {
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
    const Layer* layer = currentPageLayerForObject(m_document, objectId);
    ObjectTransform normalized = transform;
    normalized.normalizeScale();
    if (!normalized.hasPivot) {
        if (const SceneObjectGeometry* sceneObject = m_sceneGeometry.objectById(objectId)) {
            normalized.pivotLocal = sceneObject->frame.pivotLocal;
            normalized.hasPivot = true;
        }
    }
    if (!object || !layer || !layer->visible || layer->locked
        || object->transform.toJson() == normalized.toJson()) {
        return;
    }
    m_undoStack.push(new SetObjectTransformCommand(
        m_document, objectId, object->transform, normalized, [this] { onCommandChanged(); }));
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

void EditorController::renameCurrentPage(const QString& name)
{
    Page* page = m_document.currentPage();
    const QString trimmed = name.trimmed();
    if (!page || trimmed.isEmpty() || page->name == trimmed) {
        return;
    }
    m_undoStack.push(new SetPageStateCommand(
        m_document,
        page->id,
        SetPageStateCommand::Property::Name,
        page->name,
        trimmed,
        [this] { onCommandChanged(); },
        QStringLiteral("Rename page")));
}

void EditorController::setCurrentPageSize(const QSizeF& size)
{
    Page* page = m_document.currentPage();
    if (!page) {
        return;
    }
    const QSizeF bounded(qBound<qreal>(64.0, size.width(), 10000.0),
                         qBound<qreal>(64.0, size.height(), 10000.0));
    if (page->size == bounded) {
        return;
    }
    m_undoStack.push(new SetPageStateCommand(
        m_document,
        page->id,
        SetPageStateCommand::Property::Size,
        QVariant::fromValue(page->size),
        QVariant::fromValue(bounded),
        [this] { onCommandChanged(); },
        QStringLiteral("Resize page")));
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
    const QString oldPageId = current->id;
    const QString oldLayerId = m_document.activeLayerId;
    const QString replacementLayerId = replacement->layers.empty()
        ? QString()
        : replacement->layers.front()->id;
    m_undoStack.beginMacro(QStringLiteral("Delete page"));
    m_undoStack.push(new SetCurrentPageCommand(
        m_document,
        oldPageId,
        replacement->id,
        oldLayerId,
        replacementLayerId,
        [this] { onCommandChanged(); },
        QStringLiteral("Activate replacement page")));
    m_undoStack.push(new RemovePageCommand(
        m_document, *current, index, [this] { onCommandChanged(); }));
    m_undoStack.endMacro();
    m_document.activeObjectId.clear();
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

void EditorController::movePage(int from, int to)
{
    if (from < 0 || from >= static_cast<int>(m_document.pages.size())
        || to < 0 || to >= static_cast<int>(m_document.pages.size()) || from == to) {
        return;
    }
    m_undoStack.push(new ReorderPageCommand(
        m_document, from, to, [this] { onCommandChanged(); }));
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
    const QString oldLayerId = m_document.activeLayerId;
    m_undoStack.beginMacro(QStringLiteral("Add layer"));
    m_undoStack.push(new AddLayerCommand(
        m_document, page->id, layer, static_cast<int>(page->layers.size()), [this] { onCommandChanged(); }));
    m_undoStack.push(new SetCurrentPageCommand(
        m_document,
        page->id,
        page->id,
        oldLayerId,
        layerId,
        [this] { onCommandChanged(); },
        QStringLiteral("Activate layer")));
    m_undoStack.endMacro();
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
    const QString oldLayerId = layer->id;
    m_undoStack.beginMacro(QStringLiteral("Delete layer"));
    m_undoStack.push(new SetCurrentPageCommand(
        m_document,
        page->id,
        page->id,
        oldLayerId,
        replacementId,
        [this] { onCommandChanged(); },
        QStringLiteral("Activate replacement layer")));
    m_undoStack.push(new RemoveLayerCommand(
        m_document, page->id, removed, index, [this] { onCommandChanged(); }));
    m_undoStack.endMacro();
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

void EditorController::moveLayer(int from, int to)
{
    Page* page = m_document.currentPage();
    if (!page || from < 0 || from >= static_cast<int>(page->layers.size())
        || to < 0 || to >= static_cast<int>(page->layers.size()) || from == to) {
        return;
    }
    m_undoStack.push(new ReorderLayerCommand(
        m_document, page->id, from, to, [this] { onCommandChanged(); }));
}

void EditorController::moveActiveLayerUp()
{
    Page* page = m_document.currentPage();
    if (!page) {
        return;
    }
    for (int index = 0; index < static_cast<int>(page->layers.size()); ++index) {
        if (page->layers[static_cast<size_t>(index)]
            && page->layers[static_cast<size_t>(index)]->id == m_document.activeLayerId) {
            moveLayer(index, index + 1);
            return;
        }
    }
}

void EditorController::moveActiveLayerDown()
{
    Page* page = m_document.currentPage();
    if (!page) {
        return;
    }
    for (int index = 0; index < static_cast<int>(page->layers.size()); ++index) {
        if (page->layers[static_cast<size_t>(index)]
            && page->layers[static_cast<size_t>(index)]->id == m_document.activeLayerId) {
            moveLayer(index, index - 1);
            return;
        }
    }
}

void EditorController::moveObjectToLayer(const QString& objectId,
                                          const QString& destinationLayerId)
{
    Page* page = m_document.currentPage();
    if (!page || objectId.isEmpty() || destinationLayerId.isEmpty()) {
        return;
    }
    Layer* sourceLayer = currentPageLayerForObject(m_document, objectId);
    Layer* destinationLayer = page->layerById(destinationLayerId);
    if (!sourceLayer || !destinationLayer || sourceLayer == destinationLayer
        || !sourceLayer->visible || sourceLayer->locked
        || !destinationLayer->visible || destinationLayer->locked) {
        return;
    }
    TextObject* object = sourceLayer->objectById(objectId);
    if (!object) {
        return;
    }
    int sourceIndex = -1;
    for (int index = 0; index < static_cast<int>(sourceLayer->objects.size()); ++index) {
        if (sourceLayer->objects[static_cast<size_t>(index)]
            && sourceLayer->objects[static_cast<size_t>(index)]->id == objectId) {
            sourceIndex = index;
            break;
        }
    }
    if (sourceIndex < 0) {
        return;
    }

    m_selectionModel->selectSingle(objectId);
    // Moving an object through the Layers tree selects its source object
    // first. Keep the same invariant for direct callers so undo returns to
    // the layer containing the selected object.
    m_document.activeLayerId = sourceLayer->id;
    m_document.activeObjectId = objectId;
    const QString oldActiveLayerId = m_document.activeLayerId;
    m_undoStack.push(new MoveObjectToLayerCommand(
        m_document,
        objectId,
        sourceLayer->id,
        destinationLayer->id,
        sourceIndex,
        static_cast<int>(destinationLayer->objects.size()),
        oldActiveLayerId,
        destinationLayer->id,
        *object,
        [this] { onCommandChanged(); }));
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
    TextObject* object = editableActiveObject();
    if (!object) {
        publishError(QStringLiteral("Select a text object before adding an effect."));
        return;
    }
    std::unique_ptr<Effect> effect = createEffect(typeId);
    if (!effect) {
        publishError(QStringLiteral("Cannot add unknown effect '%1'.").arg(typeId));
        return;
    }
    const QString description = QStringLiteral("Add %1 effect").arg(effect->displayName());
    const QString effectId = effect->instanceId;
    if (m_selectionModel->hasTextRange()) {
        const auto range = m_selectionModel->textRange();
        effect->scope.kind = EffectScopeKind::TextRange;
        effect->scope.start = range.first;
        effect->scope.end = range.second;
    }
    const int index = object->effects.size();
    m_undoStack.push(new AddEffectCommand(
        m_document,
        index,
        std::move(effect),
        [this] { onCommandChanged(); },
        description));
    setSelectedEffectId(effectId);
}

void EditorController::removeEffect(int index)
{
    TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    const Effect* effect = object->effects.at(index);
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
    const TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    const EffectStack& effects = object->effects;
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
    TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    Effect* effect = object->effects.at(index);
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
    TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    Effect* effect = object->effects.at(index);
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
    TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    Effect* effect = object->effects.at(index);
    if (!effect || effect->scope.toJson() == scope.toJson()) {
        return;
    }
    m_undoStack.push(new SetEffectScopeCommand(
        m_document, index, effect->scope, scope, [this] { onCommandChanged(); }));
}

void EditorController::setEffectScopeById(const QString& effectId, const EffectScope& scope)
{
    const TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    const int index = object->effects.indexByInstanceId(effectId);
    if (index >= 0) {
        setEffectScope(index, scope);
    }
}

void EditorController::addEffectMaskStroke(const QString& objectId,
                                            const QString& effectId,
                                            const EffectMaskStroke& stroke)
{
    clearEffectMaskPreview();
    if (objectId.isEmpty() || effectId.isEmpty() || stroke.points.isEmpty()) {
        return;
    }
    TextObject* object = m_document.objectById(objectId);
    if (!object) {
        return;
    }
    const Page* page = m_document.currentPage();
    if (!page) {
        return;
    }
    bool editable = false;
    for (const auto& layer : page->layers) {
        if (layer && layer->objectById(objectId)) {
            editable = layer->visible && !layer->locked;
            break;
        }
    }
    if (!editable) {
        return;
    }
    if (!object->effects.byInstanceId(effectId)) {
        publishError(QStringLiteral("Select an effect before painting its mask."));
        return;
    }

    EffectMaskStroke localStroke = stroke;
    const ObjectFrame frame = m_sceneGeometry.objectById(objectId)
        ? m_sceneGeometry.objectById(objectId)->frame
        : ObjectFrame::fromTransform(object->transform, localReferenceBounds(*object));
    if (!frame.localToPage.isIdentity() || !frame.pageToLocal.isIdentity()) {
        for (QPointF& point : localStroke.points) {
            point = frame.pagePointToLocal(point);
        }
        localStroke.radius = qMax<qreal>(0.1, frame.pageRadiusToLocalEquivalentArea(localStroke.radius));
    }
    localStroke.opacity = qBound<qreal>(0.0, localStroke.opacity, 1.0);
    localStroke.hardness = qBound<qreal>(0.0, localStroke.hardness, 1.0);
    m_undoStack.push(new AddEffectMaskStrokeCommand(
        m_document, objectId, effectId, localStroke, [this] { onCommandChanged(); }));
}

void EditorController::setEffectMaskPreview(const QString& objectId,
                                             const QString& effectId,
                                             const EffectMaskStroke& stroke)
{
    if (objectId.isEmpty() || effectId.isEmpty() || stroke.points.isEmpty()) {
        clearEffectMaskPreview();
        return;
    }
    TextObject* object = m_document.objectById(objectId);
    if (!object || !object->effects.byInstanceId(effectId)) {
        clearEffectMaskPreview();
        return;
    }
    EffectMaskStroke localStroke = stroke;
    const ObjectFrame frame = m_sceneGeometry.objectById(objectId)
        ? m_sceneGeometry.objectById(objectId)->frame
        : ObjectFrame::fromTransform(object->transform, localReferenceBounds(*object));
    for (QPointF& point : localStroke.points) {
        point = frame.pagePointToLocal(point);
    }
    localStroke.radius = qMax<qreal>(0.1, frame.pageRadiusToLocalEquivalentArea(localStroke.radius));
    localStroke.opacity = qBound<qreal>(0.0, localStroke.opacity, 1.0);
    localStroke.hardness = qBound<qreal>(0.0, localStroke.hardness, 1.0);
    m_previewEffectMask = std::move(localStroke);
    m_previewEffectMaskObjectId = objectId;
    m_previewEffectMaskEffectId = effectId;
    rebuildScene();
}

void EditorController::clearEffectMaskPreview()
{
    if (!m_previewEffectMask.has_value()) {
        return;
    }
    m_previewEffectMask.reset();
    m_previewEffectMaskObjectId.clear();
    m_previewEffectMaskEffectId.clear();
    rebuildScene();
}

void EditorController::setEffectMasterStrength(int index, double strength)
{
    TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    Effect* effect = object->effects.at(index);
    if (!effect) {
        return;
    }
    const double bounded = qBound(0.0, strength, 3.0);
    if (nearlyEqual(effect->masterStrength, bounded)) {
        return;
    }
    m_undoStack.push(new SetEffectMasterStrengthCommand(
        m_document, index, effect->masterStrength, bounded, [this] { onCommandChanged(); }));
}

void EditorController::addDeformationStroke(const DeformationStroke& stroke)
{
    addDeformationStroke(m_document.activeObjectId, stroke);
}

void EditorController::addDeformationStroke(const QString& objectId,
                                            const DeformationStroke& stroke)
{
    if (stroke.samples.isEmpty() || !std::isfinite(stroke.radius) || stroke.radius <= 0.0) {
        return;
    }
    m_previewStroke.reset();
    m_previewObjectId.clear();
    m_previewEffectMask.reset();
    m_previewEffectMaskObjectId.clear();
    m_previewEffectMaskEffectId.clear();
    TextObject* object = m_document.objectById(objectId);
    if (!object) {
        return;
    }
    const Page* page = m_document.currentPage();
    bool editable = false;
    if (page) {
        for (const auto& layer : page->layers) {
            if (layer && layer->objectById(objectId)) {
                editable = layer->visible && !layer->locked;
                break;
            }
        }
    }
    if (!editable) {
        return;
    }
    const int index = object->deformation.strokes.size();
    m_undoStack.push(new AddDeformationStrokeCommand(
        m_document,
        objectId,
        index,
        stroke,
        [this] { onCommandChanged(); }));
}

void EditorController::setDeformationPreview(const DeformationStroke& stroke)
{
    setDeformationPreview(m_document.activeObjectId, stroke);
}

void EditorController::setDeformationPreview(const QString& objectId,
                                             const DeformationStroke& stroke)
{
    if (stroke.samples.isEmpty()) {
        clearDeformationPreview();
        return;
    }
    m_previewStroke = stroke;
    m_previewObjectId = objectId;
    rebuildScene();
}

void EditorController::clearDeformationPreview()
{
    if (!m_previewStroke.has_value()) {
        return;
    }
    m_previewStroke.reset();
    m_previewObjectId.clear();
    rebuildScene();
}

void EditorController::clearDeformation()
{
    TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    const ManualDeformation before = object->deformation;
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
    TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    const bool oldEnabled = object->deformation.enabled;
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
    TextObject* object = editableActiveObject();
    if (!object) {
        return;
    }
    const qreal oldStrength = object->deformation.strength;
    if (nearlyEqual(oldStrength, boundedStrength)) {
        return;
    }
    m_undoStack.push(new SetDeformationStrengthCommand(
        m_document,
        oldStrength,
        boundedStrength,
        [this] { onCommandChanged(); }));
}

void EditorController::copySelectedObjects()
{
    const QStringList ids = selectedObjectIds();
    if (ids.isEmpty()) {
        return;
    }
    QJsonArray objects;
    for (const QString& id : ids) {
        const Layer* layer = currentPageLayerForObject(m_document, id);
        const TextObject* object = layer && layer->visible && !layer->locked
            ? layer->objectById(id)
            : nullptr;
        if (object) {
            objects.append(ProjectSerializer::textObjectToJson(*object));
        }
    }
    if (objects.isEmpty()) {
        return;
    }
    auto* mimeData = new QMimeData();
    mimeData->setData(QStringLiteral("application/x-vector-typography-objects"),
                      QJsonDocument(objects).toJson(QJsonDocument::Compact));
    QStringList fallback;
    for (const QJsonValue& value : objects) {
        fallback.push_back(value.toObject().value(QStringLiteral("sourceText")).toString());
    }
    mimeData->setText(fallback.join(QStringLiteral("\n")));
    QGuiApplication::clipboard()->setMimeData(mimeData);
    emit statusMessageChanged(QStringLiteral("Copied %1 object%2.")
                                  .arg(objects.size())
                                  .arg(objects.size() == 1 ? QString() : QStringLiteral("s")));
}

void EditorController::cutSelectedObjects()
{
    if (selectedObjectIds().isEmpty()) {
        return;
    }
    copySelectedObjects();
    deleteSelectedObjects();
}

void EditorController::pasteObjects()
{
    const QMimeData* mimeData = QGuiApplication::clipboard()->mimeData();
    if (!mimeData || !mimeData->hasFormat(QStringLiteral("application/x-vector-typography-objects"))) {
        return;
    }
    Layer* layer = m_document.activeLayer();
    if (!layer || layer->locked || !layer->visible) {
        publishError(QStringLiteral("The active layer is locked or hidden."));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        mimeData->data(QStringLiteral("application/x-vector-typography-objects")), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        publishError(QStringLiteral("The clipboard does not contain editor objects."));
        return;
    }

    QStringList pastedIds;
    m_undoStack.beginMacro(QStringLiteral("Paste objects"));
    for (const QJsonValue& value : document.array()) {
        if (!value.isObject()) {
            continue;
        }
        TextObject object;
        QString error;
        if (!ProjectSerializer::textObjectFromJson(value.toObject(), &object, &error)) {
            continue;
        }
        object.id = createStableId(QStringLiteral("text"));
        object.transform.position += QPointF(24.0, 24.0);
        pastedIds.push_back(object.id);
        m_undoStack.push(new AddTextObjectCommand(
            m_document, layer->id, object, [this] { onCommandChanged(); }, QStringLiteral("Paste object")));
    }
    m_undoStack.endMacro();
    if (!pastedIds.isEmpty()) {
        m_selectionModel->setSelectedObjectIds(pastedIds, pastedIds.front());
        m_document.activeObjectId = pastedIds.front();
        emit documentChanged();
    }
}

void EditorController::selectAllObjects()
{
    QStringList ids;
    for (const SceneObjectGeometry& object : m_sceneGeometry.objects) {
        if (object.visible && !object.locked) {
            ids.push_back(object.objectId);
        }
    }
    m_selectionModel->setSelectedObjectIds(ids, ids.value(0));
    m_document.activeObjectId = ids.value(0);
    emit sceneChanged();
}

bool EditorController::savePreset(const QString& name, QString* error)
{
    const TextObject* object = editableActiveObject();
    if (!object) {
        if (error) {
            *error = QStringLiteral("Select a text object before saving a preset.");
        }
        return false;
    }
    Preset preset;
    preset.name = name.trimmed();
    preset.effects = object->effects;
    const bool saved = m_presetManager.savePreset(preset, error);
    if (saved) {
        m_presetCatalog.invalidateUserPresets();
        emit statusMessageChanged(QStringLiteral("Preset '%1' saved.").arg(preset.name));
    }
    return saved;
}

bool EditorController::applyPreset(const QString& name, QString* error)
{
    TextObject* object = editableActiveObject();
    if (!object) {
        if (error) {
            *error = QStringLiteral("Select a text object before applying a preset.");
        }
        return false;
    }
    Preset preset;
    if (!m_presetManager.loadPreset(name, &preset, error)) {
        return false;
    }

    const EffectStack before = object->effects;
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

bool EditorController::applyPresetById(const QString& id, QString* error)
{
    PresetCatalogEntry entry;
    if (!m_presetCatalog.presetById(id, &entry, error)) return false;
    const QPair<int, int> selectedRange = m_selectionModel->textRange();
    const bool hasRange = selectedRange.first >= 0 && selectedRange.second > selectedRange.first;
    QStringList targets = hasRange ? QStringList{m_document.activeObjectId} : selectedObjectIds();
    if (targets.isEmpty() && !m_document.activeObjectId.isEmpty()) targets.push_back(m_document.activeObjectId);
    int skipped = 0;
    QVector<ApplyPresetToObjectsCommand::Target> changes;
    for (const QString& targetId : targets) {
        Layer* layer = currentPageLayerForObject(m_document, targetId);
        TextObject* object = layer ? layer->objectById(targetId) : nullptr;
        if (!object || !layer->visible || layer->locked) { ++skipped; continue; }
        EffectStack next = object->effects;
        if (hasRange) {
            for (int index = 0; index < entry.preset.effects.size(); ++index) {
                std::unique_ptr<Effect> effect = entry.preset.effects.at(index)->clone();
                effect->instanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                effect->scope = {EffectScopeKind::TextRange, selectedRange.first, selectedRange.second};
                next.append(std::move(effect));
            }
        } else {
            next = entry.preset.effects;
        }
        const qreal nextStrength = hasRange ? object->effectStackStrength : 1.0;
        if (!effectStacksEqual(object->effects, next)
            || !nearlyEqual(object->effectStackStrength, nextStrength)) {
            changes.push_back({targetId, object->effects, std::move(next),
                               object->effectStackStrength, nextStrength});
        }
    }
    if (changes.isEmpty()) {
        if (error && skipped > 0) *error = QStringLiteral("All selected objects are locked or hidden.");
        return skipped == 0;
    }
    m_undoStack.push(new ApplyPresetToObjectsCommand(
        m_document, std::move(changes), [this] { onCommandChanged(); },
        QStringLiteral("Apply preset '%1'").arg(entry.preset.name)));
    if (skipped) emit statusMessageChanged(QStringLiteral("Applied '%1'; skipped %2 locked or hidden object%3.")
        .arg(entry.preset.name).arg(skipped).arg(skipped == 1 ? QString() : QStringLiteral("s")));
    else emit statusMessageChanged(QStringLiteral("Applied '%1'.").arg(entry.preset.name));
    return true;
}

bool EditorController::duplicateBuiltinPreset(const QString& id, QString* error)
{
    Preset copy;
    if (!m_presetCatalog.duplicateBuiltIn(id, &copy, error)) return false;
    const bool saved = m_presetManager.savePreset(std::move(copy), error);
    if (saved) m_presetCatalog.invalidateUserPresets();
    return saved;
}

bool EditorController::deletePresetById(const QString& id, QString* error)
{
    PresetCatalogEntry entry;
    if (!m_presetCatalog.presetById(id, &entry, error)) return false;
    if (entry.builtIn) {
        if (error) *error = QStringLiteral("Built-in presets are immutable.");
        return false;
    }
    const bool deleted = id.startsWith(QStringLiteral("legacy."))
        ? m_presetManager.deletePreset(entry.preset.name, error)
        : m_presetManager.deletePresetById(id, error);
    if (deleted) m_presetCatalog.invalidateUserPresets();
    return deleted;
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
    m_previewObjectId.clear();
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
            if (TextObject* object = layer->objectById(m_previewObjectId.isEmpty()
                                                           ? m_document.activeObjectId
                                                           : m_previewObjectId)) {
                object->deformation.strokes.push_back(*m_previewStroke);
                break;
            }
        }
    }
    if (m_previewEffectMask.has_value()) {
        for (const auto& layer : snapshot.layers) {
            if (!layer) {
                continue;
            }
            if (TextObject* object = layer->objectById(m_previewEffectMaskObjectId)) {
                if (Effect* effect = object->effects.byInstanceId(m_previewEffectMaskEffectId)) {
                    effect->maskStrokes.push_back(*m_previewEffectMask);
                }
                break;
            }
        }
    }
    const quint64 generation = ++m_evaluationGeneration;
    if (m_evaluationWatcher) {
        // The worker remains cancellable only at the task boundary. Retain
        // exactly one latest snapshot instead of queueing every keystroke or
        // brush sample behind the current evaluation.
        m_pendingEvaluation = std::move(snapshot);
        return;
    }
    startEvaluation(std::move(snapshot), generation);
}

void EditorController::startEvaluation(Page snapshot, quint64 generation)
{
    auto* watcher = new QFutureWatcher<SceneGeometry>(this);
    m_evaluationWatcher = watcher;
    connect(watcher, &QFutureWatcher<SceneGeometry>::finished, this, [this, watcher, generation] {
        SceneGeometry scene = watcher->result();
        watcher->deleteLater();
        m_evaluationWatcher = nullptr;
        if (generation == m_evaluationGeneration) {
            publishSceneResult(std::move(scene), generation);
        }
        if (m_pendingEvaluation.has_value()) {
            Page pending = std::move(*m_pendingEvaluation);
            m_pendingEvaluation.reset();
            startEvaluation(std::move(pending), m_evaluationGeneration);
        }
    });
    watcher->setFuture(QtConcurrent::run([snapshot = std::move(snapshot)] {
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
        m_document.activeObjectId.clear();
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
