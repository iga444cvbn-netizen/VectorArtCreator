#include "core/region/region_layout.h"

#include <QHash>
#include <QSet>
#include <QTextBoundaryFinder>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace vt {
namespace {

constexpr qreal LayoutEpsilon = 1.0e-6;
constexpr int BandSamples = 9;
constexpr int MaximumVerticalIterations = 8;

struct Cluster {
    int start = -1;
    int length = 1;
    QVector<int> pieceIndices;
    qreal advance = 0.0;
    qreal sourceX = 0.0;
    QRectF sourceBounds;
    bool hasSourceBounds = false;
    bool breakAfter = false;
    bool justifyAfter = false;
    int visualOrder = std::numeric_limits<int>::max();
};

struct PlannedLine {
    int sourceLineIndex = 0;
    int clusterBegin = 0;
    int clusterEnd = 0;
    qreal lineTop = 0.0;
    qreal lineHeight = 1.0;
    qreal usedWidth = 0.0;
    RegionInterval interval;
    bool oversizedCluster = false;
    bool lastParagraphLine = false;
};

struct LayoutPass {
    QVector<PlannedLine> lines;
    qreal blockHeight = 0.0;
    bool clipped = false;
};

bool isFiniteRect(const QRectF& rect)
{
    return std::isfinite(rect.x()) && std::isfinite(rect.y())
        && std::isfinite(rect.width()) && std::isfinite(rect.height());
}

QRectF uniteRect(const QRectF& current,
                 bool* hasBounds,
                 const QRectF& value)
{
    if (!hasBounds || value.isEmpty() || !isFiniteRect(value)) return current;
    if (!*hasBounds) {
        *hasBounds = true;
        return value;
    }
    return current.united(value);
}

QSet<int> lineBreaks(const QString& sourceText, int start, int end)
{
    QSet<int> result;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Line, sourceText);
    finder.setPosition(qBound(0, start, sourceText.size()));
    for (;;) {
        const int boundary = finder.toNextBoundary();
        if (boundary < 0 || boundary > end) {
            break;
        }
        if (boundary > start && boundary <= end) result.insert(boundary);
    }
    // Qt's line boundary implementation varies slightly across font/platform
    // plugins. Whitespace is an explicit deterministic fallback.
    for (int index = start; index < end; ++index) {
        if (sourceText.at(index).isSpace()) result.insert(index + 1);
    }
    return result;
}

