#include "core/text/text_engine.h"

#include "core/document/document.h"

#include <QFontDatabase>
#include <QList>
#include <QStringList>
#include <QTextLayout>
#include <QTextOption>

#include <cmath>
#include <limits>

namespace vt {

namespace {

bool samePhysicalFont(const QRawFont& left, const QRawFont& right)
{
    if (!left.isValid() || !right.isValid()) {
        return false;
    }
    // QRawFont equality identifies the physical font instance without asking
    // the Windows font backend to materialize its name table. That name-table
    // accessor is not reliable for every DirectWrite glyph run on Qt 6.8.
    return left == right;
}

void recordFallbackFont(ShapedText* result, const QRawFont& rawFont, int glyphCount)
{
    if (!result || glyphCount <= 0) {
        return;
    }
    result->fallbackGlyphCount += glyphCount;
    for (FallbackFontUsage& usage : result->fallbackFonts) {
        if (usage.rawFont == rawFont) {
            usage.glyphCount += glyphCount;
            return;
        }
    }
    // Keep the actual physical raw-font handle and glyph count. Some Qt 6
    // DirectWrite builds crash while asking a glyph-run raw font for its name,
    // so the UI-facing label is deliberately conservative here. A later
    // diagnostic layer can resolve the handle through a safe font database
    // query without making the shaping path depend on a platform name-table
    // accessor.
    result->fallbackFonts.push_back({rawFont,
                                     QStringLiteral("Qt fallback font"),
                                     {},
                                     glyphCount});
}

QString fallbackWarning(const ShapedText& result)
{
    QStringList fonts;
    for (const FallbackFontUsage& usage : result.fallbackFonts) {
        const QString face = usage.styleName.isEmpty()
            ? usage.family
            : QStringLiteral("%1 %2").arg(usage.family, usage.styleName);
        fonts.push_back(QStringLiteral("%1 (%2)").arg(face).arg(usage.glyphCount));
    }
    return QStringLiteral("Selected font does not contain all required glyphs. %1 glyphs use fallback font(s): %2.")
        .arg(result.fallbackGlyphCount)
        .arg(fonts.join(QStringLiteral(", ")));
}

} // namespace

QString TextEngine::cacheKey(const TextObject& object) const
{
    const FontDescriptor& font = object.font;
    return object.sourceText
        + QChar(0x1f)
        + font.family
        + QChar(0x1f)
        + font.styleName
        + QChar(0x1f)
        + QString::number(font.weight)
        + QChar(0x1f)
        + QString::number(object.typography.fontSize, 'g', 16)
        + QChar(0x1f)
        + QString::number(object.typography.trackingEm, 'g', 16);
}

ShapedText TextEngine::shape(const TextObject& object)
{
    const QString key = cacheKey(object);
    if (m_cachedResult.has_value() && m_cachedKey == key) {
        return *m_cachedResult;
    }

    ShapedText result;
    const QStringList families = QFontDatabase::families();
    const bool familyAvailable = object.font.family.isEmpty()
        || families.contains(object.font.family, Qt::CaseInsensitive);
    const bool styleAvailable = object.font.family.isEmpty()
        || object.font.styleName.isEmpty()
        || QFontDatabase::styles(object.font.family).contains(object.font.styleName, Qt::CaseInsensitive);
    result.requestedFontAvailable = familyAvailable && styleAvailable;

    if (!familyAvailable) {
        result.fontResolutionStatus = FontResolutionStatus::MissingFamily;
        result.warning = QStringLiteral("The requested system font '%1' is unavailable; Qt fallback is used for preview.")
                              .arg(object.font.family);
    } else if (!styleAvailable) {
        result.fontResolutionStatus = FontResolutionStatus::MissingStyle;
        result.warning = QStringLiteral("The requested style '%1' is unavailable in '%2'; Qt fallback is used for preview.")
                              .arg(object.font.styleName, object.font.family);
    }

    const QFont font = object.font.toQFont(object.typography.fontSize);
    const QRawFont requestedRawFont = QRawFont::fromFont(font);
    // Qt's PercentageSpacing is relative to shaped glyph advances. The
    // project model needs true em-relative tracking, so shape normally and
    // add a fixed offset based on the resolved raw font's pixel em size.
    const QFont shapedFont = font;

    QTextLayout layout(object.sourceText, shapedFont);
    QTextOption option;
    option.setWrapMode(QTextOption::NoWrap);
    layout.setTextOption(option);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (line.isValid()) {
        line.setLineWidth(1.0e9);
        line.setPosition(QPointF(0.0, 0.0));
    }
    layout.endLayout();

    result.logicalBounds = layout.boundingRect();
    int ordinal = 0;
    int shapedGlyphCount = 0;
    const QList<QGlyphRun> runs = layout.glyphRuns();
    if (requestedRawFont.isValid() && std::isfinite(requestedRawFont.pixelSize())
        && requestedRawFont.pixelSize() > 0.0) {
        result.resolvedEmSize = requestedRawFont.pixelSize();
    } else {
        for (const QGlyphRun& run : runs) {
            const QRawFont rawFont = run.rawFont();
            if (rawFont.isValid() && std::isfinite(rawFont.pixelSize())
                && rawFont.pixelSize() > 0.0) {
                result.resolvedEmSize = rawFont.pixelSize();
                break;
            }
        }
    }
    const qreal trackingDistance = object.typography.trackingEm * result.resolvedEmSize;
    for (const QGlyphRun& run : runs) {
        const QList<quint32> glyphIndexes = run.glyphIndexes();
        const QList<QPointF> positions = run.positions();
        const QRawFont rawFont = run.rawFont();
        if (!rawFont.isValid()) {
            continue;
        }
        const bool canDetectGlyphFallback = requestedRawFont.isValid();
        const bool usesFallback = result.fontResolutionStatus == FontResolutionStatus::RequestedFont
            && canDetectGlyphFallback
            && !samePhysicalFont(rawFont, requestedRawFont);
        if (usesFallback) {
            recordFallbackFont(&result, rawFont, glyphIndexes.size());
        }
        for (int i = 0; i < glyphIndexes.size(); ++i) {
            ShapedGlyph glyph;
            glyph.glyphIndex = glyphIndexes[i];
            glyph.rawFont = rawFont;
            const qreal direction = run.isRightToLeft() ? -1.0 : 1.0;
            glyph.position = positions.value(i, QPointF())
                + QPointF(direction * trackingDistance * shapedGlyphCount, 0.0);
            glyph.ordinal = ordinal++;
            glyph.usesFallback = usesFallback;
            result.glyphs.push_back(glyph);
            ++shapedGlyphCount;
        }
    }

    if (shapedGlyphCount > 1 && !qFuzzyIsNull(trackingDistance)) {
        const qreal adjustedWidth = result.logicalBounds.width()
            + trackingDistance * (shapedGlyphCount - 1);
        result.logicalBounds.setWidth(qMax<qreal>(0.0, adjustedWidth));
    }

    if (result.fontResolutionStatus == FontResolutionStatus::RequestedFont
        && result.fallbackGlyphCount > 0) {
        result.fontResolutionStatus = FontResolutionStatus::GlyphFallback;
        result.warning = fallbackWarning(result);
    }

    if (!object.sourceText.isEmpty() && result.glyphs.isEmpty()) {
        result.error = QStringLiteral("Qt did not return shaped glyph data for the current text and font.");
    }

    m_cachedKey = key;
    m_cachedResult = result;
    return result;
}

void TextEngine::clearCache()
{
    m_cachedKey.clear();
    m_cachedResult.reset();
}

VectorGeometry GlyphGeometryBuilder::build(const ShapedText& shaped, qreal fallbackReferenceHeight)
{
    VectorGeometry geometry;
    QRectF visibleBounds;
    bool hasVisibleBounds = false;

    for (const ShapedGlyph& glyph : shaped.glyphs) {
        GeometryPiece piece;
        piece.sourceGlyphIndex = glyph.ordinal;
        piece.anchor = glyph.position;
        piece.originalAnchor = glyph.position;

        QPainterPath glyphPath = glyph.rawFont.pathForGlyph(glyph.glyphIndex);
        QTransform placement;
        Q_UNUSED(placement.translate(glyph.position.x(), glyph.position.y()));
        piece.path = placement.map(glyphPath);
        geometry.pieces.push_back(piece);

        if (!piece.path.isEmpty()) {
            const QRectF pieceBounds = piece.path.boundingRect();
            if (!hasVisibleBounds) {
                visibleBounds = pieceBounds;
                hasVisibleBounds = true;
            } else {
                visibleBounds = visibleBounds.united(pieceBounds);
            }
        }
    }

    if (!hasVisibleBounds) {
        const qreal height = qMax<qreal>(1.0, fallbackReferenceHeight);
        const qreal width = qMax<qreal>(height, shaped.logicalBounds.width());
        visibleBounds = QRectF(0.0, -height, width, height);
    }

    geometry.setReferenceBounds(visibleBounds);
    geometry.recomputeBounds();
    return geometry;
}

} // namespace vt
