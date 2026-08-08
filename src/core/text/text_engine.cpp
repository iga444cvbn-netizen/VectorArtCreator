#include "core/text/text_engine.h"

#include "core/document/document.h"

#include <QFontDatabase>
#include <QList>
#include <QTextLayout>
#include <QTextOption>

#include <limits>

namespace vt {

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
        + QString::number(object.typography.tracking, 'g', 16);
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
        result.warning = QStringLiteral("The requested system font '%1' is unavailable; Qt fallback is used for preview.")
                              .arg(object.font.family);
    } else if (!styleAvailable) {
        result.warning = QStringLiteral("The requested style '%1' is unavailable in '%2'; Qt fallback is used for preview.")
                              .arg(object.font.styleName, object.font.family);
    }

    const QFont font = object.font.toQFont(object.typography.fontSize);
    QFont shapedFont = font;
    shapedFont.setLetterSpacing(QFont::AbsoluteSpacing, object.typography.tracking);

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
    const QList<QGlyphRun> runs = layout.glyphRuns();
    for (const QGlyphRun& run : runs) {
        const QList<quint32> glyphIndexes = run.glyphIndexes();
        const QList<QPointF> positions = run.positions();
        const QRawFont rawFont = run.rawFont();
        if (!rawFont.isValid()) {
            continue;
        }
        for (int i = 0; i < glyphIndexes.size(); ++i) {
            ShapedGlyph glyph;
            glyph.glyphIndex = glyphIndexes[i];
            glyph.rawFont = rawFont;
            glyph.position = positions.value(i, QPointF());
            glyph.ordinal = ordinal++;
            result.glyphs.push_back(glyph);
        }
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
        placement.translate(glyph.position.x(), glyph.position.y());
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