QVector<Cluster> buildClusters(const VectorGeometry& geometry,
                               const ShapedText& shaped,
                               const QString& sourceText,
                               int lineIndex)
{
    QVector<Cluster> clusters;
    QHash<quint64, int> byKey;
    for (int pieceIndex = 0; pieceIndex < geometry.pieces.size(); ++pieceIndex) {
        const GeometryPiece& piece = geometry.pieces.at(pieceIndex);
        if (piece.sourceGlyphIndex < 0 || piece.sourceLineIndex != lineIndex) continue;
        const int start = piece.sourceClusterStart;
        const int length = qMax(1, piece.sourceClusterLength);
        // Missing cluster data is kept as one cluster per glyph.  Normal Qt
        // shaping supplies UTF-16 cluster starts for all supported runs.
        const quint64 key = (static_cast<quint64>(static_cast<quint32>(start)) << 32U)
            | static_cast<quint32>(piece.sourceGlyphIndex);
        int clusterIndex = byKey.value(key, -1);
        if (start >= 0) {
            for (int index = 0; index < clusters.size(); ++index) {
                if (clusters.at(index).start == start) {
                    clusterIndex = index;
                    break;
                }
            }
        }
        if (clusterIndex < 0) {
            clusterIndex = clusters.size();
            clusters.push_back({start, length});
            byKey.insert(key, clusterIndex);
        }
        Cluster& cluster = clusters[clusterIndex];
        cluster.length = qMax(cluster.length, length);
        cluster.pieceIndices.push_back(pieceIndex);
        cluster.visualOrder = qMin(cluster.visualOrder, pieceIndex);
        cluster.advance += std::isfinite(piece.layoutAdvance)
            ? qMax<qreal>(0.0, piece.layoutAdvance) : 0.0;
        const qreal originX = std::isfinite(piece.layoutOrigin.x())
            ? piece.layoutOrigin.x() : piece.anchor.x();
        if (cluster.pieceIndices.size() == 1 || originX < cluster.sourceX) {
            cluster.sourceX = originX;
        }
        cluster.sourceBounds = uniteRect(cluster.sourceBounds,
                                         &cluster.hasSourceBounds,
                                         piece.path.boundingRect());
    }
    std::sort(clusters.begin(), clusters.end(), [](const Cluster& left,
                                                   const Cluster& right) {
        if (left.start < 0 && right.start >= 0) return false;
        if (left.start >= 0 && right.start < 0) return true;
        if (left.start != right.start) return left.start < right.start;
        return left.pieceIndices.value(0) < right.pieceIndices.value(0);
    });
    int sourceStart = sourceText.size();
    int sourceEnd = 0;
    for (const Cluster& cluster : clusters) {
        if (cluster.start >= 0) {
            sourceStart = qMin(sourceStart, cluster.start);
            sourceEnd = qMax(sourceEnd, cluster.start + cluster.length);
        }
    }
    if (sourceStart == sourceText.size()) sourceStart = 0;
    if (sourceEnd <= sourceStart) {
        sourceEnd = sourceText.indexOf(QChar('\n'), sourceStart);
        if (sourceEnd < 0) sourceEnd = sourceText.size();
    }
    const QSet<int> breaks = lineBreaks(sourceText, sourceStart, sourceEnd);
    for (Cluster& cluster : clusters) {
        const int end = cluster.start >= 0
            ? qMin(sourceText.size(), cluster.start + cluster.length) : -1;
        if (end > cluster.start && breaks.contains(end)) {
            cluster.breakAfter = true;
        } else if (cluster.start >= 0 && end > cluster.start) {
            for (int index = cluster.start; index < end; ++index) {
                if (sourceText.at(index).isSpace()) {
                    cluster.breakAfter = true;
                    cluster.justifyAfter = true;
                    break;
                }
            }
        }
    }
    return clusters;
}

QRectF sourceBand(const VectorGeometry& geometry,
                  const ShapedText& shaped,
                  int lineIndex)
{
    QRectF result;
    bool hasBounds = false;
    for (const GeometryPiece& piece : geometry.pieces) {
        if (piece.sourceGlyphIndex >= 0 && piece.sourceLineIndex == lineIndex) {
            result = uniteRect(result, &hasBounds, piece.path.boundingRect());
        }
    }
    if (!hasBounds && lineIndex >= 0 && lineIndex < shaped.lineBounds.size()) {
        result = shaped.lineBounds.at(lineIndex);
    }
    if (result.isEmpty() && lineIndex >= 0 && lineIndex < shaped.lineBounds.size()) {
        result = shaped.lineBounds.at(lineIndex);
    }
    return result;
}

QVector<RegionInterval> intersectIntervals(const QVector<RegionInterval>& left,
                                           const QVector<RegionInterval>& right)
{
    QVector<RegionInterval> result;
    for (const RegionInterval& a : left) {
        for (const RegionInterval& b : right) {
            const qreal start = qMax(a.left, b.left);
            const qreal end = qMin(a.right, b.right);
            if (end - start > LayoutEpsilon) result.push_back({start, end});
        }
    }
    return result;
}

std::optional<RegionInterval> widestInterval(const QVector<RegionInterval>& intervals)
{
    std::optional<RegionInterval> result;
    for (const RegionInterval& interval : intervals) {
        if (interval.width() <= LayoutEpsilon) continue;
        if (!result.has_value()
            || interval.width() > result->width() + LayoutEpsilon
            || (std::abs(interval.width() - result->width()) <= LayoutEpsilon
                && (interval.left < result->left - LayoutEpsilon
                    || (std::abs(interval.left - result->left) <= LayoutEpsilon
                        && interval.right < result->right)))) {
            result = interval;
        }
    }
    return result;
}

