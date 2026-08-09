#pragma once

#include "core/document/document.h"
#include "core/export/svg_exporter.h"
#include "core/presets/preset_manager.h"
#include "core/scene/scene_geometry.h"
#include "core/text/text_engine.h"
#include "core/undo/document_commands.h"
#include "core/undo/scene_commands.h"
#include "ui/selection_model.h"

#include <QColor>
#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <QUndoStack>

#include <memory>
#include <optional>

namespace vt {

class EditorController final : public QObject {
    Q_OBJECT

public:
    explicit EditorController(QObject* parent = nullptr);

    [[nodiscard]] Document& document();
    [[nodiscard]] const Document& document() const;
    [[nodiscard]] const VectorGeometry& geometry() const;
    [[nodiscard]] const SceneGeometry& sceneGeometry() const;
    [[nodiscard]] SelectionModel* selectionModel();
    [[nodiscard]] const SelectionModel* selectionModel() const;
    [[nodiscard]] QUndoStack* undoStack();
    [[nodiscard]] bool isModified() const;
    [[nodiscard]] QStringList fontFamilies() const;
    [[nodiscard]] QStringList fontStyles(const QString& family) const;
    [[nodiscard]] QStringList presetNames(QString* error = nullptr) const;
    [[nodiscard]] QString presetDirectory() const;
    [[nodiscard]] TextObject* activeObject();
    [[nodiscard]] const TextObject* activeObject() const;
    [[nodiscard]] QStringList selectedObjectIds() const;

    void refreshFonts();
    void newDocument();
    void setText(const QString& text);
    void setFontFamily(const QString& family);
    void setFontStyle(const QString& styleName);
    void setFontWeight(int weight);
    void setFontSize(qreal pointSize);
    void setTracking(qreal tracking);
    void setFillColor(const QColor& color);

    void selectObject(const QString& objectId, bool additive = false);
    void toggleObjectSelection(const QString& objectId);
    void clearSelection();
    void selectObjectsInRect(const QRectF& rect, bool additive = false);
    void createTextObject(const QPointF& position, const QString& text = {});
    void deleteSelectedObjects();
    void duplicateSelectedObjects();
    void moveSelectedObjects(const QPointF& delta);
    void nudgeSelectedObjects(const QPointF& delta);
    void setObjectTransform(const QString& objectId, const ObjectTransform& transform);

    void addPage();
    void duplicateCurrentPage();
    void removeCurrentPage();
    void switchPage(const QString& pageId);
    void addLayer();
    void removeActiveLayer();
    void renameActiveLayer(const QString& name);
    void setActiveLayerVisible(bool visible);
    void setActiveLayerLocked(bool locked);
    void switchLayer(const QString& layerId);

    void addEffect(const QString& typeId);
    void removeEffect(int index);
    void moveEffect(int from, int to);
    void setEffectEnabled(int index, bool enabled);
    void setEffectParameter(int index, const QString& parameterId, double value);
    void setEffectScope(int index, const EffectScope& scope);

    void addDeformationStroke(const DeformationStroke& stroke);
    void setDeformationPreview(const DeformationStroke& stroke);
    void clearDeformationPreview();
    void clearDeformation();
    void setDeformationEnabled(bool enabled);
    void setDeformationStrength(qreal strength);

    [[nodiscard]] bool savePreset(const QString& name, QString* error = nullptr);
    [[nodiscard]] bool applyPreset(const QString& name, QString* error = nullptr);
    [[nodiscard]] bool deletePreset(const QString& name, QString* error = nullptr);

    [[nodiscard]] bool saveProject(const QString& filePath, QString* error = nullptr);
    [[nodiscard]] bool openProject(const QString& filePath, QString* error = nullptr);
    [[nodiscard]] bool exportSvg(const QString& filePath, QString* error = nullptr) const;

public slots:
    void setEffectMasterStrength(int index, double strength);

signals:
    void documentChanged();
    void sceneChanged();
    void statusMessageChanged(const QString& message);
    void fontsChanged(const QStringList& families);

private:
    void onCommandChanged();
    void rebuildScene();
    void publishSceneResult(SceneGeometry scene, quint64 generation);
    void synchronizeSelectionWithDocument();
    void publishError(const QString& message);

    Document m_document;
    TextEngine m_textEngine;
    VectorGeometry m_geometry;
    SceneGeometry m_sceneGeometry;
    SelectionModel* m_selectionModel = nullptr;
    quint64 m_evaluationGeneration = 0;
    PresetManager m_presetManager;
    SvgExporter m_svgExporter;
    QUndoStack m_undoStack;
    std::optional<DeformationStroke> m_previewStroke;
    QVector<QFutureWatcher<SceneGeometry>*> m_evaluationWatchers;
};

} // namespace vt
