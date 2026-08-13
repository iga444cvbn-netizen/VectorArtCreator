#include "core/text/text_engine.h"

#include "core/document/document.h"

#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QList>
#include <QStringList>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

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

qreal resolvedRawFontPixelSize(const QRawFont& rawFont, const QFont& font)
{
    if (!rawFont.isValid()) {
        return 0.0;
    }
    if (std::isfinite(rawFont.pixelSize()) && rawFont.pixelSize() > 0.0) {
        return rawFont.pixelSize();
    }

    // Some Qt Windows/offscreen combinations return a valid glyph-run raw
    // font whose stored pixel size is zero when the QFont query was expressed
    // in points. QFontInfo still reports the size Qt resolved for the actual
    // screen font. Re-materializing this same physical raw font at that
    // resolved size preserves the raw-font metric source without falling back
    // to a hard-coded DPI or document font size.
    const int resolvedPixelSize = QFontInfo(font).pixelSize();
    if (resolvedPixelSize > 0) {
        QRawFont scaledRawFont = rawFont;
        scaledRawFont.setPixelSize(resolvedPixelSize);
        if (scaledRawFont.isValid() && std::isfinite(scaledRawFont.pixelSize())
            && scaledRawFont.pixelSize() > 0.0) {
            return scaledRawFont.pixelSize();
        }
    }

    // If the platform font database cannot report a pixel size (the Qt
    // offscreen plugin does this on some Windows runners), normalize the same
    // physical raw font to one pixel and recover the current em scale from
    // Qt's resolved screen-font ascent. This remains a raw-font metric
    // calculation: the normalized raw ascent supplies the font's design
    // ratio, while QFontMetricsF supplies the backend's resolved logical size.
    QRawFont normalizedRawFont = rawFont;
    normalizedRawFont.setPixelSize(1.0);
    const qreal normalizedAscent = normalizedRawFont.ascent();
    const qreal resolvedAscent = QFontMetricsF(font).ascent();
    if (normalizedRawFont.isValid() && std::isfinite(normalizedAscent)
        && normalizedAscent > 0.0 && std::isfinite(resolvedAscent)
        && resolvedAscent > 0.0) {
        return resolvedAscent / normalizedAscent;
    }
    return 0.0;
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

QVector<LogicalClusterSpan> TextEngine::logicalClusterSpans(
    int lineLength, const QVector<QVector<int>>& runStringIndexes)
{
    const int boundedLineLength = qMax(0, lineLength);
    QVector<int> boundaries;
    for (const QVector<int>& run : runStringIndexes) {
        for (int start : run) {
            if (start >= 0 && start < boundedLineLength) {
                boundaries.push_back(start);
            }
        }
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

    QVector<LogicalClusterSpan> spans;
    spans.reserve(boundaries.size());
    for (int index = 0; index < boundaries.size(); ++index) {
        const int start = boundaries.at(index);
        const int end = index + 1 < boundaries.size()
            ? boundaries.at(index + 1)
            : boundedLineLength;
        if (end > start) {
            spans.push_back({start, end - start});
        }
    }
    return spans;
}

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
        + QString::number(font.italic)
        + QChar(0x1f)
        + QString::number(font.underline)
        + QChar(0x1f)
        + QString::number(font.strikeOut)
        + QChar(0x1f)
        + QString::number(object.typography.fontSize, 'g', 16)
        + QChar(0x1f)
        + QString::number(object.typography.trackingEm, 'g', 16)
        + QChar(0x1f)
        + QString::number(object.typography.lineSpacing, 'g', 16);
}

ShapedText TextEngine::shape(const TextObject& object, const WorkControl& work)
{
    if (!work.consume(qMax(1, object.sourceText.size()))) {
        return {};
    }
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

    QTextOption option;
    option.setWrapMode(QTextOption::NoWrap);
    const QFontMetricsF fontMetrics(font);
    const qreal lineSpacing = qMax<qreal>(0.1, object.typography.lineSpacing);
    result.resolvedEmSize = resolvedRawFontPixelSize(requestedRawFont, font);

    int ordinal = 0;
    QVector<int> lineGlyphCounts;
    qreal lineTop = 0.0;
    auto appendLine = [&](const QString& lineText, int sourceStart) {
        if (!work.consume(qMax(1, lineText.size()))) {
            return;
        }
        const int lineIndex = result.lineBounds.size();
        QTextLayout lineLayout(lineText, shapedFont);
        lineLayout.setTextOption(option);
        lineLayout.setCacheEnabled(true);
        lineLayout.beginLayout();
        QTextLine line = lineLayout.createLine();
        qreal lineHeight = fontMetrics.lineSpacing();
        qreal lineWidth = 0.0;
        if (line.isValid()) {
            line.setLineWidth(1.0e9);
            line.setPosition(QPointF(0.0, 0.0));
            lineWidth = line.naturalTextWidth();
            lineHeight = line.height();
        }
        lineLayout.endLayout();
        lineHeight = qMax<qreal>(1.0, lineHeight);
        result.lineBounds.push_back(QRectF(0.0,
                                           lineTop,
                                           lineWidth,
                                           lineHeight));
        lineGlyphCounts.push_back(0);

        QList<QGlyphRun> runs;
        if (line.isValid()) {
            // The default glyphRuns() overload intentionally omits string
            // indexes. Request all data because cluster ownership is part of
            // the vector geometry contract.
            runs = line.glyphRuns(-1, -1, QTextLayout::RetrieveAll);
        }
        if (result.resolvedEmSize <= 0.0) {
            for (const QGlyphRun& run : runs) {
                result.resolvedEmSize = resolvedRawFontPixelSize(run.rawFont(), font);
                if (result.resolvedEmSize > 0.0) {
                    break;
                }
            }
        }
        QVector<QVector<int>> runClusterStarts;
        runClusterStarts.reserve(runs.size());
        for (const QGlyphRun& run : runs) {
            if (!work.consume()) {
                return;
            }
            const QList<quint32> glyphIndexes = run.glyphIndexes();
            const QList<qsizetype> stringIndexes = run.stringIndexes();
            if (stringIndexes.size() != glyphIndexes.size()) {
                continue;
            }
            if (!work.consume(qMax<qsizetype>(1, stringIndexes.size()))) {
                return;
            }
            QVector<int> starts;
            starts.reserve(stringIndexes.size());
            for (qsizetype start : stringIndexes) {
                if (start >= 0 && start < lineText.size()) {
                    starts.push_back(static_cast<int>(start));
                }
            }
            runClusterStarts.push_back(std::move(starts));
        }
        const QVector<LogicalClusterSpan> clusterSpans = logicalClusterSpans(
            static_cast<int>(lineText.size()), runClusterStarts);
        const auto clusterLengthFor = [&clusterSpans](int start) {
            const auto found = std::lower_bound(
                clusterSpans.cbegin(), clusterSpans.cend(), start,
                [](const LogicalClusterSpan& span, int value) { return span.start < value; });
            return found != clusterSpans.cend() && found->start == start
                ? found->length
                : 1;
        };
        const qreal trackingDistance = object.typography.trackingEm * result.resolvedEmSize;
        for (const QGlyphRun& run : runs) {
            const QList<quint32> glyphIndexes = run.glyphIndexes();
            const QList<QPointF> positions = run.positions();
            const QList<qsizetype> stringIndexes = run.stringIndexes();
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
                if (!work.consume()) {
                    return;
                }
                const QPointF rawPosition = positions.value(i, QPointF());
                const int lineOrdinal = lineGlyphCounts[lineIndex]++;
                const bool hasClusterIndexes = stringIndexes.size() == glyphIndexes.size();
                const qsizetype localClusterStart = hasClusterIndexes
                    ? qBound<qsizetype>(0, stringIndexes.at(i), lineText.size())
                    : -1;
                const int clusterStart = localClusterStart >= 0
                    ? sourceStart + static_cast<int>(localClusterStart)
                    : -1;
                const int clusterLength = localClusterStart >= 0
                    ? clusterLengthFor(static_cast<int>(localClusterStart))
                    : 1;
                ShapedGlyph glyph;
                glyph.glyphIndex = glyphIndexes[i];
                glyph.rawFont = rawFont;
                const QList<quint32> oneGlyph{glyphIndexes.at(i)};
                const auto advances = rawFont.advancesForGlyphIndexes(oneGlyph);
                glyph.advance = qMax<qreal>(0.0, advances.value(0).x());
                const qreal direction = run.isRightToLeft() ? -1.0 : 1.0;
                glyph.position = rawPosition
                    + QPointF(direction * trackingDistance * lineOrdinal, lineTop);
                glyph.ordinal = ordinal++;
                glyph.clusterStart = clusterStart;
                glyph.clusterLength = clusterLength;
                glyph.lineIndex = lineIndex;
                glyph.usesFallback = usesFallback;
                result.glyphs.push_back(glyph);
            }
        }
        // Decorations are built from lineBounds, so it must reflect the same
        // per-glyph tracking offsets used above.  Keep this per line: a
        // multiline layout does not have one shared tracking count.
        const int trackingPairs = qMax(0, lineGlyphCounts.at(lineIndex) - 1);
        const qreal trackedWidth = lineWidth + trackingDistance * trackingPairs;
        result.lineBounds[lineIndex].setWidth(qMax<qreal>(0.0, trackedWidth));
        lineTop += lineHeight * lineSpacing;
    };

    int sourceStart = 0;
    if (!object.sourceText.isEmpty()) {
        while (sourceStart <= object.sourceText.size()) {
            const int newline = object.sourceText.indexOf(QChar('\n'), sourceStart);
            const int sourceLength = newline >= 0
                ? newline - sourceStart
                : object.sourceText.size() - sourceStart;
            QString lineText = object.sourceText.mid(sourceStart, sourceLength);
            if (lineText.endsWith(QChar('\r'))) {
                lineText.chop(1);
            }
            appendLine(lineText, sourceStart);
            if (!work.isRunning()) {
                break;
            }
            if (newline < 0) {
                break;
            }
            sourceStart = newline + 1;
        }
    }

    result.lineCount = result.lineBounds.size();
    if (!result.lineBounds.isEmpty()) {
        result.logicalBounds = result.lineBounds.front();
        for (int index = 1; index < result.lineBounds.size(); ++index) {
            result.logicalBounds = result.logicalBounds.united(result.lineBounds.at(index));
        }
    }
    if (result.fontResolutionStatus == FontResolutionStatus::RequestedFont
        && result.fallbackGlyphCount > 0) {
        result.fontResolutionStatus = FontResolutionStatus::GlyphFallback;
        result.warning = fallbackWarning(result);
    }

    if (!object.sourceText.isEmpty() && result.glyphs.isEmpty()) {
        result.error = QStringLiteral("Qt did not return shaped glyph data for the current text and font.");
    }

    if (work.isRunning()) {
        m_cachedKey = key;
        m_cachedResult = result;
    }
    return result;
}

