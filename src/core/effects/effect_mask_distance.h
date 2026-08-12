#pragma once

#include "core/evaluation/work_control.h"

#include <QPainterPath>
#include <QPointF>
#include <QVector>

namespace vt {

// Shared narrow-phase contract for effect-mask preview and final evaluation.
// Bounds may reject work before this function is called, but only distance to
// actual flattened contour segments can authorize mask influence.
class EffectMaskDistance final {
public:
    [[nodiscard]] static qreal minimumContourDistance(
        const QPainterPath& path,
        const QVector<QPointF>& brushPoints,
        qreal flattenTolerance = 0.25,
        const WorkControl& work = WorkControl::unlimited());
    [[nodiscard]] static qreal strokeInfluence(
        const QPainterPath& path,
        const QVector<QPointF>& brushPoints,
        qreal radius,
        qreal hardness,
        qreal opacity,
        const WorkControl& work = WorkControl::unlimited());
};

} // namespace vt
