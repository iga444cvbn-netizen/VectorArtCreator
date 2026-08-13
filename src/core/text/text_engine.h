#pragma once

#include "core/evaluation/work_control.h"
#include "core/geometry/vector_geometry.h"

#include <QGlyphRun>
#include <QRawFont>
#include <QRectF>
#include <QString>
#include <QVector>

#include <optional>

namespace vt {

struct TextObject;

struct ShapedGlyph {
    quint32 glyphIndex = 0;
    QRawFont rawFont;
    QPointF position;
    qreal advance = 0.0;
    int ordinal = -1;
    int clusterStart = -1;
    int clusterLength = 1;
    int lineIndex = 0;
    bool usesFallback = false;
};

struct LogicalClusterSpan {
    int start = 0;
    int length = 0;

    friend bool operator==(const LogicalClusterSpan&, const LogicalClusterSpan&) = default;
};

enum class FontResolutionStatus {
    RequestedFont,
    MissingFamily,
    MissingStyle,
    GlyphFallback,
};

struct FallbackFontUsage {
    QRawFont rawFont;
    QString family;
    QString styleName;
    int glyphCount = 0;
};

struct ShapedText {
    QVector<ShapedGlyph> glyphs;
    QRectF logicalBounds;
    // The resolved em size in the same logical pixel coordinates used by
    // QTextLayout/QGlyphRun positions. It is exposed for diagnostics and
    // deterministic tests of relative tracking.
    qreal resolvedEmSize = 0.0;
    int lineCount = 0;
    QVector<QRectF> lineBounds;
    bool requestedFontAvailable = true;
    FontResolutionStatus fontResolutionStatus = FontResolutionStatus::RequestedFont;
    QVector<FallbackFontUsage> fallbackFonts;
    int fallbackGlyphCount = 0;
    QString warning;
    QString error;
};

class TextEngine {
public:
    [[nodiscard]] ShapedText shape(
        const TextObject& object,
        const WorkControl& work = WorkControl::unlimited());
    // Deterministic seam for the logical-cluster contract. Each inner vector
    // represents indexes reported by one physical-font/bidi run; spans come
    // from their complete line-wide union.
    [[nodiscard]] static QVector<LogicalClusterSpan> logicalClusterSpans(
        int lineLength, const QVector<QVector<int>>& runStringIndexes);
    void clearCache();

private:
    [[nodiscard]] QString cacheKey(const TextObject& object) const;

    QString m_cachedKey;
    std::optional<ShapedText> m_cachedResult;
};

class GlyphGeometryBuilder {
public:
    [[nodiscard]] static VectorGeometry build(const ShapedText& shaped,
                                              qreal fallbackReferenceHeight,
                                              bool underline = false,
                                              bool strikeOut = false,
                                              const WorkControl& work = WorkControl::unlimited());
};

} // namespace vt