qreal clusterFlowWidth(const Cluster& cluster)
{
    const qreal visualWidth = cluster.hasSourceBounds
        ? qMax<qreal>(0.0, cluster.sourceBounds.width()) : 0.0;
    return qMax<qreal>(cluster.advance, visualWidth);
}

QVector<RegionInterval> safeIntervalsForBand(const TypographyRegion& region,
                                             qreal top,
                                             qreal bottom,
                                             const RegionTypographyProperties& settings,
                                             const WorkControl& work)
{
    if (!std::isfinite(top) || !std::isfinite(bottom) || bottom <= top) return {};
    const auto outer = flattenRegionContour(region.outer, 0.05, work);
    if (!outer.has_value() || !work.isRunning()) return {};
    QRectF outerBounds;
    for (const QPointF& point : *outer) outerBounds = outerBounds.united(QRectF(point, QSizeF()));
    const qreal contentTop = outerBounds.top() + settings.paddingTop;
    const qreal contentBottom = outerBounds.bottom() - settings.paddingBottom;
    if (top < contentTop - LayoutEpsilon || bottom > contentBottom + LayoutEpsilon) return {};

    const qreal height = bottom - top;
    const qreal edgeInset = qMin<qreal>(height * 0.001, 0.25);
    QVector<RegionInterval> common;
    for (int sample = 0; sample < BandSamples; ++sample) {
        if (!work.consume()) return {};
        qreal y = top + (height * sample) / static_cast<qreal>(BandSamples - 1);
        if (height > edgeInset * 2.0) {
            y = qBound(top + edgeInset, y, bottom - edgeInset);
        } else {
            y = (top + bottom) * 0.5;
        }
        QVector<RegionInterval> current = regionIntervalsAtY(region, y, work);
        for (RegionInterval& interval : current) {
            interval.left += settings.paddingLeft;
            interval.right -= settings.paddingRight;
        }
        current.erase(std::remove_if(current.begin(), current.end(), [](const RegionInterval& interval) {
            return interval.width() <= LayoutEpsilon;
        }), current.end());
        if (current.isEmpty()) return {};
        common = common.isEmpty() ? current : intersectIntervals(common, current);
        if (common.isEmpty()) return {};
    }
    std::sort(common.begin(), common.end(), [](const RegionInterval& left,
                                               const RegionInterval& right) {
        if (left.left != right.left) return left.left < right.left;
        return left.right < right.right;
    });
    return common;
}

qreal lineAdvance(const QRectF& line, qreal spacing)
{
    return qMax<qreal>(1.0, qMax<qreal>(1.0, line.height())
                              * qBound<qreal>(0.1, spacing, 8.0));
}

qreal clusterWidth(const QVector<Cluster>& clusters, int begin, int end)
{
    qreal width = 0.0;
    for (int index = begin; index < end; ++index) {
        width += clusterFlowWidth(clusters.at(index));
    }
    return std::isfinite(width) ? qMax<qreal>(0.0, width) : 0.0;
}

