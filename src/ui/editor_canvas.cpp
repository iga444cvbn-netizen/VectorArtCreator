#include "ui/editor_canvas.h"

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
    setCursor(Qt::ArrowCursor);
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
        return;
    }

    QTransform transform;
    transform.translate(width() * 0.5 + m_panOffset.x(), height() * 0.5 + m_panOffset.y());
    transform.scale(m_zoom, m_zoom);
    transform.translate(-bounds.center().x(), -bounds.center().y());

    painter.setTransform(transform);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_fill);
    painter.drawPath(m_geometry.combinedPath());

    painter.resetTransform();
    painter.setPen(QColor(150, 150, 160));
    painter.drawText(12, height() - 12, QStringLiteral("Zoom %1%   •   Wheel to zoom   •   Middle-drag to pan")
                                      .arg(qRound(m_zoom * 100.0)));
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
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void EditorCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_panning) {
        const QPoint current = event->position().toPoint();
        m_panOffset += QPointF(current - m_lastMousePosition);
        m_lastMousePosition = current;
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
        setCursor(m_spacePressed ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void EditorCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = true;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void EditorCanvas::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = false;
        if (!m_panning) {
            setCursor(Qt::ArrowCursor);
        }
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

} // namespace vt
