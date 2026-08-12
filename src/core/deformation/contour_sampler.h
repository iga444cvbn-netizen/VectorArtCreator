#pragma once

#include "core/evaluation/work_control.h"

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
                                                             int maxPointsPerContour = 8192,
                                                             const WorkControl& work = WorkControl::unlimited());
    [[nodiscard]] static QPainterPath reconstructPath(const QVector<SampledContour>& contours,
                                                      Qt::FillRule fillRule,
                                                      qreal simplificationTolerance,
                                                      const WorkControl& work = WorkControl::unlimited());
};

} // namespace vt
