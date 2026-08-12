#include "ui/editor_canvas.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QTextCursor>
#include <QTextOption>
#include <QTimer>
#include <QWheelEvent>

#include <cmath>

namespace vt {

EditorCanvas::EditorCanvas(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(520, 420);
    setAutoFillBackground(false);
    setMouseTracking(true);
    updateCursorShape();
}

void EditorCanvas::setScene(const VectorGeometry& geometry, const QColor& fill)
{
    m_geometry = geometry;
    m_sceneGeometry = SceneGeometry();
    SceneObjectGeometry object;
    object.objectId = QStringLiteral("primary");
    object.geometry = geometry;
    object.fill = fill;
    m_sceneGeometry.objects.push_back(std::move(object));
    m_sceneGeometry.pageSize = QSizeF(1200.0, 800.0);
    m_sceneGeometry.bounds = geometry.bounds;
    if (fill.isValid()) {
        m_fill = fill;
    }
    if (!m_hasInitialFit && width() > 0 && height() > 0) {
        fitContent();
    }
    updateTextEditorGeometry();
    update();
}

void EditorCanvas::setScene(const SceneGeometry& scene,
                            const QStringList& selectedObjectIds,
                            const QString& activeObjectId)
{
    m_sceneGeometry = scene;
    m_selectedObjectIds = selectedObjectIds;
    m_activeObjectId = activeObjectId.isEmpty() ? selectedObjectIds.value(0) : activeObjectId;
    const SceneObjectGeometry* active = m_sceneGeometry.objectById(m_activeObjectId);
    m_geometry = active ? active->geometry : VectorGeometry();
    if (active && active->fill.isValid()) {
        m_fill = active->fill;
    }
    if (!m_hasInitialFit && width() > 0 && height() > 0) {
        fitContent();
    }
    if (!m_editingObjectId.isEmpty()) {
        const SceneObjectGeometry* editing = m_sceneGeometry.objectById(m_editingObjectId);
        if (m_editingPageId != m_sceneGeometry.pageId || m_activeObjectId != m_editingObjectId
            || !editing || !editing->visible || editing->locked) {
            finishTextEditing();
        }
    }
    updateTextEditorGeometry();
    update();
}

void EditorCanvas::setSelection(const QStringList& selectedObjectIds, const QString& activeObjectId)
{
    m_selectedObjectIds = selectedObjectIds;
    m_activeObjectId = activeObjectId.isEmpty() ? selectedObjectIds.value(0) : activeObjectId;
    if (isTextEditing() && m_activeObjectId != m_editingObjectId) {
        finishTextEditing();
    }
    update();
}

void EditorCanvas::setTextRange(int start, int end)
{
    m_textRangeStart = start;
    m_textRangeEnd = end;
    update();
}

void EditorCanvas::setTool(EditorTool tool)
{
    if (m_tool == tool) {
        updateCursorShape();
        return;
    }
    if (m_textEditor && m_textEditor->isVisible() && tool != EditorTool::Text) {
        finishTextEditing();
    }
    cancelBrushStroke();
    m_marqueeSelecting = false;
    m_movingObjects = false;
    m_tool = tool;
    if (m_tool == EditorTool::Smooth) {
        m_brushTarget = BrushTarget::Shape;
    }
    updateCursorShape();
    update();
}

void EditorCanvas::setBrushSettings(BrushMode mode,
                                    BrushTarget target,
                                    qreal radius,
                                    qreal strength,
                                    qreal hardness)
{
    m_brushMode = mode;
    m_brushTarget = mode == BrushMode::Smooth ? BrushTarget::Shape : target;
    m_brushRadius = qBound<qreal>(1.0, radius, 100000.0);
    m_brushStrength = qBound<qreal>(0.0, strength, 4.0);
    m_brushHardness = qBound<qreal>(0.0, hardness, 1.0);
    updateCursorShape();
    if (m_brushing) {
        updateBrushPreview();
    }
    update();
}

void EditorCanvas::setMaskRestoreMode(bool restore)
{
    m_maskRestore = restore;
}

void EditorCanvas::setMaskBrushSettings(qreal radius, qreal opacity, qreal hardness, bool restore)
{
    m_maskRadius = qBound<qreal>(1.0, radius, 100000.0);
    m_maskOpacity = qBound<qreal>(0.0, opacity, 1.0);
    m_maskHardness = qBound<qreal>(0.0, hardness, 1.0);
    m_maskRestore = restore;
    update();
}

void EditorCanvas::setMaskTarget(const QString& objectId)
{
    m_maskTargetId = objectId;
}

void EditorCanvas::setMaskEnabled(bool enabled)
{
    m_maskEnabled = enabled;
}

void EditorCanvas::setMaskEffectId(const QString& effectId)
{
    m_maskEffectId = effectId;
}

void EditorCanvas::setNavigationSettings(const QString& mode, bool invertZoom)
{
    m_navigationMode = mode;
    m_invertZoom = invertZoom;
}

void EditorCanvas::beginTextEditing(const QString& objectId,
                                    const QString& text,
                                    const QFont& font,
                                    const QRectF& documentBounds)
{
    if (objectId.isEmpty()) {
        return;
    }
    if (isTextEditing() && m_editingObjectId != objectId) {
        finishTextEditing();
    }
    if (!m_textEditor) {
        m_editorView = new QGraphicsView(this);
        m_editorView->setFocusPolicy(Qt::StrongFocus);
        m_editorView->setFrameShape(QFrame::NoFrame);
        m_editorView->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_editorView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_editorView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_editorView->setStyleSheet(QStringLiteral("QGraphicsView { background: transparent; border: 0px; }"));
        m_editorScene = new QGraphicsScene(m_editorView);
        m_editorView->setScene(m_editorScene);
        m_editorView->setVisible(false);
        // QAbstractScrollArea delivers pointer input to its viewport, not the
        // QGraphicsView object.  Filtering the viewport keeps the full-canvas
        // overlay from swallowing input outside the native editor.
        m_editorView->viewport()->installEventFilter(this);
        m_editorView->viewport()->setFocusPolicy(Qt::StrongFocus);

        m_textEditor = new QPlainTextEdit;
        m_textEditor->setFrameShape(QFrame::NoFrame);
        m_textEditor->setWordWrapMode(QTextOption::NoWrap);
        m_textEditor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textEditor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textEditor->setStyleSheet(QStringLiteral(
            "QPlainTextEdit { background: rgba(18, 21, 27, 190); color: #f4f7fb; "
            "border: 1px solid #79bfff; padding: 0px; selection-background-color: #2e76ba; }"));
        m_editorProxy = m_editorScene->addWidget(m_textEditor);
        m_editorProxy->setFlag(QGraphicsItem::ItemIsFocusable, true);
        m_editorProxy->setFocusPolicy(Qt::StrongFocus);
        m_editorScene->setStickyFocus(true);
        m_textEditor->installEventFilter(this);
        connect(m_textEditor, &QPlainTextEdit::textChanged, this, [this] {
            if (!m_updatingTextEditor && !m_editingObjectId.isEmpty()) {
                emit textEdited(m_editingObjectId, m_textEditor->toPlainText());
            }
        });
        connect(m_textEditor, &QPlainTextEdit::selectionChanged, this, [this] {
            if (m_editingObjectId.isEmpty()) {
                return;
            }
            const QTextCursor cursor = m_textEditor->textCursor();
            emit textRangeChanged(m_editingObjectId,
                                  qMin(cursor.anchor(), cursor.position()),
                                  qMax(cursor.anchor(), cursor.position()));
        });
    }

    m_editingObjectId = objectId;
    m_editingPageId = m_sceneGeometry.pageId;
    m_editingDocumentBounds = documentBounds;
    m_updatingTextEditor = true;
    m_textEditor->setFont(font);
    m_textEditor->setPlainText(text);
    QTextCursor cursor = m_textEditor->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_textEditor->setTextCursor(cursor);
    m_updatingTextEditor = false;
    updateTextEditorGeometry();
    m_editorProxy->show();
    m_editorView->show();
    m_editorView->raise();
    // A button or canvas mouse handler may still own focus while this method
    // returns.  Focus the graphics view, proxy and native widget as one
    // session, then repeat after the originating event has unwound.
    m_editorView->setFocus(Qt::OtherFocusReason);
    m_editorView->viewport()->setFocus(Qt::OtherFocusReason);
    m_editorScene->setFocusItem(m_editorProxy, Qt::OtherFocusReason);
    m_editorProxy->setFocus(Qt::OtherFocusReason);
    m_textEditor->setFocus(Qt::OtherFocusReason);
    const QString editingId = m_editingObjectId;
    QTimer::singleShot(0, this, [this, editingId] {
        if (!isTextEditing() || m_editingObjectId != editingId) return;
        m_editorView->setFocus(Qt::OtherFocusReason);
        m_editorView->viewport()->setFocus(Qt::OtherFocusReason);
        m_editorScene->setFocusItem(m_editorProxy, Qt::OtherFocusReason);
        m_editorProxy->setFocus(Qt::OtherFocusReason);
        m_textEditor->setFocus(Qt::OtherFocusReason);
    });
    emit textEditingChanged(true);
}

void EditorCanvas::finishTextEditing()
{
    if (!m_textEditor || m_editingObjectId.isEmpty()) {
        return;
    }
    m_editorProxy->hide();
    m_editorView->hide();
    m_editingObjectId.clear();
    m_editingPageId.clear();
    m_textRangeStart = -1;
    m_textRangeEnd = -1;
    setFocus(Qt::OtherFocusReason);
    emit textEditingChanged(false);
    update();
}

qreal EditorCanvas::zoom() const
{
    return m_zoom;
}

QPointF EditorCanvas::mapDocumentToViewport(const QPointF& documentPoint) const
{
    return viewTransform().map(documentPoint);
}

void EditorCanvas::fitContent()
{
    QRectF bounds(QPointF(0.0, 0.0), m_sceneGeometry.pageSize);
    if (bounds.isEmpty()) {
        bounds = m_sceneGeometry.bounds;
    }
    if (bounds.isEmpty()) {
        bounds = m_geometry.bounds;
    }
    if (bounds.isEmpty()) {
        return;
    }

    const qreal availableWidth = qMax<qreal>(1.0, width() - 72.0);
    const qreal availableHeight = qMax<qreal>(1.0, height() - 72.0);
    const qreal widthScale = availableWidth / qMax<qreal>(1.0, bounds.width());
    const qreal heightScale = availableHeight / qMax<qreal>(1.0, bounds.height());
    m_panOffset = QPointF(0.0, 0.0);
    m_viewCenter = bounds.center();
    m_hasViewCenter = true;
    setZoom(qBound<qreal>(0.02, qMin(widthScale, heightScale), 32.0));
    m_hasInitialFit = true;
    updateTextEditorGeometry();
    update();
}

void EditorCanvas::zoomIn()
{
    setZoom(m_zoom * 1.2);
}

void EditorCanvas::zoomOut()
{
    setZoom(m_zoom / 1.2);
}

void EditorCanvas::zoom100()
{
    setZoom(1.0);
}

void EditorCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(30, 33, 39));

    const QRectF pageRect(QPointF(0.0, 0.0), m_sceneGeometry.pageSize.isEmpty()
                                                       ? QSizeF(1200.0, 800.0)
                                                       : m_sceneGeometry.pageSize);
    painter.setTransform(viewTransform());
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(10, 12, 15, 90));
    painter.drawRect(pageRect.translated(8.0 / qMax<qreal>(0.01, m_zoom),
                                         8.0 / qMax<qreal>(0.01, m_zoom)));
    painter.setBrush(m_sceneGeometry.pageBackground.isValid()
                         ? m_sceneGeometry.pageBackground
                         : QColor(242, 242, 246));
    painter.drawRect(pageRect);
    painter.setPen(QPen(QColor(125, 132, 144), 1.0 / qMax<qreal>(0.01, m_zoom)));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(pageRect);

    for (const SceneObjectGeometry& object : m_sceneGeometry.objects) {
        if (!object.visible) {
            continue;
        }
        if (object.geometry.hasVisibleGeometry()) {
            painter.setPen(Qt::NoPen);
            const QColor baseFill = object.fill.isValid() ? object.fill : m_fill;
            for (const GeometryPiece& piece : object.geometry.pieces) {
                QColor fill = baseFill;
                fill.setAlphaF(baseFill.alphaF() * qBound<qreal>(0.0, piece.opacityMultiplier, 1.0));
                painter.setBrush(fill);
                painter.drawPath(piece.path);
            }
        } else {
            painter.setPen(QPen(QColor(125, 145, 165, 170), 1.0 / qMax<qreal>(0.01, m_zoom), Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(object.frame.orientedPageQuad());
        }

        if (object.objectId == m_activeObjectId && m_textRangeStart >= 0 && m_textRangeEnd > m_textRangeStart) {
            painter.setBrush(QColor(65, 150, 255, 75));
            for (const GeometryPiece& piece : object.geometry.pieces) {
                const int pieceEnd = piece.sourceClusterStart + qMax(1, piece.sourceClusterLength);
                if (piece.sourceClusterStart < m_textRangeEnd && pieceEnd > m_textRangeStart) {
                    painter.drawPath(piece.path);
                }
            }
        }
    }

    const QRectF pageBounds = pageRect;
    if (m_sceneGeometry.objects.isEmpty()) {
        painter.setPen(QColor(100, 108, 120));
        painter.drawText(pageBounds, Qt::AlignCenter,
                         QStringLiteral("Press T or choose the Text tool to create text"));
    }

    for (const QString& objectId : m_selectedObjectIds) {
        const SceneObjectGeometry* object = m_sceneGeometry.objectById(objectId);
        if (!object || !object->visible) {
            continue;
        }
        const bool active = objectId == m_activeObjectId;
        painter.setPen(QPen(active ? QColor(90, 176, 255) : QColor(150, 190, 235),
                            (active ? 1.7 : 1.2) / qMax<qreal>(0.01, m_zoom),
                            Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        const QPolygonF quad = object->frame.orientedPageQuad();
        painter.drawPolygon(quad);
        if (active) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(90, 176, 255));
            const qreal handle = 7.0 / qMax<qreal>(0.01, m_zoom);
            const QVector<QPointF> handles = {
                quad.value(0), quad.value(1), quad.value(3), quad.value(2)};
            for (const QPointF& handleCenter : handles) {
                painter.drawRect(QRectF(handleCenter - QPointF(handle / 2.0, handle / 2.0),
                                        QSizeF(handle, handle)));
            }
            const QPointF rotationCenter = rotationHandleCenter(*object);
            const QPointF topCenter = (quad.value(0) + quad.value(1)) * 0.5;
            painter.setPen(QPen(QColor(90, 176, 255), 1.3 / qMax<qreal>(0.01, m_zoom)));
            painter.drawLine(topCenter, rotationCenter);
            painter.setBrush(QColor(32, 52, 72));
            painter.drawEllipse(rotationCenter, handle * 0.62, handle * 0.62);
        }
    }
    if (m_marqueeSelecting) {
        painter.setPen(QPen(QColor(85, 160, 255), 1.0 / qMax<qreal>(0.01, m_zoom), Qt::DashLine));
        painter.setBrush(QColor(85, 160, 255, 40));
        painter.drawRect(m_marqueeRect.normalized());
    }

    painter.resetTransform();
    painter.setPen(QColor(175, 180, 190));
    painter.drawText(12,
                     height() - 12,
                     QStringLiteral("Zoom %1%   •   Wheel to zoom   •   Middle-drag to pan")
                         .arg(qRound(m_zoom * 100.0)));
    if (m_tool != EditorTool::Select && m_tool != EditorTool::Move && m_tool != EditorTool::Text
        && m_hasCursorPosition && !m_panning && !m_spacePressed) {
        painter.setPen(QPen(m_tool == EditorTool::EffectMask
                                ? QColor(255, 174, 104, 220)
                                : QColor(85, 160, 255, 210),
                            1.0));
        painter.setBrush(Qt::NoBrush);
        const QString cursorTarget = m_brushing ? m_brushTargetId
            : (m_tool == EditorTool::EffectMask ? m_maskTargetId : m_activeObjectId);
        const SceneObjectGeometry* target = m_sceneGeometry.objectById(cursorTarget);
        if (target) {
            const QPointF localCenter = target->frame.pagePointToLocal(documentPosition(m_cursorPosition));
            QPainterPath localCircle;
            const qreal pageRadius = m_tool == EditorTool::EffectMask ? m_maskRadius : m_brushRadius;
            const qreal localRadius = target->frame.pageRadiusToLocalEquivalentArea(pageRadius);
            localCircle.addEllipse(localCenter, localRadius, localRadius);
            painter.drawPath(viewTransform().map(target->frame.localToPage.map(localCircle)));
        } else {
            const qreal screenRadius = qMax<qreal>(3.0,
                                                    (m_tool == EditorTool::EffectMask ? m_maskRadius : m_brushRadius)
                                                        * m_zoom);
            painter.drawEllipse(QPointF(m_cursorPosition), screenRadius, screenRadius);
        }
        painter.drawLine(QPointF(m_cursorPosition.x() - 3, m_cursorPosition.y()),
                         QPointF(m_cursorPosition.x() + 3, m_cursorPosition.y()));
        painter.drawLine(QPointF(m_cursorPosition.x(), m_cursorPosition.y() - 3),
                         QPointF(m_cursorPosition.x(), m_cursorPosition.y() + 3));
    }
}

void EditorCanvas::wheelEvent(QWheelEvent* event)
{
    if (!handleCanvasWheel(event->angleDelta(), event->modifiers())) {
        event->ignore();
        return;
    }
    event->accept();
}

void EditorCanvas::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    if (handleCanvasMousePress(event->button(), event->position(), event->modifiers())) {
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void EditorCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    // Deformation and effect-mask tools own their gestures.  Select/Move may
    // promote an editable object into the native QPlainTextEdit session.
    if (event->button() == Qt::LeftButton
        && (m_tool == EditorTool::Select || m_tool == EditorTool::Move)) {
        const QString objectId = hitTestObject(documentPosition(event->position()));
        if (!objectId.isEmpty()) {
            emit objectClicked(objectId, false);
            emit textEditRequested(objectId);
            event->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

bool EditorCanvas::handleCanvasMousePress(Qt::MouseButton button,
                                          const QPointF& widgetPosition,
                                          Qt::KeyboardModifiers modifiers)
{
    if (button == Qt::MiddleButton
        || (button == Qt::LeftButton
            && m_spacePressed && m_navigationMode == QStringLiteral("middleSpace"))) {
        m_panning = true;
        m_lastMousePosition = widgetPosition.toPoint();
        updateCursorShape();
        return true;
    }

    if (button == Qt::LeftButton
        && (m_tool == EditorTool::Select || m_tool == EditorTool::Move || m_tool == EditorTool::Text)) {
        const QPointF documentPoint = documentPosition(widgetPosition);
        if ((m_tool == EditorTool::Select || m_tool == EditorTool::Move)
            && beginTransform(documentPoint)) {
            grabMouse();
            return true;
        }
        const QString hitObjectId = hitTestObject(documentPoint);
        const bool additive = modifiers.testFlag(Qt::ShiftModifier);
        if (m_tool == EditorTool::Text) {
            if (hitObjectId.isEmpty()) {
                emit textCreateRequested(documentPoint);
            } else {
                emit objectClicked(hitObjectId, additive);
                emit textEditRequested(hitObjectId);
            }
            return true;
        }
        if (m_tool == EditorTool::Move && !hitObjectId.isEmpty()) {
            if (!m_selectedObjectIds.contains(hitObjectId)) {
                emit objectClicked(hitObjectId, additive);
            }
            m_movingObjects = true;
            m_moveStartDocument = documentPoint;
            m_sceneBeforeMove = m_sceneGeometry;
            m_moveObjectIds = m_selectedObjectIds;
            if (!m_moveObjectIds.contains(hitObjectId)) {
                m_moveObjectIds = {hitObjectId};
            }
            grabMouse();
            return true;
        }
        if (m_tool == EditorTool::Select && !hitObjectId.isEmpty()) {
            emit objectClicked(hitObjectId, additive);
            return true;
        }
        if (m_tool == EditorTool::Select && hitObjectId.isEmpty()) {
            m_marqueeSelecting = true;
            m_marqueeMoved = false;
            m_moveStartDocument = documentPoint;
            m_marqueeStartWidget = widgetPosition;
            m_marqueeRect = QRectF(documentPoint, documentPoint);
            grabMouse();
            return true;
        }
    }

    if (button == Qt::LeftButton && m_tool == EditorTool::EffectMask) {
        const QPointF documentPoint = documentPosition(widgetPosition);
        const QString hit = hitTestObject(documentPoint);
        if (!m_maskEnabled || m_maskTargetId.isEmpty()
            || (!hit.isEmpty() && hit != m_maskTargetId)) {
            return false;
        }
        m_cursorPosition = widgetPosition.toPoint();
        m_hasCursorPosition = true;
        m_brushing = true;
        m_brushTargetId = m_maskTargetId;
        m_brushEffectId = m_maskEffectId;
        m_brushPositions.clear();
        m_brushPositions.push_back(documentPoint);
        grabMouse();
        emit effectMaskPreviewChanged(m_brushTargetId, m_brushEffectId, currentMaskStroke());
        update();
        return true;
    }

    if (button == Qt::LeftButton && m_tool != EditorTool::Select
        && m_tool != EditorTool::Move && m_tool != EditorTool::Text
        && m_tool != EditorTool::EffectMask) {
        m_cursorPosition = widgetPosition.toPoint();
        m_hasCursorPosition = true;
        m_brushing = true;
        m_brushTargetId = hitTestObject(documentPosition(widgetPosition));
        if (m_brushTargetId.isEmpty()) {
            m_brushTargetId = m_activeObjectId;
        }
        if (m_brushTargetId.isEmpty()) {
            m_brushing = false;
            return false;
        }
        m_brushPositions.clear();
        m_brushPositions.push_back(documentPosition(widgetPosition));
        grabMouse();
        updateBrushPreview();
        return true;
    }
    return false;
}

void EditorCanvas::mouseMoveEvent(QMouseEvent* event)
{
    m_cursorPosition = event->position().toPoint();
    m_hasCursorPosition = true;
    if (m_panning) {
        const QPoint current = event->position().toPoint();
        m_panOffset += QPointF(current - m_lastMousePosition);
        m_lastMousePosition = current;
        updateTextEditorGeometry();
        update();
        event->accept();
        return;
    }
    if (m_marqueeSelecting) {
        m_marqueeMoved = (event->position() - m_marqueeStartWidget).manhattanLength() >= 4;
        m_marqueeRect = QRectF(m_moveStartDocument, documentPosition(event->position())).normalized();
        update();
        event->accept();
        return;
    }
    if (m_movingObjects) {
        const QPointF delta = documentPosition(event->position()) - m_moveStartDocument;
        m_sceneGeometry = m_sceneBeforeMove;
        for (const QString& objectId : m_moveObjectIds) {
            const SceneObjectGeometry* source = m_sceneBeforeMove.objectById(objectId);
            if (SceneObjectGeometry* object = m_sceneGeometry.objectById(objectId); source) {
                ObjectTransform transform = source->transform;
                transform.position += delta;
                applyPreviewTransform(object, *source, transform);
            }
        }
        m_sceneGeometry.recomputeBounds();
        updateTextEditorGeometry();
        update();
        event->accept();
        return;
    }
    if (m_transforming) {
        updateTransformPreview(documentPosition(event->position()));
        event->accept();
        return;
    }
    if (m_brushing) {
        const QPointF position = documentPosition(event->position());
        if (m_brushPositions.isEmpty() || m_brushPositions.last() != position) {
            m_brushPositions.push_back(position);
            if (m_tool == EditorTool::EffectMask) {
                emit effectMaskPreviewChanged(m_brushTargetId, m_brushEffectId, currentMaskStroke());
            } else {
                updateBrushPreview();
            }
        }
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void EditorCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        updateCursorShape();
        event->accept();
        return;
    }
    if (m_marqueeSelecting && event->button() == Qt::LeftButton) {
        m_marqueeSelecting = false;
        releaseMouse();
        if (m_marqueeMoved) {
            emit marqueeSelectionRequested(m_marqueeRect.normalized(),
                                           QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier));
        } else {
            emit objectClicked({}, false);
        }
        m_marqueeRect = QRectF();
        update();
        event->accept();
        return;
    }
    if (m_movingObjects && event->button() == Qt::LeftButton) {
        const QPointF delta = documentPosition(event->position()) - m_moveStartDocument;
        m_movingObjects = false;
        releaseMouse();
        m_sceneGeometry = m_sceneBeforeMove;
        if (!delta.isNull()) {
            emit moveCommitted(m_moveObjectIds, delta);
        }
        m_moveObjectIds.clear();
        update();
        event->accept();
        return;
    }
    if (m_transforming && event->button() == Qt::LeftButton) {
        m_transforming = false;
        releaseMouse();
        m_sceneGeometry = m_sceneBeforeTransform;
        if (m_transformPreview.position != m_transformBefore.position
            || !qFuzzyCompare(m_transformPreview.rotation, m_transformBefore.rotation)
            || m_transformPreview.scale != m_transformBefore.scale) {
            emit objectTransformCommitted(m_transformObjectId, m_transformPreview);
        }
        m_transformObjectId.clear();
        update();
        event->accept();
        return;
    }
    if (m_brushing && event->button() == Qt::LeftButton) {
        const QString targetId = m_brushTargetId;
        const DeformationStroke stroke = currentStroke();
        const EffectMaskStroke maskStroke = currentMaskStroke();
        m_brushing = false;
        releaseMouse();
        m_brushPositions.clear();
        emit deformationPreviewCleared();
        if (m_tool == EditorTool::EffectMask) {
            emit effectMaskPreviewCleared();
            if (!maskStroke.points.isEmpty()) {
            emit effectMaskStrokeReady(targetId, m_brushEffectId, maskStroke);
            }
        } else if (!stroke.samples.isEmpty()) {
            emit deformationStrokeReady(targetId, stroke);
        }
        m_brushTargetId.clear();
        m_brushEffectId.clear();
        updateCursorShape();
        update();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void EditorCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && !event->isAutoRepeat()) {
        cancelBrushStroke();
        if (m_marqueeSelecting || m_movingObjects || m_transforming) {
            const bool cancelTransform = m_transforming;
            m_marqueeSelecting = false;
            m_movingObjects = false;
            m_transforming = false;
            m_sceneGeometry = cancelTransform ? m_sceneBeforeTransform : m_sceneBeforeMove;
            releaseMouse();
            update();
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        cancelBrushStroke();
        m_spacePressed = true;
        updateCursorShape();
        event->accept();
        return;
    }
    if ((m_tool == EditorTool::Select || m_tool == EditorTool::Move)
        && !m_selectedObjectIds.isEmpty()) {
        const qreal step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10.0 : 1.0;
        QPointF delta;
        switch (event->key()) {
        case Qt::Key_Left: delta.setX(-step); break;
        case Qt::Key_Right: delta.setX(step); break;
        case Qt::Key_Up: delta.setY(-step); break;
        case Qt::Key_Down: delta.setY(step); break;
        case Qt::Key_Delete: emit deleteRequested(); event->accept(); return;
        case Qt::Key_D:
            if (event->modifiers().testFlag(Qt::ControlModifier)) {
                emit duplicateRequested();
                event->accept();
                return;
            }
            break;
        default: break;
        }
        if (!delta.isNull()) {
            emit nudgeRequested(delta);
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void EditorCanvas::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = false;
        updateCursorShape();
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void EditorCanvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!m_hasInitialFit) {
        fitContent();
    }
    updateTextEditorGeometry();
}

void EditorCanvas::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    updateCursorShape();
}

void EditorCanvas::leaveEvent(QEvent* event)
{
    m_hasCursorPosition = false;
    update();
    QWidget::leaveEvent(event);
}

bool EditorCanvas::eventFilter(QObject* watched, QEvent* event)
{
    if (m_editorView && watched == m_editorView->viewport() && isTextEditing()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (isOutsideNativeEditor(mouseEvent->position())) {
                const QPointF canvasPosition = m_editorView->viewport()->mapTo(this,
                                                                                mouseEvent->position().toPoint());
                finishTextEditing();
                setFocus(Qt::MouseFocusReason);
                static_cast<void>(handleCanvasMousePress(mouseEvent->button(),
                                                         canvasPosition,
                                                         mouseEvent->modifiers()));
                return true;
            }
        } else if (event->type() == QEvent::Wheel) {
            auto* wheel = static_cast<QWheelEvent*>(event);
            if (isOutsideNativeEditor(wheel->position())) {
                static_cast<void>(handleCanvasWheel(wheel->angleDelta(), wheel->modifiers()));
                return true;
            }
        }
    }
    if (watched == m_textEditor && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape && !keyEvent->isAutoRepeat()) {
            finishTextEditing();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Z
            && keyEvent->modifiers().testFlag(Qt::ControlModifier)
            && keyEvent->modifiers().testFlag(Qt::ShiftModifier)
            && !keyEvent->isAutoRepeat()) {
            m_textEditor->redo();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool EditorCanvas::handleCanvasWheel(const QPoint& angleDelta, Qt::KeyboardModifiers modifiers)
{
    if (angleDelta.y() == 0
        || (modifiers.testFlag(Qt::ControlModifier)
            && m_navigationMode == QStringLiteral("middleSpace"))) {
        return false;
    }
    qreal delta = static_cast<qreal>(angleDelta.y());
    if (m_invertZoom) {
        delta = -delta;
    }
    setZoom(qBound<qreal>(0.02, m_zoom * std::pow(1.0015, delta), 32.0));
    return true;
}

bool EditorCanvas::isOutsideNativeEditor(const QPointF& viewportPosition) const
{
    if (!m_editorView || !m_editorProxy) {
        return true;
    }
    const QPointF proxyPosition = m_editorProxy->mapFromScene(
        m_editorView->mapToScene(viewportPosition.toPoint()));
    return !m_editorProxy->boundingRect().contains(proxyPosition);
}

void EditorCanvas::setZoom(qreal value)
{
    const qreal bounded = qBound<qreal>(0.02, value, 32.0);
    if (qFuzzyCompare(m_zoom, bounded)) {
        return;
    }
    m_zoom = bounded;
    emit zoomChanged(m_zoom);
    updateTextEditorGeometry();
    update();
}

QTransform EditorCanvas::viewTransform() const
{
    const QRectF pageBounds(QPointF(0.0, 0.0), m_sceneGeometry.pageSize.isEmpty()
                                                   ? QSizeF(1200.0, 800.0)
                                                   : m_sceneGeometry.pageSize);
    const QPointF center = m_hasViewCenter ? m_viewCenter : pageBounds.center();
    QTransform transform;
    Q_UNUSED(transform.translate(width() * 0.5 + m_panOffset.x(), height() * 0.5 + m_panOffset.y()));
    Q_UNUSED(transform.scale(m_zoom, m_zoom));
    Q_UNUSED(transform.translate(-center.x(), -center.y()));
    return transform;
}

QPointF EditorCanvas::documentPosition(const QPointF& widgetPosition) const
{
    bool invertible = false;
    const QTransform inverse = viewTransform().inverted(&invertible);
    return invertible ? inverse.map(widgetPosition) : QPointF();
}

DeformationStroke EditorCanvas::currentStroke() const
{
    DeformationStroke stroke;
    stroke.mode = m_brushMode;
    stroke.target = m_brushTarget;
    const SceneObjectGeometry* object = m_sceneGeometry.objectById(m_brushTargetId);
    stroke.radius = object ? object->frame.pageRadiusToLocalEquivalentArea(m_brushRadius) : m_brushRadius;
    stroke.strength = m_brushStrength;
    stroke.hardness = m_brushHardness;
    QVector<QPointF> localPositions;
    localPositions.reserve(m_brushPositions.size());
    if (object) {
        for (const QPointF& position : m_brushPositions) {
            localPositions.push_back(object->frame.pagePointToLocal(position));
        }
    } else {
        localPositions = m_brushPositions;
    }
    stroke.samples = resampleBrushStroke(localPositions,
                                         qMax<qreal>(0.5, stroke.radius * 0.25), 1.0, 4096);
    return stroke;
}

EffectMaskStroke EditorCanvas::currentMaskStroke() const
{
    EffectMaskStroke stroke;
    stroke.points = m_brushPositions;
    stroke.radius = m_maskRadius;
    stroke.opacity = m_maskOpacity;
    stroke.hardness = m_maskHardness;
    stroke.restore = m_maskRestore;
    return stroke;
}

void EditorCanvas::updateBrushPreview()
{
    const DeformationStroke stroke = currentStroke();
    if (!stroke.samples.isEmpty()) {
        emit deformationPreviewChanged(m_brushTargetId, stroke);
    }
}

void EditorCanvas::cancelBrushStroke()
{
    if (!m_brushing) {
        return;
    }
    m_brushing = false;
    releaseMouse();
    m_brushPositions.clear();
    m_brushTargetId.clear();
    m_brushEffectId.clear();
    emit deformationPreviewCleared();
    if (m_tool == EditorTool::EffectMask) {
        emit effectMaskPreviewCleared();
    }
    updateCursorShape();
    update();
}

void EditorCanvas::updateCursorShape()
{
    if (m_panning) {
        setCursor(Qt::ClosedHandCursor);
    } else if (m_spacePressed) {
        setCursor(Qt::OpenHandCursor);
    } else if (m_tool == EditorTool::Select) {
        setCursor(Qt::ArrowCursor);
    } else if (m_tool == EditorTool::Move) {
        setCursor(Qt::SizeAllCursor);
    } else if (m_tool == EditorTool::Text) {
        setCursor(Qt::IBeamCursor);
    } else {
        setCursor(Qt::CrossCursor);
    }
}

void EditorCanvas::updateTextEditorGeometry()
{
    if (!m_textEditor || !m_editorView || !m_editorProxy || m_editingObjectId.isEmpty()) {
        return;
    }
    m_editorView->setGeometry(rect());
    m_editorScene->setSceneRect(QRectF(rect()));
    if (const SceneObjectGeometry* object = m_sceneGeometry.objectById(m_editingObjectId)) {
        // Native editing follows the source frame, never the effected geometry.
        QRectF localBounds = object->frame.baseLocalBounds;
        if (localBounds.isEmpty()) {
            localBounds = object->frame.baseLocalBounds;
        }
        if (localBounds.isEmpty()) {
            localBounds = QRectF(0.0, 0.0, 120.0, 36.0);
        }
        m_textEditor->resize(qMax(1, qCeil(localBounds.width())),
                             qMax(1, qCeil(localBounds.height())));
        QTransform localOffset;
        localOffset.translate(localBounds.x(), localBounds.y());
        m_editorProxy->setTransform(viewTransform() * object->frame.localToPage * localOffset);
        m_editorProxy->setPos(QPointF());
        return;
    }
    QRectF documentBounds = m_editingDocumentBounds;
    if (documentBounds.isEmpty()) {
        documentBounds = QRectF(QPointF(0.0, 0.0), QSizeF(360.0, 120.0));
    }
    m_textEditor->resize(qMax(1, qCeil(documentBounds.width())),
                         qMax(1, qCeil(documentBounds.height())));
    // Proxy positions are scene-space while documentBounds is page-space.
    // Keep the same one-matrix contract as the evaluated ObjectFrame path:
    // widget-local -> page -> view/scene, with a zero proxy position.
    QTransform documentOffset;
    documentOffset.translate(documentBounds.x(), documentBounds.y());
    m_editorProxy->setTransform(viewTransform() * documentOffset);
    m_editorProxy->setPos(QPointF());
}

QString EditorCanvas::hitTestObject(const QPointF& documentPoint) const
{
    for (int index = m_sceneGeometry.objects.size() - 1; index >= 0; --index) {
        const SceneObjectGeometry& object = m_sceneGeometry.objects.at(index);
        if (!object.visible || object.locked) {
            continue;
        }
        if (object.geometry.combinedPath().contains(documentPoint)
            || (!object.geometry.hasVisibleGeometry()
                && object.frame.orientedPageQuad().containsPoint(documentPoint, Qt::OddEvenFill))) {
            return object.objectId;
        }
    }
    return {};
}

QPointF EditorCanvas::rotationHandleCenter(const SceneObjectGeometry& object) const
{
    const QPolygonF quad = object.frame.orientedPageQuad();
    const QPointF topCenter = (quad.value(0) + quad.value(1)) * 0.5;
    QPointF direction = topCenter - object.frame.localPointToPage(object.frame.pivotLocal);
    const qreal length = QLineF(QPointF(), direction).length();
    if (length <= 1.0e-6) {
        direction = QPointF(0.0, -1.0);
    } else {
        direction /= length;
    }
    return topCenter + direction * (26.0 / qMax<qreal>(0.01, m_zoom));
}

int EditorCanvas::scaleHandleAt(const QPointF& documentPoint) const
{
    const SceneObjectGeometry* object = m_sceneGeometry.objectById(m_activeObjectId);
    if (!object || object->locked || !m_selectedObjectIds.contains(m_activeObjectId)) {
        return -1;
    }
    const qreal tolerance = 9.0 / qMax<qreal>(0.01, m_zoom);
    const QPolygonF quad = object->frame.orientedPageQuad();
    for (int index = 0; index < quad.size(); ++index) {
        if (QLineF(documentPoint, quad.at(index)).length() <= tolerance) {
            return index;
        }
    }
    return -1;
}

bool EditorCanvas::rotationHandleContains(const QPointF& documentPoint) const
{
    const SceneObjectGeometry* object = m_sceneGeometry.objectById(m_activeObjectId);
    return object && !object->locked && m_selectedObjectIds.contains(m_activeObjectId)
        && QLineF(documentPoint, rotationHandleCenter(*object)).length()
            <= 10.0 / qMax<qreal>(0.01, m_zoom);
}

bool EditorCanvas::beginTransform(const QPointF& documentPoint)
{
    const bool rotation = rotationHandleContains(documentPoint);
    const int handle = rotation ? -1 : scaleHandleAt(documentPoint);
    if (!rotation && handle < 0) {
        return false;
    }
    const SceneObjectGeometry* object = m_sceneGeometry.objectById(m_activeObjectId);
    if (!object) {
        return false;
    }
    m_transforming = true;
    m_rotatingObject = rotation;
    m_scaleHandle = handle;
    m_transformObjectId = object->objectId;
    m_sceneBeforeTransform = m_sceneGeometry;
    m_transformBefore = object->transform;
    m_transformPreview = m_transformBefore;
    m_transformPivotPage = object->frame.localPointToPage(object->frame.pivotLocal);
    m_transformStartLocal = object->frame.pagePointToLocal(documentPoint);
    const QPointF vector = documentPoint - m_transformPivotPage;
    m_transformStartAngle = std::atan2(vector.y(), vector.x());
    return true;
}

void EditorCanvas::updateTransformPreview(const QPointF& documentPoint)
{
    const SceneObjectGeometry* before = m_sceneBeforeTransform.objectById(m_transformObjectId);
    if (!before) {
        return;
    }
    ObjectTransform transform = m_transformBefore;
    if (m_rotatingObject) {
        const QPointF vector = documentPoint - m_transformPivotPage;
        const qreal angle = std::atan2(vector.y(), vector.x());
        constexpr qreal Pi = 3.14159265358979323846;
        transform.rotation += (angle - m_transformStartAngle) * 180.0 / Pi;
    } else {
        const QPointF currentLocal = before->frame.pagePointToLocal(documentPoint);
        const QPointF pivot = before->frame.pivotLocal;
        const QPointF initial = m_transformStartLocal - pivot;
        const QPointF current = currentLocal - pivot;
        if (std::abs(initial.x()) > 1.0e-5) {
            transform.scale.setX(m_transformBefore.scale.x() * current.x() / initial.x());
        }
        if (std::abs(initial.y()) > 1.0e-5) {
            transform.scale.setY(m_transformBefore.scale.y() * current.y() / initial.y());
        }
        transform.normalizeScale();
    }
    m_transformPreview = transform;
    m_sceneGeometry = m_sceneBeforeTransform;
    if (SceneObjectGeometry* preview = m_sceneGeometry.objectById(m_transformObjectId)) {
        applyPreviewTransform(preview, *before, transform);
    }
    m_sceneGeometry.recomputeBounds();
    update();
}

void EditorCanvas::applyPreviewTransform(SceneObjectGeometry* preview,
                                         const SceneObjectGeometry& source,
                                         const ObjectTransform& transform)
{
    if (!preview) {
        return;
    }
    const ObjectFrame frame = ObjectFrame::fromTransform(transform,
                                                          source.frame.baseLocalBounds,
                                                          source.frame.currentLocalBounds);
    // Scene geometry is in page coordinates.  Always reconstruct the local
    // geometry from the unchanged drag-start frame before applying the one
    // authoritative preview transform.  Incremental page-space deltas were
    // the source of rotation/move drift when an object already had rotation.
    preview->geometry = source.geometry;
    preview->geometry.transformAll(source.frame.pageToLocal);
    preview->geometry.transformAll(frame.localToPage);
    preview->frame = frame;
    preview->visualBounds = frame.pageAabb();
    preview->transform = transform;
}

} // namespace vt
