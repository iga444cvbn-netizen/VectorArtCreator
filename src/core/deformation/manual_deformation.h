#pragma once

#include "core/geometry/vector_geometry.h"

#include <QJsonObject>
#include <QPointF>
#include <QVector>

namespace vt {

enum class BrushMode {
    Push,
    Pull,
    Inflate,
    Pinch,
    Smooth,
};

enum class BrushTarget {
    Glyphs,
    Shape,
};

// New strokes are authored in object-local coordinates.  LegacyPageAmbiguous
// is retained only to identify pre-v5 files: its numeric samples are left
// untouched on load because their original object transform was not saved with
// each stroke and therefore cannot be recovered safely.
enum class DeformationCoordinateSpace {
    ObjectLocal,
    LegacyPageAmbiguous,
};

struct BrushSample {
    QPointF position;
    QPointF delta;
    qreal pressure = 1.0;

    friend bool operator==(const BrushSample&, const BrushSample&) = default;
};

struct DeformationStroke {
    BrushMode mode = BrushMode::Push;
    BrushTarget target = BrushTarget::Shape;
    qreal radius = 40.0;
    qreal strength = 0.7;
    qreal hardness = 0.5;
    DeformationCoordinateSpace coordinateSpace = DeformationCoordinateSpace::ObjectLocal;
    QVector<BrushSample> samples;
};

[[nodiscard]] QString brushModeToString(BrushMode mode);
[[nodiscard]] bool brushModeFromString(const QString& value, BrushMode* mode);
[[nodiscard]] QString brushTargetToString(BrushTarget target);
[[nodiscard]] bool brushTargetFromString(const QString& value, BrushTarget* target);

// Converts irregular pointer positions into bounded document-space samples.
// The returned delta is the movement since the preceding emitted sample.
[[nodiscard]] QVector<BrushSample> resampleBrushStroke(const QVector<QPointF>& positions,
                                                        qreal spacing,
                                                        qreal pressure = 1.0,
                                                        int maxSamples = 4096);

class ManualDeformation {
public:
    bool enabled = true;
    qreal strength = 1.0;
    QVector<DeformationStroke> strokes;

    void apply(VectorGeometry& geometry) const;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static bool fromJson(const QJsonObject& object,
                                       ManualDeformation* deformation,
                                       QString* error = nullptr);
};

} // namespace vt
