#include "ui/editor_canvas.h"

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
    if (fill.isValid()) {
        m_fill = fill;
    }
    if (!m_hasInitialFit && width() > 0 && height() > 0) {
        fitContent();
    }
    update();
}

void EditorCanvas::setTool(EditorTool tool)
{
    if (m_tool == tool) {
        updateCursorShape();
        return;
    }
    cancelBrushStroke();
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
    QRectF bounds = m_geometry.bounds;
    if (bounds.isEmpty()) {
        bounds = m_geometry.referenceBounds;
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
    painter.fillRect(rect(), QColor(242, 242, 246));

    const QRectF bounds = m_geometry.bounds.isEmpty() ? m_geometry.referenceBounds : m_geometry.bounds;
    if (!m_geometry.hasVisibleGeometry() || bounds.isEmpty()) {
        painter.setPen(QColor(110, 110, 120));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Type text to begin"));
    } else {
        painter.setTransform(viewTransform());
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_fill);
        painter.drawPath(m_geometry.combinedPath());
    }

    painter.resetTransform();
    painter.setPen(QColor(150, 150, 160));
    painter.drawText(12,
                    height() - 12,
                    QStringLiteral("Zoom %1%   •   Wheel to zoom   •   Middle-drag to pan")
                        .arg(qRound(m_zoom * 100.0)));
    if (m_tool != EditorTool::Select && m_hasCursorPosition && !m_panning && !m_spacePressed) {
        const qreal screenRadius = qMax<qreal>(3.0, m_brushRadius * m_zoom);
        painter.setPen(QPen(QColor(55, 85, 150, 210), 1.0));
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
    if (event->button() == Qt::LeftButton && m_tool != EditorTool::Select) {
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
    const QRectF bounds = m_geometry.bounds.isEmpty() ? m_geometry.referenceBounds : m_geometry.bounds;
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
    } else {
        setCursor(Qt::CrossCursor);
    }
}

} // namespace vt
