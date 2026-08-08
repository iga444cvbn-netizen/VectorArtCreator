#pragma once

#include "core/geometry/vector_geometry.h"

#include <QColor>
#include <QPoint>
#include <QWidget>

namespace vt {

class EditorCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit EditorCanvas(QWidget* parent = nullptr);

    void setScene(const VectorGeometry& geometry, const QColor& fill);
    [[nodiscard]] qreal zoom() const;

public slots:
    void fitContent();

signals:
    void zoomChanged(qreal zoom);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setZoom(qreal value);

    VectorGeometry m_geometry;
    QColor m_fill = QColor(24, 24, 28);
    qreal m_zoom = 1.0;
    QPointF m_panOffset;
    QPoint m_lastMousePosition;
    bool m_panning = false;
    bool m_spacePressed = false;
    bool m_hasInitialFit = false;
};

} // namespace vt
