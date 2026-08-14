#pragma once

#include "core/deformation/manual_deformation.h"
#include "core/effects/effect.h"
#include "core/geometry/vector_geometry.h"
#include "core/path/path_geometry.h"
#include "core/scene/scene_geometry.h"
#include "ui/deformation_tool_state.h"

#include <QColor>
#include <QEnterEvent>
#include <QFont>
#include <QPoint>
#include <QPlainTextEdit>
#include <QTransform>
#include <QWidget>
#include <QStringList>

class QGraphicsProxyWidget;
class QGraphicsScene;
class QGraphicsView;

namespace vt {

class EditorCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit EditorCanvas(QWidget* parent = nullptr);

    void setScene(const VectorGeometry& geometry, const QColor& fill);
    void setScene(const SceneGeometry& scene,
                  const QStringList& selectedObjectIds = {},
                  const QString& activeObjectId = {});
    void setSelection(const QStringList& selectedObjectIds, const QString& activeObjectId = {});
    void setTextRange(int start, int end);
    void setTool(EditorTool tool);
    void setBrushSettings(BrushMode mode,
                          BrushTarget target,
                          qreal radius,
                          qreal strength,
                          qreal hardness);
    void setMaskRestoreMode(bool restore);
    void setMaskBrushSettings(qreal radius, qreal opacity, qreal hardness, bool restore);
    void setMaskTarget(const QString& objectId);
    void setMaskEnabled(bool enabled);
    void setMaskEffectId(const QString& effectId);
    void setPathEditor(const QString& objectId,
                       const PathGeometry* path,
                       const ObjectFrame& frame,
                       quint64 spatialRevision,
                       bool enabled);
    void setNavigationSettings(const QString& mode, bool invertZoom);
    void beginTextEditing(const QString& objectId,
                          const QString& text,
                          const QFont& font,
                          const QRectF& documentBounds);
    void finishTextEditing();
    [[nodiscard]] bool isTextEditing() const { return m_textEditor != nullptr && m_textEditor->isVisible(); }
    [[nodiscard]] QString editingObjectId() const { return m_editingObjectId; }
    [[nodiscard]] QString editingPageId() const { return m_editingPageId; }
    [[nodiscard]] qreal zoom() const;
    // Read-only coordinate seam used by diagnostics and accessibility tooling.
    // It exposes the same page-to-widget transform used to paint the canvas.
    [[nodiscard]] QPointF mapDocumentToViewport(const QPointF& documentPoint) const;
    [[nodiscard]] QString pathEditorObjectId() const { return m_pathEditObjectId; }
    [[nodiscard]] quint64 pathEditorSpatialRevision() const { return m_pathEditSpatialRevision; }
    [[nodiscard]] BrushMode brushMode() const { return m_brushMode; }
    [[nodiscard]] BrushTarget brushTarget() const { return m_brushTarget; }
    [[nodiscard]] bool maskEnabled() const { return m_maskEnabled; }
    [[nodiscard]] QString maskEffectId() const { return m_maskEffectId; }

public slots:
    void fitContent();
    void zoomIn();
    void zoomOut();
    void zoom100();

signals:
    void zoomChanged(qreal zoom);
    void deformationPreviewChanged(const QString& objectId,
                                   const DeformationStroke& stroke,
                                   quint64 spatialRevision);
    void deformationPreviewCleared();
    void deformationStrokeReady(const QString& objectId,
                                const DeformationStroke& stroke,
                                quint64 spatialRevision);
    void objectClicked(const QString& objectId, bool additive);
    void marqueeSelectionRequested(const QRectF& rect, bool additive);
    void moveCommitted(const QStringList& objectIds, const QPointF& delta);
    void textCreateRequested(const QPointF& position);
    void textEditRequested(const QString& objectId);
    void textEdited(const QString& objectId, const QString& text);
    void textRangeChanged(const QString& objectId, int start, int end);
    void textEditingChanged(bool editing);
    void effectMaskStrokeReady(const QString& objectId, const QString& effectId,
                               const EffectMaskStroke& stroke,
                               quint64 spatialRevision);
    void effectMaskPreviewChanged(const QString& objectId, const QString& effectId,
                                  const EffectMaskStroke& stroke,
                                  quint64 spatialRevision);
    void effectMaskPreviewCleared();
    void objectTransformCommitted(const QString& objectId,
                                  const ObjectTransform& transform,
                                  quint64 spatialRevision);
    void nudgeRequested(const QPointF& delta);
    void deleteRequested();
    void duplicateRequested();
    void pathGeometryCommitted(const QString& objectId,
                               const PathGeometry& path,
                               quint64 spatialRevision);
    void pathEditCancelled();

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setZoom(qreal value);
    [[nodiscard]] QTransform viewTransform() const;
    [[nodiscard]] QPointF documentPosition(const QPointF& widgetPosition) const;
    [[nodiscard]] DeformationStroke currentStroke() const;
    [[nodiscard]] EffectMaskStroke currentMaskStroke() const;
    [[nodiscard]] QString hitTestObject(const QPointF& documentPoint) const;
    [[nodiscard]] int scaleHandleAt(const QPointF& documentPoint) const;
    [[nodiscard]] bool rotationHandleContains(const QPointF& documentPoint) const;
    [[nodiscard]] QPointF rotationHandleCenter(const SceneObjectGeometry& object) const;
    bool beginTransform(const QPointF& documentPoint);
    void updateTransformPreview(const QPointF& documentPoint);
    void applyPreviewTransform(SceneObjectGeometry* preview,
                               const SceneObjectGeometry& source,
                               const ObjectTransform& transform);
    [[nodiscard]] QRectF selectionRectInDocument() const;
    void updateBrushPreview();
    void cancelBrushStroke();
    void updateCursorShape();
    void updateTextEditorGeometry();
    [[nodiscard]] bool handleCanvasMousePress(Qt::MouseButton button,
                                              const QPointF& widgetPosition,
                                              Qt::KeyboardModifiers modifiers);
    [[nodiscard]] bool handleCanvasWheel(const QPoint& angleDelta,
                                         Qt::KeyboardModifiers modifiers);
    [[nodiscard]] bool isOutsideNativeEditor(const QPointF& viewportPosition) const;
    [[nodiscard]] int pathHandleAt(const QPointF& documentPoint,
                                   int* nodeIndex,
                                   int* handleKind) const;
    [[nodiscard]] QPointF pathPointToPage(const QPointF& localPoint) const;
    void updatePathEditPreview(const QPointF& documentPoint);
    void cancelPathEdit();

