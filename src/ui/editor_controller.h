#pragma once

#include "core/document/document.h"
#include "core/export/svg_exporter.h"
#include "core/export/export_payload_builder.h"
#include "platform/vector_clipboard_service.h"
#include "core/presets/preset_manager.h"
#include "core/presets/preset_catalog.h"
#include "core/scene/scene_geometry.h"
#include "core/text/text_engine.h"
#include "core/undo/document_commands.h"
#include "core/undo/scene_commands.h"
#include "ui/deformation_tool_state.h"
#include "ui/selection_model.h"

#include <QColor>
#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QSizeF>
#include <QUndoStack>

#include <memory>
#include <optional>

namespace vt {

class EditorController final : public QObject {
    Q_OBJECT

public:
    explicit EditorController(QObject* parent = nullptr);
    ~EditorController() override;

    [[nodiscard]] Document& document();
    [[nodiscard]] const Document& document() const;
    [[nodiscard]] const VectorGeometry& geometry() const;
    [[nodiscard]] const SceneGeometry& sceneGeometry() const;
    [[nodiscard]] SelectionModel* selectionModel();
    [[nodiscard]] const SelectionModel* selectionModel() const;
    [[nodiscard]] QUndoStack* undoStack();
    [[nodiscard]] bool isModified() const;
    [[nodiscard]] quint64 spatialRevision() const { return m_spatialRevision; }
    [[nodiscard]] QStringList fontFamilies() const;
    [[nodiscard]] QStringList fontStyles(const QString& family) const;
    [[nodiscard]] QStringList presetNames(QString* error = nullptr) const;
    [[nodiscard]] QVector<PresetCatalogEntry> presetCatalogEntries(QString* diagnostics = nullptr) const;
    [[nodiscard]] QString presetDirectory() const;
    [[nodiscard]] TextObject* activeObject();
    [[nodiscard]] const TextObject* activeObject() const;
    [[nodiscard]] QStringList selectedObjectIds() const;
    [[nodiscard]] EditorTool tool() const;
    [[nodiscard]] BrushTarget brushTarget() const;
    [[nodiscard]] qreal brushRadius() const;
    [[nodiscard]] qreal brushStrength() const;
    [[nodiscard]] qreal brushHardness() const;
    [[nodiscard]] bool maskRestoreMode() const;
    [[nodiscard]] QString selectedEffectId() const;
    [[nodiscard]] bool pathLayoutEnabled() const;
    [[nodiscard]] const PathGeometry* activePath() const;

    void refreshFonts();
    void newDocument();
    void setTool(EditorTool tool);
    void setBrushSettings(BrushTarget target, qreal radius, qreal strength, qreal hardness);
    void setMaskBrushSettings(qreal radius, qreal opacity, qreal hardness, bool restore);
    void setMaskRestoreMode(bool restore);
    void setSelectedEffectId(const QString& effectId);
    void setText(const QString& text);
    void setTextRange(int start, int end);
    void clearTextRange();
    void setFontFamily(const QString& family);
    void setFontStyle(const QString& styleName);
    void setFontWeight(int weight);
    void setFontItalic(bool italic);
    void setFontUnderline(bool underline);
    void setFontStrikeOut(bool strikeOut);
    void setFontSize(qreal pointSize);
    void setTracking(qreal tracking);
    void setLineSpacing(qreal lineSpacing);
    void setFillColor(const QColor& color);
    void setPathLayoutEnabled(bool enabled);
    void removePathLayout();
    void setPathStartOffset(qreal offset);
    void setPathBaselineOffset(qreal offset);
    void beginPathStartOffsetGesture();
    void endPathStartOffsetGesture();
    void beginPathBaselineOffsetGesture();
    void endPathBaselineOffsetGesture();
    void setPathReverse(bool reverse);
    void setPathFlip(bool flip);
    void setPathFollowTangent(bool followTangent);
    void setPathOverflow(PathOverflowMode overflow);
    void reversePath();
    void setPathClosed(bool closed);
    void setPathGeometry(const QString& objectId,
                         const PathGeometry& path,
                         quint64 inputSpatialRevision = 0);
    void setEffectStackStrength(qreal strength);
    void beginEffectStackStrengthGesture();
    void endEffectStackStrengthGesture();

