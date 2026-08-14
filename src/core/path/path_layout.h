#pragma once

#include "core/evaluation/work_control.h"
#include "core/geometry/vector_geometry.h"
#include "core/path/path_geometry.h"

#include <QPointF>
#include <QVector>

#include <optional>

namespace vt {

struct ShapedText;

struct PathArcSample {
    qreal distance = 0.0;
    qreal t = 0.0;
    QPointF point;
    QPointF tangent = QPointF(1.0, 0.0);
};

struct PathArcSegment {
    QPointF p0;
    QPointF p1;
    QPointF p2;
    QPointF p3;
    qreal length = 0.0;
    QVector<PathArcSample> samples;
};

struct PathPosition {
    bool valid = false;
    qreal distance = 0.0;
    QPointF point;
    QPointF tangent = QPointF(1.0, 0.0);
};

// A bounded, deterministic arc-length lookup. Samples store the cubic
// parameter only as an implementation detail; callers query by geometric
// distance, never by raw cubic t.
class PathArcLengthTable {
public:
    static constexpr int MaximumSubdivisionDepth = 12;
    static constexpr int MaximumSamplesPerSegment = 2048;

    [[nodiscard]] static std::optional<PathArcLengthTable> build(
        const PathGeometry& path,
        const WorkControl& work = WorkControl::unlimited());

    [[nodiscard]] PathPosition positionAt(qreal distance,
                                           bool wrapClosed) const;
    [[nodiscard]] qreal totalLength() const { return m_totalLength; }
    [[nodiscard]] const QVector<PathArcSegment>& segments() const { return m_segments; }

private:
    QVector<PathArcSegment> m_segments;
    qreal m_totalLength = 0.0;
};

class PathLayoutEngine {
public:
    // Applies path layout to glyph geometry in object-local coordinates. The
    // caller must invoke this before effects and manual deformation.
    [[nodiscard]] static bool apply(VectorGeometry* geometry,
                                    const ShapedText& shaped,
                                    const PathGeometry& path,
                                    const PathTypographyProperties& settings,
                                    QString* error = nullptr,
                                    const WorkControl& work = WorkControl::unlimited());
};

} // namespace vt
