#include "ui/editor_canvas.h"

#include <QApplication>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
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
    update();
}

void EditorCanvas::setScene(const SceneGeometry& scene, const QStringList& selectedObjectIds)
{
    m_sceneGeometry = scene;
    m_selectedObjectIds = selectedObjectIds;
    const SceneObjectGeometry* active = nullptr;
    if (!selectedObjectIds.isEmpty()) {
        active = m_sceneGeometry.objectById(selectedObjectIds.first());
    }
    if (!active && !m_sceneGeometry.objects.isEmpty()) {
        active = &m_sceneGeometry.objects.first();
    }
    m_geometry = active ? active->geometry : VectorGeometry();
    if (active && active->fill.isValid()) {
        m_fill = active->fill;
    }
    if (!m_hasInitialFit && width() > 0 && height() > 0) {
        fitContent();
    }
    update();
}

void EditorCanvas::setSelection(const QStringList& selectedObjectIds)
{
    m_selectedObjectIds = selectedObjectIds;
    update();
}

void EditorCanvas::setTool(EditorTool tool)
{
    if (m_tool == tool) {
        updateCursorShape();
        return;
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

qreal EditorCanvas::zoom() const
{
    return m_zoom;
}

void EditorCanvas::fitContent()
{
    QRectF bounds = m_sceneGeometry.bounds;
    if (bounds.isEmpty()) {
        bounds = m_geometry.bounds;
    }
    if (bounds.isEmpty()) {
        bounds = m_geometry.referenceBounds;
    }
    if (bounds.isEmpty() && !m_sceneGeometry.pageSize.isEmpty()) {
        bounds = QRectF(QPointF(0.0, 0.0), m_sceneGeometry.pageSize);
    }
    if (bounds.isEmpty() || width() <= 0 || height() <= 0) {
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
    update();
}

void EditorCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(35, 38, 44));

    const QRectF bounds = m_sceneGeometry.bounds.isEmpty()
        ? (m_geometry.bounds.isEmpty() ? m_geometry.referenceBounds : m_geometry.bounds)
        : m_sceneGeometry.bounds;
    if (!m_sceneGeometry.hasVisibleGeometry() && !m_geometry.hasVisibleGeometry()) {
        painter.setPen(QColor(180, 185, 195));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Type text to begin"));
    } else {
        painter.setTransform(viewTransform());
        for (const SceneObjectGeometry& object : m_sceneGeometry.objects) {
            if (!object.visible || !object.geometry.hasVisibleGeometry()) {
                continue;
            }
            painter.setPen(Qt::NoPen);
            painter.setBrush(object.fill.isValid() ? object.fill : m_fill);
            painter.drawPath(object.geometry.combinedPath());
        }
        if (m_sceneGeometry.objects.isEmpty() && m_geometry.hasVisibleGeometry()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_fill);
            painter.drawPath(m_geometry.combinedPath());
        }

        for (const QString& objectId : m_selectedObjectIds) {
            const SceneObjectGeometry* object = m_sceneGeometry.objectById(objectId);
            if (!object || !object->visible) {
                continue;
            }
            painter.setPen(QPen(QColor(85, 160, 255), 1.5 / qMax<qreal>(0.01, m_zoom), Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(object->visualBounds.adjusted(-4.0 / m_zoom,
                                                            -4.0 / m_zoom,
                                                            4.0 / m_zoom,
                                                            4.0 / m_zoom));
        }
        if (m_marqueeSelecting) {
            painter.setPen(QPen(QColor(85, 160, 255), 1.0 / qMax<qreal>(0.01, m_zoom), Qt::DashLine));
            painter.setBrush(QColor(85, 160, 255, 40));
            painter.drawRect(m_marqueeRect.normalized());
        }
    }

    painter.resetTransform();
    painter.setPen(QColor(175, 180, 190));
    painter.drawText(12,
                    height() - 12,
                    QStringLiteral("Zoom %1%   •   Wheel to zoom   •   Middle-drag to pan")
                        .arg(qRound(m_zoom * 100.0)));
    if (m_tool != EditorTool::Select && m_tool != EditorTool::Move && m_tool != EditorTool::Text
        && m_hasCursorPosition && !m_panning && !m_spacePressed) {
        const qreal screenRadius = qMax<qreal>(3.0, m_brushRadius * m_zoom);
        painter.setPen(QPen(QColor(85, 160, 255, 210), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(m_cursorPosition), screenRadius, screenRadius);
        painter.drawLine(QPointF(m_cursorPosition.x() - 3, m_cursorPosition.y()),
                         QPointF(m_cursorPosition.x() + 3, m_cursorPosition.y()));
        painter.drawLine(QPointF(m_cursorPosition.x(), m_cursorPosition.y() - 3),
                         QPointF(m_cursorPosition.x(), m_cursorPosition.y() + 3));
    }
}

void EditorCanvas::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }
    const qreal factor = std::pow(1.0015, static_cast<qreal>(delta));
    setZoom(qBound<qreal>(0.02, m_zoom * factor, 32.0));
    event->accept();
}

void EditorCanvas::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && m_spacePressed)) {
        m_panning = true;
        m_lastMousePosition = event->position().toPoint();
        updateCursorShape();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton
        && (m_tool == EditorTool::Select || m_tool == EditorTool::Move || m_tool == EditorTool::Text)) {
        const QPointF documentPoint = documentPosition(event->position());
        const QString hitObjectId = hitTestObject(documentPoint);
        const bool additive = event->modifiers().testFlag(Qt::ShiftModifier);
        if (m_tool == EditorTool::Text) {
            if (hitObjectId.isEmpty()) {
                emit textCreateRequested(documentPoint);
            } else {
                emit objectClicked(hitObjectId, additive);
                emit textEditRequested(hitObjectId);
            }
            event->accept();
            return;
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
            event->accept();
            return;
        }
        if (m_tool == EditorTool::Select && !hitObjectId.isEmpty()) {
            emit objectClicked(hitObjectId, additive);
            event->accept();
            return;
        }
        if (m_tool == EditorTool::Select && hitObjectId.isEmpty()) {
            m_marqueeSelecting = true;
            m_moveStartDocument = documentPoint;
            m_marqueeRect = QRectF(documentPoint, documentPoint);
            grabMouse();
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::LeftButton && m_tool != EditorTool::Select
        && m_tool != EditorTool::Move && m_tool != EditorTool::Text) {
        m_cursorPosition = event->position().toPoint();
        m_hasCursorPosition = true;
        m_brushing = true;
        m_brushPositions.clear();
        m_brushPositions.push_back(documentPosition(event->position()));
        grabMouse();
        updateBrushPreview();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void EditorCanvas::mouseMoveEvent(QMouseEvent* event)
{
    m_cursorPosition = event->position().toPoint();
    m_hasCursorPosition = true;
    if (m_panning) {
        const QPoint current = event->position().toPoint();
        m_panOffset += QPointF(current - m_lastMousePosition);
        m_lastMousePosition = current;
        update();
        event->accept();
        return;
    }
    if (m_marqueeSelecting) {
        m_marqueeRect = QRectF(m_moveStartDocument, documentPosition(event->position())).normalized();
        update();
        event->accept();
        return;
    }
    if (m_movingObjects) {
        const QPointF delta = documentPosition(event->position()) - m_moveStartDocument;
        m_sceneGeometry = m_sceneBeforeMove;
        for (const QString& objectId : m_moveObjectIds) {
            if (SceneObjectGeometry* object = m_sceneGeometry.objectById(objectId)) {
                QTransform transform;
                Q_UNUSED(transform.translate(delta.x(), delta.y()));
                object->geometry.transformAll(transform);
                object->visualBounds = object->geometry.bounds;
            }
        }
        m_sceneGeometry.recomputeBounds();
        update();
        event->accept();
        return;
    }
    if (m_brushing) {
        const QPointF position = documentPosition(event->position());
        if (m_brushPositions.isEmpty() || m_brushPositions.last() != position) {
            m_brushPositions.push_back(position);
            updateBrushPreview();
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
        emit marqueeSelectionRequested(m_marqueeRect.normalized(),
                                       QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier));
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
        emit moveCommitted(m_moveObjectIds, delta);
        m_moveObjectIds.clear();
        update();
        event->accept();
        return;
    }
    if (m_brushing && event->button() == Qt::LeftButton) {
        const DeformationStroke stroke = currentStroke();
        m_brushing = false;
        releaseMouse();
        m_brushPositions.clear();
        emit deformationPreviewCleared();
        if (!stroke.samples.isEmpty()) {
            emit deformationStrokeReady(stroke);
        }
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
        if (m_marqueeSelecting || m_movingObjects) {
            m_marqueeSelecting = false;
            m_movingObjects = false;
            m_sceneGeometry = m_sceneBeforeMove;
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

void EditorCanvas::setZoom(qreal value)
{
    const qreal bounded = qBound<qreal>(0.02, value, 32.0);
    if (qFuzzyCompare(m_zoom, bounded)) {
        return;
    }
    m_zoom = bounded;
    emit zoomChanged(m_zoom);
    update();
}

QTransform EditorCanvas::viewTransform() const
{
    const QRectF bounds = m_sceneGeometry.bounds.isEmpty()
        ? (m_geometry.bounds.isEmpty() ? m_geometry.referenceBounds : m_geometry.bounds)
        : m_sceneGeometry.bounds;
    const QPointF center = m_hasViewCenter ? m_viewCenter : bounds.center();
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
    stroke.radius = m_brushRadius;
    stroke.strength = m_brushStrength;
    stroke.hardness = m_brushHardness;
    stroke.samples = resampleBrushStroke(
        m_brushPositions, qMax<qreal>(0.5, m_brushRadius * 0.25), 1.0, 4096);
    return stroke;
}

void EditorCanvas::updateBrushPreview()
{
    const DeformationStroke stroke = currentStroke();
    if (!stroke.samples.isEmpty()) {
        emit deformationPreviewChanged(stroke);
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
    emit deformationPreviewCleared();
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

QString EditorCanvas::hitTestObject(const QPointF& documentPoint) const
{
    for (int index = m_sceneGeometry.objects.size() - 1; index >= 0; --index) {
        const SceneObjectGeometry& object = m_sceneGeometry.objects.at(index);
        if (!object.visible || object.locked) {
            continue;
        }
        if (object.geometry.combinedPath().contains(documentPoint)
            || object.visualBounds.contains(documentPoint)) {
            return object.objectId;
        }
    }
    return {};
}

QRectF EditorCanvas::selectionRectInDocument() const
{
    return m_marqueeRect.normalized();
}

} // namespace vt