LayoutPass layoutAtOrigin(const VectorGeometry& geometry,
                          const ShapedText& shaped,
                          const QString& sourceText,
                          const TypographyRegion& region,
                          const RegionTypographyProperties& settings,
                          qreal spacing,
                          qreal origin,
                          const QVector<QVector<Cluster>>& allClusters,
                          const QVector<QRectF>& bands,
                          const WorkControl& work)
{
    LayoutPass pass;
    qreal lineTop = origin;
    for (int sourceLineIndex = 0; sourceLineIndex < shaped.lineBounds.size(); ++sourceLineIndex) {
        const QVector<Cluster>& clusters = allClusters.at(sourceLineIndex);
        int cursor = 0;
        if (clusters.isEmpty()) {
            const QRectF sourceLine = shaped.lineBounds.at(sourceLineIndex);
            const qreal height = qMax<qreal>(1.0, sourceLine.height());
            const qreal delta = lineTop - sourceLine.top();
            const QRectF band = bands.at(sourceLineIndex).translated(0.0, delta);
            const QVector<RegionInterval> intervals = safeIntervalsForBand(
                region, band.top(), band.bottom(), settings, work);
            const std::optional<RegionInterval> selected = widestInterval(intervals);
            if (!selected.has_value()) {
                pass.clipped = true;
                break;
            }
            pass.lines.push_back({sourceLineIndex, 0, 0, lineTop, height, 0.0,
                                  *selected, false, true});
            lineTop += lineAdvance(sourceLine, spacing);
            pass.blockHeight = lineTop - origin;
            continue;
        }
        while (cursor < clusters.size()) {
            if (!work.consume()) {
                pass.clipped = true;
                return pass;
            }
            const QRectF sourceLine = shaped.lineBounds.at(sourceLineIndex);
            const qreal delta = lineTop - sourceLine.top();
            const QRectF band = bands.at(sourceLineIndex).translated(0.0, delta);
            const QVector<RegionInterval> intervals = safeIntervalsForBand(
                region, band.top(), band.bottom(), settings, work);
            const std::optional<RegionInterval> selected = widestInterval(intervals);
            if (!selected.has_value()) {
                pass.clipped = true;
                return pass;
            }
            const RegionInterval interval = *selected;
            const qreal capacity = interval.width();
            int end = cursor;
            int lastLegalBreak = -1;
            qreal used = 0.0;
            bool oversized = false;
            for (int index = cursor; index < clusters.size(); ++index) {
                const Cluster& cluster = clusters.at(index);
                const qreal flowWidth = clusterFlowWidth(cluster);
                if (index == cursor && flowWidth > capacity + LayoutEpsilon) {
                    end = cursor + 1;
                    oversized = true;
                    break;
                }
                if (used + flowWidth <= capacity + LayoutEpsilon) {
                    used += flowWidth;
                    end = index + 1;
                    if (cluster.breakAfter) lastLegalBreak = index;
                    continue;
                }
                break;
            }
            if (end <= cursor) {
                end = cursor + 1;
                oversized = clusterFlowWidth(clusters.at(cursor)) > capacity + LayoutEpsilon;
            } else if (end < clusters.size() && lastLegalBreak >= cursor) {
                end = lastLegalBreak + 1;
                used = clusterWidth(clusters, cursor, end);
            }
            const bool lastParagraphLine = end >= clusters.size();
            pass.lines.push_back({sourceLineIndex, cursor, end, lineTop,
                                  qMax<qreal>(1.0, sourceLine.height()), used,
                                  interval, oversized, lastParagraphLine});
            cursor = end;
            lineTop += lineAdvance(sourceLine, spacing);
            pass.blockHeight = lineTop - origin;
        }
    }
    if (!pass.lines.isEmpty()) {
        const PlannedLine& last = pass.lines.constLast();
        // The final line contributes its visible line height, not the
        // inter-line leading that would follow it. This keeps Center/Bottom
        // vertical alignment centered on the actual text block.
        pass.blockHeight = qMax<qreal>(0.0,
                                       (last.lineTop - origin) + last.lineHeight);
    } else {
        pass.blockHeight = 0.0;
    }
    Q_UNUSED(sourceText);
    Q_UNUSED(geometry);
    return pass;
}

qreal verticalOrigin(qreal contentTop,
                     qreal contentHeight,
                     qreal blockHeight,
                     RegionVerticalAlignment alignment)
{
    const qreal freeSpace = qMax<qreal>(0.0, contentHeight - blockHeight);
    switch (alignment) {
    case RegionVerticalAlignment::Top: return contentTop;
    case RegionVerticalAlignment::Center: return contentTop + freeSpace * 0.5;
    case RegionVerticalAlignment::Bottom: return contentTop + freeSpace;
    }
    return contentTop;
}

