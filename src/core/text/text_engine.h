#pragma once

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
    int ordinal = -1;
    bool usesFallback = false;
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
    bool requestedFontAvailable = true;
    FontResolutionStatus fontResolutionStatus = FontResolutionStatus::RequestedFont;
    QVector<FallbackFontUsage> fallbackFonts;
    int fallbackGlyphCount = 0;
    QString warning;
    QString error;
};

class TextEngine {
public:
    [[nodiscard]] ShapedText shape(const TextObject& object);
    void clearCache();

private:
    [[nodiscard]] QString cacheKey(const TextObject& object) const;

    QString m_cachedKey;
    std::optional<ShapedText> m_cachedResult;
};

class GlyphGeometryBuilder {
public:
    [[nodiscard]] static VectorGeometry build(const ShapedText& shaped, qreal fallbackReferenceHeight);
};

} // namespace vt