    VectorGeometry m_geometry;
    SceneGeometry m_sceneGeometry;
    QStringList m_selectedObjectIds;
    QString m_activeObjectId;
    QColor m_fill = QColor(24, 24, 28);
    qreal m_zoom = 1.0;
    QPointF m_panOffset;
    QPointF m_viewCenter;
    QPoint m_lastMousePosition;
    QPoint m_cursorPosition;
    QVector<QPointF> m_brushPositions;
    EditorTool m_tool = EditorTool::Select;
    BrushMode m_brushMode = BrushMode::Push;
    BrushTarget m_brushTarget = BrushTarget::Shape;
    qreal m_brushRadius = 40.0;
    qreal m_brushStrength = 0.7;
    qreal m_brushHardness = 0.5;
    qreal m_maskRadius = 40.0;
    qreal m_maskOpacity = 1.0;
    qreal m_maskHardness = 0.5;
    bool m_maskRestore = false;
    bool m_panning = false;
    bool m_brushing = false;
    bool m_spacePressed = false;
    bool m_hasCursorPosition = false;
    bool m_hasViewCenter = false;
    bool m_hasInitialFit = false;
    bool m_marqueeSelecting = false;
    bool m_marqueeMoved = false;
    bool m_movingObjects = false;
    bool m_transforming = false;
    bool m_rotatingObject = false;
    int m_scaleHandle = -1;
    QPointF m_moveStartDocument;
    QPointF m_lastMoveDocument;
    QRectF m_marqueeRect;
    QPointF m_marqueeStartWidget;
    QStringList m_moveObjectIds;
    SceneGeometry m_sceneBeforeMove;
    SceneGeometry m_sceneBeforeTransform;
    ObjectTransform m_transformBefore;
    ObjectTransform m_transformPreview;
    QPointF m_transformPivotPage;
    QPointF m_transformStartLocal;
    qreal m_transformStartAngle = 0.0;
    QString m_transformObjectId;
    // The native editor is hosted in a graphics proxy so its Qt text document,
    // caret, selection and IME support share the object's page transform.
    QPlainTextEdit* m_textEditor = nullptr;
    QGraphicsView* m_editorView = nullptr;
    QGraphicsScene* m_editorScene = nullptr;
    QGraphicsProxyWidget* m_editorProxy = nullptr;
    QString m_editingObjectId;
    QString m_editingPageId;
    QRectF m_editingDocumentBounds;
    bool m_updatingTextEditor = false;
    QString m_maskTargetId;
    QString m_maskEffectId;
    bool m_maskEnabled = false;
    QString m_brushTargetId;
    QString m_brushEffectId;
    quint64 m_brushSpatialRevision = 0;
    quint64 m_transformSpatialRevision = 0;
    int m_textRangeStart = -1;
    int m_textRangeEnd = -1;
    QString m_navigationMode = QStringLiteral("middleSpace");
    bool m_invertZoom = false;
    QString m_pathEditObjectId;
    PathGeometry m_pathEditGeometry;
    PathGeometry m_pathEditBefore;
    ObjectFrame m_pathEditFrame;
    quint64 m_pathEditSpatialRevision = 0;
    int m_pathEditNodeIndex = -1;
    QString m_pathEditNodeId;
    int m_pathEditHandleKind = 0; // 1 anchor, 2 incoming, 3 outgoing
    QPointF m_pathEditStartLocal;
    bool m_pathEditing = false;
};

} // namespace vt