    void selectObject(const QString& objectId, bool additive = false);
    void toggleObjectSelection(const QString& objectId);
    void clearSelection();
    void selectObjectsInRect(const QRectF& rect, bool additive = false);
    [[nodiscard]] QString createTextObject(const QPointF& position, const QString& text = {});
    void cancelNewTextObject(const QString& objectId);
    void deleteObject(const QString& objectId);
    void deleteSelectedObjects();
    void duplicateSelectedObjects();
    void moveSelectedObjects(const QPointF& delta);
    void moveObjects(const QStringList& objectIds, const QPointF& delta);
    void nudgeSelectedObjects(const QPointF& delta);
    void setObjectTransform(const QString& objectId, const ObjectTransform& transform);
    void setObjectTransform(const QString& objectId,
                            const ObjectTransform& transform,
                            quint64 inputSpatialRevision);

    void addPage();
    void duplicateCurrentPage();
    void removeCurrentPage();
    void switchPage(const QString& pageId);
    void addLayer();
    void removeActiveLayer();
    void renameActiveLayer(const QString& name);
    void moveLayer(int from, int to);
    void moveActiveLayerUp();
    void moveActiveLayerDown();
    void movePage(int from, int to);
    void moveObjectToLayer(const QString& objectId, const QString& destinationLayerId);
    void setActiveLayerVisible(bool visible);
    void setActiveLayerLocked(bool locked);
    void switchLayer(const QString& layerId);
    void renameCurrentPage(const QString& name);
    void setCurrentPageSize(const QSizeF& size);

    void addEffect(const QString& typeId);
    void removeEffect(int index);
    void moveEffect(int from, int to);
    void setEffectEnabled(int index, bool enabled);
    void setEffectParameter(int index, const QString& parameterId, double value);
    void setEffectScope(int index, const EffectScope& scope);
    void setEffectScopeById(const QString& effectId, const EffectScope& scope);
    void addEffectMaskStroke(const QString& objectId,
                             const QString& effectId,
                             const EffectMaskStroke& stroke);
    void addEffectMaskStroke(const QString& objectId,
                             const QString& effectId,
                             const EffectMaskStroke& stroke,
                             quint64 inputSpatialRevision);
    void setEffectMaskPreview(const QString& objectId,
                              const QString& effectId,
                              const EffectMaskStroke& stroke);
    void setEffectMaskPreview(const QString& objectId,
                              const QString& effectId,
                              const EffectMaskStroke& stroke,
                              quint64 inputSpatialRevision);
    void clearEffectMaskPreview();

    void addDeformationStroke(const DeformationStroke& stroke);
    void addDeformationStroke(const QString& objectId, const DeformationStroke& stroke);
    void addDeformationStroke(const QString& objectId,
                              const DeformationStroke& stroke,
                              quint64 inputSpatialRevision);
    void setDeformationPreview(const DeformationStroke& stroke);
    void setDeformationPreview(const QString& objectId, const DeformationStroke& stroke);
    void setDeformationPreview(const QString& objectId,
                               const DeformationStroke& stroke,
                               quint64 inputSpatialRevision);
    void clearDeformationPreview();
    void clearDeformation();
    void setDeformationEnabled(bool enabled);
    void setDeformationStrength(qreal strength);

    [[nodiscard]] bool savePreset(const QString& name, QString* error = nullptr);
    [[nodiscard]] bool applyPreset(const QString& name, QString* error = nullptr);
    [[nodiscard]] bool applyPresetById(const QString& id, QString* error = nullptr);
    [[nodiscard]] bool duplicateBuiltinPreset(const QString& id, QString* error = nullptr);
    [[nodiscard]] bool deletePresetById(const QString& id, QString* error = nullptr);
    [[nodiscard]] bool deletePreset(const QString& name, QString* error = nullptr);

    void copySelectedObjects();
    void cutSelectedObjects();
    void pasteObjects();
    void selectAllObjects();