void TextEngine::clearCache()
{
    m_cachedKey.clear();
    m_cachedResult.reset();
}

VectorGeometry GlyphGeometryBuilder::build(const ShapedText& shaped,
                                           qreal fallbackReferenceHeight,
                                           bool underline,
                                           bool strikeOut,
                                           const WorkControl& work)
{
    VectorGeometry geometry;
    QRectF visibleBounds;
    bool hasVisibleBounds = false;

    for (const ShapedGlyph& glyph : shaped.glyphs) {
        if (!work.consume()) {
            break;
        }
        GeometryPiece piece;
        piece.sourceGlyphIndex = glyph.ordinal;
        piece.sourceClusterStart = glyph.clusterStart;
        piece.sourceClusterLength = glyph.clusterLength;
        piece.sourceLineIndex = glyph.lineIndex;
        piece.anchor = glyph.position;
        piece.originalAnchor = glyph.position;
        piece.layoutOrigin = glyph.position;
        piece.layoutAdvance = glyph.advance;

        QPainterPath glyphPath = glyph.rawFont.pathForGlyph(glyph.glyphIndex);
        if (!work.consume(qMax(1, glyphPath.elementCount()))) {
            break;
        }
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

    const auto addDecoration = [&geometry, &visibleBounds, &hasVisibleBounds](const QRectF& bounds,
                                                                                 int lineIndex,
                                                                                 qreal verticalCenter,
                                                                                 qreal thickness) {
        if (bounds.width() <= 0.0) {
            return;
        }
        const QRectF decoration(bounds.left(), verticalCenter - thickness * 0.5,
                               bounds.width(), thickness);
        GeometryPiece piece;
        piece.sourceLineIndex = lineIndex;
        piece.anchor = decoration.center();
        piece.originalAnchor = piece.anchor;
        piece.layoutOrigin = QPointF(decoration.left(), decoration.center().y());
        piece.layoutAdvance = decoration.width();
        piece.path.addRect(decoration);
        geometry.pieces.push_back(piece);
        visibleBounds = hasVisibleBounds ? visibleBounds.united(decoration) : decoration;
        hasVisibleBounds = true;
    };
    if (underline || strikeOut) {
        for (int lineIndex = 0; lineIndex < shaped.lineBounds.size(); ++lineIndex) {
            if (!work.consume()) {
                break;
            }
            const QRectF line = shaped.lineBounds.at(lineIndex);
            const qreal thickness = qMax<qreal>(1.0, line.height() * 0.055);
            if (underline) {
                addDecoration(line, lineIndex, line.bottom() - line.height() * 0.10, thickness);
            }
            if (strikeOut) {
                addDecoration(line, lineIndex, line.center().y() - line.height() * 0.10, thickness);
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