void clearRegionMetadata(VectorGeometry* geometry)
{
    if (!geometry) return;
    for (GeometryPiece& piece : geometry->pieces) {
        piece.effectReferenceAnchor = QPointF();
        piece.hasEffectReferenceAnchor = false;
        piece.effectReferenceProgress = 0.0;
        piece.hasEffectReferenceProgress = false;
    }
}

} // namespace

bool RegionLayoutEngine::apply(VectorGeometry* geometry,
                               const ShapedText& shaped,
                               const QString& sourceText,
                               const TypographyRegion& region,
                               const RegionTypographyProperties& settings,
                               qreal lineSpacing,
                               QString* error,
                               const WorkControl& work)
{
    if (!geometry) {
        if (error) *error = QStringLiteral("Region layout received null geometry.");
        return false;
    }
    if (settings.overflow != RegionOverflowMode::Clip
        || !settings.isFinite()
        || settings.regionId.trimmed().isEmpty()
        || settings.regionId != region.id) {
        if (error) *error = QStringLiteral("Region typography contains invalid settings or reference.");
        return false;
    }
    if (!std::isfinite(lineSpacing)) {
        if (error) *error = QStringLiteral("Region typography contains invalid line spacing.");
        return false;
    }
    if (!work.consume()) {
        if (error) *error = work.interruptionMessage();
        return false;
    }
    if (!region.validate(error, work)) return false;

    const auto outer = flattenRegionContour(region.outer, 0.05, work);
    if (!outer.has_value() || !work.isRunning()) {
        if (error) *error = work.interruptionMessage();
        return false;
    }
    QRectF regionBounds;
    for (const QPointF& point : *outer) regionBounds = regionBounds.united(QRectF(point, QSizeF()));
    if (regionBounds.width() <= LayoutEpsilon || regionBounds.height() <= LayoutEpsilon) {
        if (error) *error = QStringLiteral("Region has no usable area.");
        return false;
    }

    QVector<QVector<Cluster>> allClusters;
    QVector<QRectF> bands;
    allClusters.reserve(shaped.lineBounds.size());
    bands.reserve(shaped.lineBounds.size());
    for (int lineIndex = 0; lineIndex < shaped.lineBounds.size(); ++lineIndex) {
        if (!work.consume()) {
            if (error) *error = work.interruptionMessage();
            return false;
        }
        allClusters.push_back(buildClusters(*geometry, shaped, sourceText, lineIndex));
        bands.push_back(sourceBand(*geometry, shaped, lineIndex));
    }

    const qreal contentTop = regionBounds.top() + settings.paddingTop;
    const qreal contentBottom = regionBounds.bottom() - settings.paddingBottom;
    const qreal contentHeight = qMax<qreal>(0.0, contentBottom - contentTop);
    qreal origin = contentTop;
    LayoutPass pass;
    int previousLineCount = -1;
    for (int iteration = 0; iteration < MaximumVerticalIterations; ++iteration) {
        pass = layoutAtOrigin(*geometry, shaped, sourceText, region, settings,
                              lineSpacing, origin, allClusters, bands, work);
        if (!work.isRunning()) {
            if (error) *error = work.interruptionMessage();
            return false;
        }
        const qreal nextOrigin = verticalOrigin(contentTop, contentHeight,
                                                pass.blockHeight, settings.verticalAlignment);
        if (std::abs(nextOrigin - origin) <= 0.01
            && previousLineCount == pass.lines.size()) {
            break;
        }
        previousLineCount = pass.lines.size();
        origin = nextOrigin;
    }
    pass = layoutAtOrigin(*geometry, shaped, sourceText, region, settings,
                          lineSpacing, origin, allClusters, bands, work);
    if (!work.isRunning()) {
        if (error) *error = work.interruptionMessage();
        return false;
    }

    VectorGeometry candidate = *geometry;
    clearRegionMetadata(&candidate);
    QVector<bool> assigned(candidate.pieces.size(), false);
    QVector<int> laidOutGlyphs;
    QHash<int, QVector<int>> decorationsBySourceLine;
    for (int index = 0; index < candidate.pieces.size(); ++index) {
        if (candidate.pieces.at(index).sourceGlyphIndex < 0) {
            decorationsBySourceLine[candidate.pieces.at(index).sourceLineIndex].push_back(index);
        }
    }
    int outputLineIndex = 0;
    for (const PlannedLine& line : pass.lines) {
        if (!work.consume()) {
            if (error) *error = work.interruptionMessage();
            return false;
        }
        const QVector<Cluster>& clusters = allClusters.at(line.sourceLineIndex);
        const RegionInterval interval = line.interval;
        const qreal capacity = interval.width();
        const int spaceCount = [&] {
            if (line.lastParagraphLine
                || settings.horizontalAlignment != RegionHorizontalAlignment::Justified) return 0;
            int count = 0;
            for (int index = line.clusterBegin; index < line.clusterEnd; ++index) {
                if (clusters.at(index).justifyAfter) ++count;
            }
            return count;
        }();
        const qreal extraSpace = spaceCount > 0
            ? qMax<qreal>(0.0, (capacity - line.usedWidth)
                               / static_cast<qreal>(spaceCount)) : 0.0;
        qreal alignmentOffset = 0.0;
        if (settings.horizontalAlignment == RegionHorizontalAlignment::Center) {
            alignmentOffset = qMax<qreal>(0.0, (capacity - line.usedWidth) * 0.5);
        } else if (settings.horizontalAlignment == RegionHorizontalAlignment::Right) {
            alignmentOffset = qMax<qreal>(0.0, capacity - line.usedWidth);
        }
        qreal cursor = interval.left + qMin(alignmentOffset, capacity);

        QVector<int> placementOrder;
        placementOrder.reserve(qMax(0, line.clusterEnd - line.clusterBegin));
        for (int clusterIndex = line.clusterBegin;
             clusterIndex < line.clusterEnd && clusterIndex < clusters.size();
             ++clusterIndex) {
            placementOrder.push_back(clusterIndex);
        }
        std::stable_sort(placementOrder.begin(), placementOrder.end(),
                         [&clusters](int left, int right) {
                             if (clusters.at(left).visualOrder
                                 != clusters.at(right).visualOrder) {
                                 return clusters.at(left).visualOrder
                                     < clusters.at(right).visualOrder;
                             }
                             return left < right;
                         });
        for (const int clusterIndex : placementOrder) {
            const Cluster& cluster = clusters.at(clusterIndex);
            const qreal flowWidth = clusterFlowWidth(cluster);
            const qreal targetX = cursor;
            const bool placed = flowWidth <= interval.right - cursor + LayoutEpsilon;
            qreal dx = targetX - cluster.sourceX;
            const bool fits = !line.oversizedCluster && placed;
            if (!fits) {
                for (int pieceIndex : cluster.pieceIndices) {
                    if (pieceIndex >= 0 && pieceIndex < candidate.pieces.size()) {
                        candidate.pieces[pieceIndex].path = QPainterPath();
                        candidate.pieces[pieceIndex].sourceLineIndex = outputLineIndex;
                        assigned[pieceIndex] = true;
                    }
                }
            } else {
                // Correct small font side bearings while retaining the
                // cluster's shaped internal geometry. A cluster wider than
                // the interval is clipped as one legal semantic unit.
                if (cluster.hasSourceBounds) {
                    const qreal minimumDx = targetX - cluster.sourceBounds.left();
                    const qreal maximumDx = targetX + flowWidth - cluster.sourceBounds.right();
                    dx = qBound(minimumDx, dx, maximumDx);
                }
                for (int pieceIndex : cluster.pieceIndices) {
                    if (pieceIndex < 0 || pieceIndex >= candidate.pieces.size()) continue;
                    GeometryPiece& piece = candidate.pieces[pieceIndex];
                    QTransform transform;
                    Q_UNUSED(transform.translate(dx, line.lineTop
                                                     - shaped.lineBounds.at(line.sourceLineIndex).top()));
                    piece.path = transform.map(piece.path);
                    piece.anchor = transform.map(piece.anchor);
                    piece.layoutOrigin = QPointF(targetX, line.lineTop);
                    piece.sourceLineIndex = outputLineIndex;
                    piece.effectReferenceAnchor = piece.anchor;
                    piece.hasEffectReferenceAnchor = true;
                    assigned[pieceIndex] = true;
                }
            }
            if (placed) cursor += flowWidth;
            if (cluster.justifyAfter && spaceCount > 0) {
                cursor += extraSpace;
            }
        }
        // A line is placed in shaped visual order, but progression-dependent
        // effects intentionally consume logical source order. The stable
        // cluster metadata makes those two contracts independent.
        for (int clusterIndex = line.clusterBegin;
             clusterIndex < line.clusterEnd && clusterIndex < clusters.size();
             ++clusterIndex) {
            for (const int pieceIndex : clusters.at(clusterIndex).pieceIndices) {
                if (pieceIndex >= 0 && pieceIndex < candidate.pieces.size()
                    && assigned.at(pieceIndex)
                    && candidate.pieces.at(pieceIndex).hasEffectReferenceAnchor) {
                    laidOutGlyphs.push_back(pieceIndex);
                }
            }
        }

        // Decorations are derived from the shaped source line. Use the first
        // wrapped line for each source decoration; glyph layout remains the
        // authoritative region geometry.
        for (int pieceIndex : decorationsBySourceLine.value(line.sourceLineIndex)) {
            if (pieceIndex < 0 || pieceIndex >= candidate.pieces.size() || assigned[pieceIndex]) continue;
            GeometryPiece& piece = candidate.pieces[pieceIndex];
            const QRectF sourceRect = piece.path.boundingRect();
            const qreal width = qMax<qreal>(0.0,
                                            qMin(interval.right,
                                                 interval.left + line.usedWidth)
                                                - interval.left);
            const qreal sourceLineTop = shaped.lineBounds.at(line.sourceLineIndex).top();
            const qreal relativeY = sourceRect.center().y() - sourceLineTop;
            const QRectF target(interval.left,
                                line.lineTop + relativeY - sourceRect.height() * 0.5,
                                width, sourceRect.height());
            QPainterPath decoration;
            if (target.width() > LayoutEpsilon && target.height() > 0.0) decoration.addRect(target);
            piece.path = decoration;
            piece.anchor = target.center();
            piece.layoutOrigin = QPointF(target.left(), target.center().y());
            piece.sourceLineIndex = outputLineIndex;
            piece.effectReferenceAnchor = piece.anchor;
            piece.hasEffectReferenceAnchor = true;
            assigned[pieceIndex] = true;
        }
        ++outputLineIndex;
    }
    for (int index = 0; index < candidate.pieces.size(); ++index) {
        if (!assigned.at(index)) {
            candidate.pieces[index].path = QPainterPath();
            candidate.pieces[index].hasEffectReferenceProgress = false;
        }
    }
    const int progressCount = laidOutGlyphs.size();
    for (int ordinal = 0; ordinal < progressCount; ++ordinal) {
        GeometryPiece& piece = candidate.pieces[laidOutGlyphs.at(ordinal)];
        piece.effectReferenceProgress = progressCount <= 1
            ? 0.0 : static_cast<qreal>(ordinal) / static_cast<qreal>(progressCount - 1);
        piece.hasEffectReferenceProgress = true;
    }
    if (!work.isRunning()) {
        if (error) *error = work.interruptionMessage();
        return false;
    }
    candidate.setReferenceBounds(regionBounds);
    candidate.recomputeBounds();
    *geometry = std::move(candidate);
    return true;
}

} // namespace vt