    [[nodiscard]] bool saveProject(const QString& filePath, QString* error = nullptr);
    [[nodiscard]] bool openProject(const QString& filePath, QString* error = nullptr);
    [[nodiscard]] bool exportSvg(const QString& filePath, QString* error = nullptr) const;
    [[nodiscard]] bool exportSvg(const QString& filePath, ExportScope scope, QString* error) const;
    [[nodiscard]] bool copyForWord(ExportScope scope, QString* error = nullptr) const;
    [[nodiscard]] ClipboardPublicationResult copyForWordResult(ExportScope scope) const;
    [[nodiscard]] bool canExport(ExportScope scope) const;

public slots:
    void setEffectMasterStrength(int index, double strength);

signals:
    void documentChanged();
    void sceneChanged();
    void toolChanged(EditorTool tool);
    void brushSettingsChanged(BrushMode mode,
                              BrushTarget target,
                              qreal radius,
                              qreal strength,
                              qreal hardness);
    void maskSettingsChanged(qreal radius, qreal strength, qreal hardness, bool restore);
    void selectedEffectChanged(const QString& effectId);
    void statusMessageChanged(const QString& message);
    void fontsChanged(const QStringList& families);

private:
    void onCommandChanged();
    void resetTransientPreviews();
    void rebuildScene();
    void publishSceneResult(SceneGeometry scene,
                            quint64 generation,
                            quint64 spatialRevision);
    void startEvaluation(Page snapshot,
                         quint64 generation,
                         quint64 spatialRevision,
                         bool containsTransientPreview);
    void synchronizeSelectionWithDocument();
    void publishError(const QString& message);
    [[nodiscard]] TextObject* editableActiveObject();
    [[nodiscard]] const TextObject* editableActiveObject() const;
    [[nodiscard]] bool buildExportPayload(ExportScope scope,
                                          VectorExportPayload* payload,
                                          QString* error,
                                          const WorkControl& work) const;
    [[nodiscard]] std::optional<ObjectFrame> authoritativeObjectFrame(
        const QString& objectId);
    [[nodiscard]] bool normalizeDeformationInput(
        const QString& objectId,
        const DeformationStroke& input,
        quint64 inputSpatialRevision,
        DeformationStroke* normalized);
    void pushPathState(const QString& objectId,
                       std::optional<PathGeometry> path,
                       PathTypographyProperties layout,
                       const QString& description);
    void beginPathOffsetGesture(PathOffsetProperty property);
    void endPathOffsetGesture();
    [[nodiscard]] bool pathInputRevisionIsCurrent(const QString& objectId,
                                                  quint64 inputSpatialRevision);

    struct PendingEvaluation {
        Page snapshot;
        quint64 generation = 0;
        quint64 spatialRevision = 0;
        bool containsTransientPreview = false;
    };

    Document m_document;
    TextEngine m_textEngine;
    VectorGeometry m_geometry;
    SceneGeometry m_sceneGeometry;
    SelectionModel* m_selectionModel = nullptr;
    DeformationToolState m_toolState;
    qreal m_brushRadius = 40.0;
    qreal m_brushStrength = 0.7;
    qreal m_brushHardness = 0.5;
    qreal m_maskRadius = 40.0;
    qreal m_maskOpacity = 1.0;
    qreal m_maskHardness = 0.5;
    bool m_maskRestore = false;
    QString m_selectedEffectId;
    quint64 m_evaluationGeneration = 0;
    quint64 m_spatialRevision = 1;
    PresetManager m_presetManager;
    PresetCatalog m_presetCatalog;
    SvgExporter m_svgExporter;
    QUndoStack m_undoStack;
    std::optional<DeformationStroke> m_previewStroke;
    QString m_previewObjectId;
    std::optional<EffectMaskStroke> m_previewEffectMask;
    QString m_previewEffectMaskObjectId;
    QString m_previewEffectMaskEffectId;
    QFutureWatcher<SceneGeometry>* m_evaluationWatcher = nullptr;
    WorkControl m_activeEvaluationWork = WorkControl::unlimited();
    std::optional<PendingEvaluation> m_pendingEvaluation;
    QHash<QString, ObjectFrame> m_authoritativeFrameCache;
    quint64 m_authoritativeFrameCacheRevision = 0;
    bool m_effectStackStrengthGestureActive = false;
    QString m_effectStackStrengthGestureObjectId;
    quint64 m_effectStackStrengthGestureSerial = 0;
    quint64 m_effectStackStrengthGestureToken = 0;
    bool m_pathOffsetGestureActive = false;
    PathOffsetProperty m_pathOffsetGestureProperty = PathOffsetProperty::Start;
    QString m_pathOffsetGestureObjectId;
    quint64 m_pathOffsetGestureSerial = 0;
    quint64 m_pathOffsetGestureToken = 0;
};

} // namespace vt
