#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QVector>

namespace vt {

struct SampledContour {
    QVector<QPointF> points;
    bool closed = true;
};

class ContourSampler {
public:
    [[nodiscard]] static QVector<SampledContour> samplePath(const QPainterPath& path,
                                                             qreal tolerance,
                                                             int maxPointsPerContour = 8192);
    [[nodiscard]] static QPainterPath reconstructPath(const QVector<SampledContour>& contours,
                                                      Qt::FillRule fillRule,
                                                      qreal simplificationTolerance);
};

} // namespace vt
