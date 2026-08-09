#pragma once

#include "core/deformation/manual_deformation.h"
#include "core/geometry/vector_geometry.h"

#include <QColor>
#include <QEnterEvent>
#include <QPoint>
#include <QTransform>
#include <QWidget>

namespace vt {

class EditorCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit EditorCanvas(QWidget* parent = nullptr);

    void setScene(const VectorGeometry& geometry, const QColor& fill);
    void setBrushSettings(BrushMode mode,
                          BrushTarget target,
                          qreal radius,
                          qreal strength,
                          qreal hardness);
    [[nodiscard]] qreal zoom() const;

public slots:
    void fitContent();

signals:
    void zoomChanged(qreal zoom);
    void deformationPreviewChanged(const DeformationStroke& stroke);
    void deformationPreviewCleared();
    void deformationStrokeReady(const DeformationStroke& stroke);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void setZoom(qreal value);
    [[nodiscard]] QTransform viewTransform() const;
    [[nodiscard]] QPointF documentPosition(const QPointF& widgetPosition) const;
    [[nodiscard]] DeformationStroke currentStroke() const;
    void updateBrushPreview();
    void cancelBrushStroke();
    void updateCursorShape();

    VectorGeometry m_geometry;
    QColor m_fill = QColor(24, 24, 28);
    qreal m_zoom = 1.0;
    QPointF m_panOffset;
    QPointF m_viewCenter;
    QPoint m_lastMousePosition;
    QPoint m_cursorPosition;
    QVector<QPointF> m_brushPositions;
    BrushMode m_brushMode = BrushMode::Push;
    BrushTarget m_brushTarget = BrushTarget::Shape;
    qreal m_brushRadius = 40.0;
    qreal m_brushStrength = 0.7;
    qreal m_brushHardness = 0.5;
    bool m_panning = false;
    bool m_brushing = false;
    bool m_spacePressed = false;
    bool m_hasCursorPosition = false;
    bool m_hasViewCenter = false;
    bool m_hasInitialFit = false;
};

} // namespace vt
