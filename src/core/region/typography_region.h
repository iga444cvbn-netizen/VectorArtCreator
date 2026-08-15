#pragma once

#include "core/evaluation/work_control.h"
#include "core/path/path_geometry.h"

#include <QJsonObject>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QVector>

#include <optional>

namespace vt {

// A text object has exactly one active typography layout mode.  Path and
// Region are deliberately represented by one tagged value rather than two
// independent enable flags.
enum class TypographyLayoutMode {
    Baseline,
    Path,
    Region,
};

[[nodiscard]] QString typographyLayoutModeToString(TypographyLayoutMode mode);
[[nodiscard]] bool typographyLayoutModeFromString(const QString& value,
                                                  TypographyLayoutMode* mode);

enum class RegionHorizontalAlignment {
    Left,
    Center,
    Right,
    Justified,
};

enum class RegionVerticalAlignment {
    Top,
    Center,
    Bottom,
};

enum class RegionOverflowMode {
    Clip,
};

enum class RegionPaddingSide {
    Left,
    Right,
    Top,
    Bottom,
};

[[nodiscard]] QString regionHorizontalAlignmentToString(RegionHorizontalAlignment alignment);
[[nodiscard]] bool regionHorizontalAlignmentFromString(
    const QString& value, RegionHorizontalAlignment* alignment);
[[nodiscard]] QString regionVerticalAlignmentToString(RegionVerticalAlignment alignment);
[[nodiscard]] bool regionVerticalAlignmentFromString(
    const QString& value, RegionVerticalAlignment* alignment);
[[nodiscard]] QString regionOverflowModeToString(RegionOverflowMode mode);
[[nodiscard]] bool regionOverflowModeFromString(const QString& value,
                                                RegionOverflowMode* mode);

struct RegionTypographyProperties {
    QString regionId;
    qreal paddingLeft = 16.0;
    qreal paddingRight = 16.0;
    qreal paddingTop = 16.0;
    qreal paddingBottom = 16.0;
    RegionHorizontalAlignment horizontalAlignment = RegionHorizontalAlignment::Left;
    RegionVerticalAlignment verticalAlignment = RegionVerticalAlignment::Top;
    RegionOverflowMode overflow = RegionOverflowMode::Clip;

    [[nodiscard]] bool isFinite() const;
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static bool fromJson(const QJsonObject& object,
                                       RegionTypographyProperties* properties,
                                       QString* error = nullptr);
    friend bool operator==(const RegionTypographyProperties&, const RegionTypographyProperties&) = default;
};

// The persistent region owns an explicit outer contour and zero or more hole
// contours.  PathGeometry remains the canonical node/segment representation;
// QPainterPath is only constructed on demand for drawing and hit testing.
struct TypographyRegion {
    static constexpr int MaximumHoles = 1024;

    QString id;
    PathGeometry outer;
    QVector<PathGeometry> holes;

    [[nodiscard]] static TypographyRegion makeRectangle(const QRectF& bounds);
    [[nodiscard]] static TypographyRegion makeEllipse(const QRectF& bounds);
    [[nodiscard]] static TypographyRegion makeCustom(const QRectF& bounds);
    [[nodiscard]] bool validate(QString* error = nullptr,
                                const WorkControl& work = WorkControl::unlimited()) const;
    [[nodiscard]] QPainterPath toPainterPath() const;
    [[nodiscard]] TypographyRegion duplicatedFresh() const;
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static bool fromJson(const QJsonObject& object,
                                       TypographyRegion* region,
                                       QString* error = nullptr,
                                       const WorkControl& work = WorkControl::unlimited());
    friend bool operator==(const TypographyRegion&, const TypographyRegion&) = default;
};

struct RegionInterval {
    qreal left = 0.0;
    qreal right = 0.0;

    [[nodiscard]] qreal width() const { return right - left; }
    friend bool operator==(const RegionInterval&, const RegionInterval&) = default;
};

// A bounded per-layout scanline representation. The persistent cubic model
// remains authoritative; this value is transient and is never serialized.
struct FlattenedTypographyRegion {
    QVector<QPointF> outer;
    QVector<QVector<QPointF>> holes;
};

// Deterministic bounded flattening is used for validation and scanline
// intersection.  It is not a replacement for the persistent cubic model.
[[nodiscard]] std::optional<QVector<QPointF>> flattenRegionContour(
    const PathGeometry& contour,
    qreal tolerance = 0.05,
    const WorkControl& work = WorkControl::unlimited());

[[nodiscard]] std::optional<FlattenedTypographyRegion> flattenTypographyRegion(
    const TypographyRegion& region,
    qreal tolerance = 0.05,
    const WorkControl& work = WorkControl::unlimited());

[[nodiscard]] QVector<RegionInterval> regionIntervalsAtY(
    const FlattenedTypographyRegion& region,
    qreal y,
    const WorkControl& work = WorkControl::unlimited());

// Return the filled horizontal intervals at one Y coordinate.  Outer/hole
// ownership is explicit; contour orientation is not consulted.
[[nodiscard]] QVector<RegionInterval> regionIntervalsAtY(
    const TypographyRegion& region,
    qreal y,
    const WorkControl& work = WorkControl::unlimited());

} // namespace vt
