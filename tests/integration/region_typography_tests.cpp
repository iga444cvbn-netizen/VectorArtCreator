#include "core/document/document.h"
#include "core/evaluation/work_control.h"
#include "core/region/region_layout.h"
#include "core/serialization/project_serializer.h"
#include "core/text/text_engine.h"
#include "tests/support/state_fingerprint.h"
#include "tests/support/test_fonts.h"
#include "ui/editor_controller.h"

#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainterPath>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

using namespace vt;

namespace {

PathGeometry polygonContour(const QString& id, const QVector<QPointF>& points)
{
    PathGeometry contour;
    contour.id = id;
    contour.closed = true;
    contour.nodes.reserve(points.size());
    for (int index = 0; index < points.size(); ++index) {
        PathNode node;
        node.id = QStringLiteral("%1-node-%2").arg(id).arg(index);
        node.anchor = points.at(index);
        contour.nodes.push_back(node);
    }
    return contour;
}

TypographyRegion rectangleRegion(const QString& id, const QRectF& bounds)
{
    TypographyRegion region;
    region.id = id;
    region.outer = polygonContour(
        id + QStringLiteral("-outer"),
        {bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()});
    return region;
}

PathGeometry rectangleContour(const QString& id, const QRectF& bounds)
{
    return polygonContour(
        id,
        {bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()});
}

VectorGeometry glyphGeometry(const QVector<int>& clusterStarts,
                             const QVector<int>& clusterLengths,
                             const QVector<qreal>& advances,
                             qreal glyphWidth,
                             qreal y = 0.0)
{
    VectorGeometry geometry;
    qreal x = 0.0;
    for (int index = 0; index < clusterStarts.size(); ++index) {
        GeometryPiece piece;
        piece.path.addRect(QRectF(x, y, glyphWidth, 10.0));
        piece.anchor = QPointF(x, y + 5.0);
        piece.originalAnchor = piece.anchor;
        piece.layoutOrigin = QPointF(x, y);
        piece.layoutAdvance = advances.at(index);
        piece.sourceGlyphIndex = index;
        piece.sourceClusterStart = clusterStarts.at(index);
        piece.sourceClusterLength = clusterLengths.at(index);
        piece.sourceLineIndex = 0;
        piece.effectReferenceAnchor = piece.anchor;
        piece.hasEffectReferenceAnchor = true;
        geometry.pieces.push_back(piece);
        x += advances.at(index);
    }
    geometry.setReferenceBounds(QRectF(0.0, y, qMax<qreal>(x, glyphWidth), 10.0));
    geometry.recomputeBounds();
    return geometry;
}

VectorGeometry glyphGeometryWithWidths(const QVector<int>& clusterStarts,
                                        const QVector<int>& clusterLengths,
                                        const QVector<qreal>& advances,
                                        const QVector<qreal>& widths,
                                        qreal y = 0.0)
{
    VectorGeometry geometry;
    qreal x = 0.0;
    for (int index = 0; index < clusterStarts.size(); ++index) {
        GeometryPiece piece;
        piece.path.addRect(QRectF(x, y, widths.at(index), 10.0));
        piece.anchor = QPointF(x + widths.at(index) * 0.5, y + 5.0);
        piece.originalAnchor = piece.anchor;
        piece.layoutOrigin = QPointF(x, y);
        piece.layoutAdvance = advances.at(index);
        piece.sourceGlyphIndex = index;
        piece.sourceClusterStart = clusterStarts.at(index);
        piece.sourceClusterLength = clusterLengths.at(index);
        piece.sourceLineIndex = 0;
        piece.effectReferenceAnchor = piece.anchor;
        piece.hasEffectReferenceAnchor = true;
        geometry.pieces.push_back(piece);
        x += advances.at(index);
    }
    geometry.setReferenceBounds(QRectF(0.0, y, qMax<qreal>(x, 1.0), 10.0));
    geometry.recomputeBounds();
    return geometry;
}

ShapedText oneLineShape(qreal width, qreal y = 0.0)
{
    ShapedText shaped;
    shaped.lineCount = 1;
    shaped.lineBounds = {QRectF(0.0, y, width, 10.0)};
    shaped.logicalBounds = shaped.lineBounds.front();
    return shaped;
}

qreal leftOf(const GeometryPiece& piece)
{
    return piece.path.boundingRect().left();
}

QByteArray geometryState(const VectorGeometry& geometry)
{
    QByteArray result;
    for (const GeometryPiece& piece : geometry.pieces) {
        const QRectF bounds = piece.path.boundingRect();
        result += QByteArray::number(bounds.left(), 'f', 6);
        result += ',';
        result += QByteArray::number(bounds.top(), 'f', 6);
        result += ',';
        result += QByteArray::number(bounds.width(), 'f', 6);
        result += ';';
        result += QByteArray::number(piece.anchor.x(), 'f', 6);
        result += ',';
        result += QByteArray::number(piece.anchor.y(), 'f', 6);
        result += '|';
    }
    return result;
}

RegionTypographyProperties regionSettings(const TypographyRegion& region)
{
    RegionTypographyProperties settings;
    settings.regionId = region.id;
    settings.paddingLeft = 0.0;
    settings.paddingRight = 0.0;
    settings.paddingTop = 0.0;
    settings.paddingBottom = 0.0;
    return settings;
}

} // namespace

class RegionTypographyTests final : public QObject {
    Q_OBJECT

private slots:
    void scanlineIntervalsHandleConcavityHolesAndCubics();
    void regionLayoutWrapsWithoutSplittingClusters();
    void regionLayoutHonorsAlignmentPaddingAndClip();
    void regionVerticalReflowIsDeterministic();
    void regionLayoutUsesLogicalJustificationAndCancellation();
    void regionLayoutJustifiesWhitespaceWithIndependentGeometryOracle();
    void regionLayoutRejectsBetweenSampleBandConcavityAndHole();
    void regionLayoutDefinesUnbreakableWordFallback();
    void regionLayoutShapesRealBidiTextWithoutClusterLoss();
    void regionValidationRejectsAdversarialTopologyAndCancellation();
    void regionSerializationMigratesAndRejectsDuplicateIdentity();
    void controllerRegionEditsUndoAndFreshenDuplicateIdentity();
};

void RegionTypographyTests::scanlineIntervalsHandleConcavityHolesAndCubics()
{
    TypographyRegion withHole = rectangleRegion(QStringLiteral("hole-region"),
                                                QRectF(0.0, 0.0, 200.0, 100.0));
    withHole.holes.push_back(rectangleContour(QStringLiteral("hole-contour"),
                                              QRectF(60.0, 30.0, 80.0, 40.0)));
    QString error;
    QVERIFY2(withHole.validate(&error), qPrintable(error));

    const QVector<RegionInterval> holeIntervals = regionIntervalsAtY(withHole, 50.0);
    QCOMPARE(holeIntervals.size(), 2);
    QVERIFY(std::abs(holeIntervals.at(0).left - 0.0) < 1.0e-4);
    QVERIFY(std::abs(holeIntervals.at(0).right - 60.0) < 1.0e-4);
    QVERIFY(std::abs(holeIntervals.at(1).left - 140.0) < 1.0e-4);
    QVERIFY(std::abs(holeIntervals.at(1).right - 200.0) < 1.0e-4);

    TypographyRegion concave;
    concave.id = QStringLiteral("concave-region");
    concave.outer = polygonContour(
        QStringLiteral("concave-outer"),
        {{0.0, 0.0}, {200.0, 0.0}, {200.0, 40.0},
         {100.0, 40.0}, {100.0, 120.0}, {0.0, 120.0}});
    QVERIFY2(concave.validate(&error), qPrintable(error));
    const QVector<RegionInterval> upper = regionIntervalsAtY(concave, 20.0);
    const QVector<RegionInterval> lower = regionIntervalsAtY(concave, 80.0);
    QCOMPARE(upper.size(), 1);
    QCOMPARE(lower.size(), 1);
    QVERIFY(std::abs(upper.front().width() - 200.0) < 1.0e-4);
    QVERIFY(std::abs(lower.front().width() - 100.0) < 1.0e-4);

    TypographyRegion ellipse = TypographyRegion::makeEllipse(QRectF(0.0, 0.0, 200.0, 100.0));
    ellipse.id = QStringLiteral("cubic-ellipse");
    QVERIFY2(ellipse.validate(&error), qPrintable(error));
    const QVector<RegionInterval> center = regionIntervalsAtY(ellipse, 50.0);
    QCOMPARE(center.size(), 1);
    QVERIFY(std::abs(center.front().left - 0.0) < 0.2);
    QVERIFY(std::abs(center.front().right - 200.0) < 0.2);

    TypographyRegion custom = TypographyRegion::makeCustom(QRectF(0.0, 0.0, 200.0, 100.0));
    QVERIFY2(custom.validate(&error), qPrintable(error));
    const QVector<RegionInterval> customCenter = regionIntervalsAtY(custom, 49.0);
    QCOMPARE(customCenter.size(), 2);
    QVERIFY(customCenter.at(0).width() > 0.0);
    QVERIFY(customCenter.at(1).width() > 0.0);
}

void RegionTypographyTests::regionLayoutWrapsWithoutSplittingClusters()
{
    TypographyRegion region = rectangleRegion(QStringLiteral("wrap-region"),
                                               QRectF(0.0, 0.0, 100.0, 100.0));
    RegionTypographyProperties settings = regionSettings(region);
    const QVector<int> starts = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    const QVector<int> lengths(starts.size(), 1);
    const QVector<qreal> advances(starts.size(), 20.0);
    VectorGeometry geometry = glyphGeometry(starts, lengths, advances, 20.0);
    const ShapedText shaped = oneLineShape(200.0);

    QString error;
    QVERIFY2(RegionLayoutEngine::apply(&geometry, shaped, QString(10, QLatin1Char('a')),
                                       region, settings, 1.0, &error),
             qPrintable(error));
    QVERIFY(!geometry.pieces.at(0).path.isEmpty());
    QVERIFY(!geometry.pieces.at(4).path.isEmpty());
    QVERIFY(!geometry.pieces.at(5).path.isEmpty());
    QVERIFY(std::abs(geometry.pieces.at(0).path.boundingRect().top()) < 1.0e-4);
    QVERIFY(std::abs(geometry.pieces.at(5).path.boundingRect().top() - 10.0) < 1.0e-4);

    // Two physical glyphs share one UTF-16 cluster. An interval narrower than
    // that cluster clears the complete cluster, then continues with the next
    // cluster rather than piling both glyphs at an endpoint.
    TypographyRegion clusterRegion = rectangleRegion(QStringLiteral("cluster-region"),
                                                      QRectF(0.0, 0.0, 15.0, 100.0));
    RegionTypographyProperties clusterSettings = regionSettings(clusterRegion);
    VectorGeometry clusterGeometry = glyphGeometry(
        {0, 0, 2}, {2, 2, 1}, {8.0, 8.0, 8.0}, 8.0);
    QVERIFY2(RegionLayoutEngine::apply(&clusterGeometry, oneLineShape(24.0),
                                       QString::fromUtf8("😀x"), clusterRegion,
                                       clusterSettings, 1.0, &error),
             qPrintable(error));
    QVERIFY(clusterGeometry.pieces.at(0).path.isEmpty());
    QVERIFY(clusterGeometry.pieces.at(1).path.isEmpty());
    QVERIFY(!clusterGeometry.pieces.at(2).path.isEmpty());
    QVERIFY(std::abs(clusterGeometry.pieces.at(2).path.boundingRect().top() - 10.0) < 1.0e-4);

    // A line band crossing a hole exposes two legal intervals. Phase 5 keeps
    // one continuous interval per logical line, choosing the widest interval
    // and the leftmost interval for equal-width ties.
    TypographyRegion splitRegion = rectangleRegion(QStringLiteral("split-region"),
                                                   QRectF(0.0, 0.0, 200.0, 100.0));
    splitRegion.holes.push_back(rectangleContour(QStringLiteral("split-hole"),
                                                 QRectF(60.0, 0.5, 80.0, 9.0)));
    QVERIFY2(splitRegion.validate(&error), qPrintable(error));
    VectorGeometry splitGeometry = glyphGeometry(
        {0, 1, 2, 3, 4, 5}, {1, 1, 1, 1, 1, 1},
        {20.0, 20.0, 20.0, 20.0, 20.0, 20.0}, 20.0);
    QVERIFY2(RegionLayoutEngine::apply(&splitGeometry, oneLineShape(120.0),
                                       QString(6, QLatin1Char('a')), splitRegion,
                                       regionSettings(splitRegion), 1.0, &error),
             qPrintable(error));
    for (int index = 0; index < 3; ++index) {
        const QRectF bounds = splitGeometry.pieces.at(index).path.boundingRect();
        QVERIFY(!splitGeometry.pieces.at(index).path.isEmpty());
        QVERIFY(bounds.right() <= 60.0 + 1.0e-4);
        QVERIFY(std::abs(bounds.top()) < 1.0e-4);
    }
    for (int index = 3; index < 6; ++index) {
        const QRectF bounds = splitGeometry.pieces.at(index).path.boundingRect();
        QVERIFY(!splitGeometry.pieces.at(index).path.isEmpty());
        QVERIFY(std::abs(bounds.top() - 10.0) < 1.0e-4);
        QVERIFY(bounds.left() >= -1.0e-4);
        QVERIFY(bounds.right() <= 200.0 + 1.0e-4);
    }
    QVERIFY(std::abs(splitGeometry.pieces.at(3).path.boundingRect().left()) < 1.0e-4);

    TypographyRegion unequalRegion = rectangleRegion(QStringLiteral("unequal-region"),
                                                     QRectF(0.0, 0.0, 200.0, 100.0));
    unequalRegion.holes.push_back(rectangleContour(QStringLiteral("unequal-hole"),
                                                   QRectF(30.0, 0.5, 30.0, 9.0)));
    QVERIFY2(unequalRegion.validate(&error), qPrintable(error));
    VectorGeometry unequalGeometry = glyphGeometry(
        {0, 1, 2, 3, 4}, {1, 1, 1, 1, 1},
        {20.0, 20.0, 20.0, 20.0, 20.0}, 20.0);
    QVERIFY2(RegionLayoutEngine::apply(&unequalGeometry, oneLineShape(100.0),
                                       QString(5, QLatin1Char('a')), unequalRegion,
                                       regionSettings(unequalRegion), 1.0, &error),
             qPrintable(error));
    for (int index = 0; index < 5; ++index) {
        const QRectF bounds = unequalGeometry.pieces.at(index).path.boundingRect();
        QVERIFY(!unequalGeometry.pieces.at(index).path.isEmpty());
        QVERIFY(bounds.left() >= 60.0 - 1.0e-4);
        QVERIFY(bounds.right() <= 200.0 + 1.0e-4);
    }

    // A shaped RTL run can arrive in physical visual order while its UTF-16
    // cluster starts descend. Placement follows that shaped order; the
    // source cluster metadata remains untouched for logical effects/ranges.
    VectorGeometry visualOrder;
    for (int visualIndex = 0; visualIndex < 3; ++visualIndex) {
        GeometryPiece piece;
        piece.path.addRect(QRectF(visualIndex * 20.0, 0.0, 20.0, 10.0));
        piece.anchor = QPointF(visualIndex * 20.0 + 10.0, 5.0);
        piece.originalAnchor = piece.anchor;
        piece.layoutOrigin = QPointF(visualIndex * 20.0, 0.0);
        piece.layoutAdvance = 20.0;
        piece.sourceGlyphIndex = visualIndex;
        piece.sourceClusterStart = 2 - visualIndex;
        piece.sourceClusterLength = 1;
        piece.sourceLineIndex = 0;
        visualOrder.pieces.push_back(piece);
    }
    visualOrder.setReferenceBounds(QRectF(0.0, 0.0, 60.0, 10.0));
    visualOrder.recomputeBounds();
    const QString rtl = QString(QChar(0x05D0)) + QChar(0x05D1) + QChar(0x05D2);
    QVERIFY2(RegionLayoutEngine::apply(&visualOrder, oneLineShape(60.0), rtl,
                                       region, regionSettings(region), 1.0, &error),
             qPrintable(error));
    QVERIFY(std::abs(visualOrder.pieces.at(0).path.boundingRect().left() - 0.0) < 1.0e-4);
    QVERIFY(std::abs(visualOrder.pieces.at(2).path.boundingRect().left() - 40.0) < 1.0e-4);
    QCOMPARE(visualOrder.pieces.at(0).sourceClusterStart, 2);
    QCOMPARE(visualOrder.pieces.at(2).sourceClusterStart, 0);
}

void RegionTypographyTests::regionLayoutHonorsAlignmentPaddingAndClip()
{
    const TypographyRegion region = rectangleRegion(QStringLiteral("alignment-region"),
                                                     QRectF(0.0, 0.0, 100.0, 100.0));
    const ShapedText shaped = oneLineShape(20.0);
    const QString source = QStringLiteral("a");

    for (const auto alignment : {RegionHorizontalAlignment::Left,
                                 RegionHorizontalAlignment::Center,
                                 RegionHorizontalAlignment::Right}) {
        RegionTypographyProperties settings = regionSettings(region);
        settings.horizontalAlignment = alignment;
        VectorGeometry geometry = glyphGeometry({0}, {1}, {10.0}, 10.0);
        QString error;
        QVERIFY2(RegionLayoutEngine::apply(&geometry, shaped, source, region, settings,
                                           1.0, &error), qPrintable(error));
        const qreal expectedLeft = alignment == RegionHorizontalAlignment::Left ? 0.0
            : alignment == RegionHorizontalAlignment::Center ? 45.0 : 90.0;
        QVERIFY(std::abs(leftOf(geometry.pieces.front()) - expectedLeft) < 1.0e-4);
    }

    RegionTypographyProperties padded = regionSettings(region);
    padded.paddingLeft = 10.0;
    padded.paddingRight = 10.0;
    padded.paddingTop = 10.0;
    padded.paddingBottom = 10.0;
    VectorGeometry paddedGeometry = glyphGeometry({0}, {1}, {10.0}, 10.0);
    QString error;
    QVERIFY2(RegionLayoutEngine::apply(&paddedGeometry, shaped, source, region, padded,
                                       1.0, &error), qPrintable(error));
    QVERIFY(std::abs(leftOf(paddedGeometry.pieces.front()) - 10.0) < 1.0e-4);
    QVERIFY(std::abs(paddedGeometry.pieces.front().path.boundingRect().top() - 10.0) < 1.0e-4);

    RegionTypographyProperties centered = regionSettings(region);
    centered.verticalAlignment = RegionVerticalAlignment::Center;
    VectorGeometry centeredGeometry = glyphGeometry({0}, {1}, {10.0}, 10.0);
    QVERIFY2(RegionLayoutEngine::apply(&centeredGeometry, shaped, source, region, centered,
                                       1.0, &error), qPrintable(error));
    QVERIFY(std::abs(centeredGeometry.pieces.front().path.boundingRect().top() - 45.0) < 1.0e-4);

    // Vertical centering uses the visible final line height, not an extra
    // trailing line-spacing advance. The expected block is 30px tall when
    // two 10px lines use 2x spacing, so it starts at 35px in this region.
    VectorGeometry multiline;
    for (int line = 0; line < 2; ++line) {
        GeometryPiece piece;
        piece.path.addRect(QRectF(0.0, line * 10.0, 10.0, 10.0));
        piece.anchor = QPointF(5.0, line * 10.0 + 5.0);
        piece.originalAnchor = piece.anchor;
        piece.layoutOrigin = QPointF(0.0, line * 10.0);
        piece.layoutAdvance = 10.0;
        piece.sourceGlyphIndex = line;
        piece.sourceClusterStart = line == 0 ? 0 : 2;
        piece.sourceClusterLength = 1;
        piece.sourceLineIndex = line;
        multiline.pieces.push_back(piece);
    }
    multiline.setReferenceBounds(QRectF(0.0, 0.0, 20.0, 20.0));
    multiline.recomputeBounds();
    ShapedText twoLines;
    twoLines.lineCount = 2;
    twoLines.lineBounds = {QRectF(0.0, 0.0, 20.0, 10.0),
                           QRectF(0.0, 10.0, 20.0, 10.0)};
    twoLines.logicalBounds = twoLines.lineBounds.front().united(twoLines.lineBounds.at(1));
    RegionTypographyProperties spacedCenter = regionSettings(region);
    spacedCenter.verticalAlignment = RegionVerticalAlignment::Center;
    QVERIFY2(RegionLayoutEngine::apply(&multiline, twoLines, QStringLiteral("a\na"),
                                       region, spacedCenter, 2.0, &error), qPrintable(error));
    QVERIFY(std::abs(multiline.pieces.at(0).path.boundingRect().top() - 35.0) < 1.0e-4);
    QVERIFY(std::abs(multiline.pieces.at(1).path.boundingRect().top() - 55.0) < 1.0e-4);

    // A single overlong cluster is clipped as one unit and never translated
    // repeatedly to the last interval endpoint.
    const TypographyRegion narrow = rectangleRegion(QStringLiteral("narrow-region"),
                                                     QRectF(0.0, 0.0, 15.0, 100.0));
    VectorGeometry overlong = glyphGeometry({0}, {1}, {40.0}, 40.0);
    QVERIFY2(RegionLayoutEngine::apply(&overlong, oneLineShape(40.0), source, narrow,
                                       regionSettings(narrow), 1.0, &error), qPrintable(error));
    QVERIFY(overlong.pieces.front().path.isEmpty());
}

void RegionTypographyTests::regionVerticalReflowIsDeterministic()
{
    const TypographyRegion region = rectangleRegion(
        QStringLiteral("vertical-reflow-region"), QRectF(0.0, 0.0, 42.0, 140.0));
    const QVector<int> starts = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    const QVector<int> lengths(starts.size(), 1);
    const QVector<qreal> advances(starts.size(), 15.0);
    const QVector<qreal> widths(starts.size(), 13.0);
    const QString source = QStringLiteral("abcdefghijkl");
    RegionTypographyProperties settings = regionSettings(region);
    settings.verticalAlignment = RegionVerticalAlignment::Center;

    VectorGeometry first = glyphGeometryWithWidths(starts, lengths, advances, widths);
    VectorGeometry second = first;
    QString error;
    QVERIFY2(RegionLayoutEngine::apply(&first, oneLineShape(180.0), source, region,
                                       settings, 1.7, &error), qPrintable(error));
    QVERIFY2(RegionLayoutEngine::apply(&second, oneLineShape(180.0), source, region,
                                       settings, 1.7, &error), qPrintable(error));
    QCOMPARE(geometryState(first), geometryState(second));
    for (const GeometryPiece& piece : first.pieces) {
        if (!piece.path.isEmpty()) {
            QVERIFY(piece.sourceLineIndex >= 0);
            QVERIFY(piece.hasEffectReferenceAnchor);
        }
    }
}

void RegionTypographyTests::regionLayoutUsesLogicalJustificationAndCancellation()
{
    const TypographyRegion region = rectangleRegion(QStringLiteral("justify-region"),
                                                     QRectF(0.0, 0.0, 20.0, 100.0));
    RegionTypographyProperties settings = regionSettings(region);
    settings.horizontalAlignment = RegionHorizontalAlignment::Justified;
    VectorGeometry geometry = glyphGeometry({0, 1, 2}, {1, 1, 1},
                                             {8.0, 8.0, 8.0}, 8.0);
    QString error;
    QVERIFY2(RegionLayoutEngine::apply(&geometry, oneLineShape(24.0),
                                       QStringLiteral("a a"), region, settings, 1.0,
                                       &error), qPrintable(error));
    for (const GeometryPiece& piece : geometry.pieces) {
        if (!piece.path.isEmpty()) {
            const QRectF bounds = piece.path.boundingRect();
            QVERIFY(bounds.left() >= -1.0e-4);
            QVERIFY(bounds.right() <= 20.0 + 1.0e-4);
        }
    }

    const VectorGeometry original = glyphGeometry({0, 1, 2, 3}, {1, 1, 1, 1},
                                                    {8.0, 8.0, 8.0, 8.0}, 8.0);
    VectorGeometry interrupted = original;
    const QByteArray before = geometryState(interrupted);
    const WorkControl work = WorkControl::withBudget(1);
    QVERIFY(!RegionLayoutEngine::apply(&interrupted, oneLineShape(32.0),
                                       QStringLiteral("aaaa"), region, settings, 1.0,
                                       &error, work));
    QCOMPARE(geometryState(interrupted), before);
}

void RegionTypographyTests::regionLayoutJustifiesWhitespaceWithIndependentGeometryOracle()
{
    const TypographyRegion region = rectangleRegion(
        QStringLiteral("justification-oracle"), QRectF(0.0, 0.0, 100.0, 100.0));
    const QString source = QStringLiteral("a a");
    const QVector<int> starts = {0, 1, 2};
    const QVector<int> lengths = {1, 1, 1};
    const QVector<qreal> advances = {10.0, 5.0, 10.0};
    const QVector<qreal> widths = {8.0, 5.0, 8.0};

    RegionTypographyProperties leftSettings = regionSettings(region);
    leftSettings.horizontalAlignment = RegionHorizontalAlignment::Left;
    VectorGeometry left = glyphGeometryWithWidths(starts, lengths, advances, widths);
    QString error;
    QVERIFY2(RegionLayoutEngine::apply(&left, oneLineShape(30.0), source, region,
                                       leftSettings, 1.0, &error), qPrintable(error));

    RegionTypographyProperties justifiedSettings = leftSettings;
    justifiedSettings.horizontalAlignment = RegionHorizontalAlignment::Justified;
    VectorGeometry justified = glyphGeometryWithWidths(starts, lengths, advances, widths);
    QVERIFY2(RegionLayoutEngine::apply(&justified, oneLineShape(30.0), source, region,
                                       justifiedSettings, 1.0, &error), qPrintable(error));

    const qreal leftSecondWord = left.pieces.at(2).path.boundingRect().left();
    const qreal justifiedSecondWord = justified.pieces.at(2).path.boundingRect().left();
    const qreal expectedExtra = 100.0 - (advances.at(0) + advances.at(1) + advances.at(2));
    QVERIFY2(justifiedSecondWord > leftSecondWord + 1.0,
             "Justified layout must visibly redistribute whitespace.");
    QVERIFY(std::abs((justifiedSecondWord - leftSecondWord) - expectedExtra) < 1.0e-4);
    QVERIFY(std::abs(justified.pieces.at(2).path.boundingRect().width()
                     - left.pieces.at(2).path.boundingRect().width()) < 1.0e-4);

    const QString repeatedSource = QStringLiteral("a a a");
    const QVector<int> repeatedStarts = {0, 1, 2, 3, 4};
    const QVector<int> repeatedLengths(5, 1);
    const QVector<qreal> repeatedAdvances = {8.0, 2.0, 8.0, 2.0, 8.0};
    const QVector<qreal> repeatedWidths = repeatedAdvances;
    VectorGeometry repeatedLeft = glyphGeometryWithWidths(
        repeatedStarts, repeatedLengths, repeatedAdvances, repeatedWidths);
    VectorGeometry repeatedJustified = repeatedLeft;
    QVERIFY2(RegionLayoutEngine::apply(&repeatedLeft, oneLineShape(40.0), repeatedSource,
                                       region, leftSettings, 1.0, &error), qPrintable(error));
    QVERIFY2(RegionLayoutEngine::apply(&repeatedJustified, oneLineShape(40.0), repeatedSource,
                                       region, justifiedSettings, 1.0, &error),
             qPrintable(error));
    const qreal perOpportunity = (100.0 - 40.0) / 2.0;
    QVERIFY(std::abs((repeatedJustified.pieces.at(2).path.boundingRect().left()
                      - repeatedLeft.pieces.at(2).path.boundingRect().left())
                     - perOpportunity) < 1.0e-4);
    QVERIFY(std::abs((repeatedJustified.pieces.at(4).path.boundingRect().left()
                      - repeatedLeft.pieces.at(4).path.boundingRect().left())
                     - perOpportunity * 2.0) < 1.0e-4);

    // A wrapped paragraph distributes only on its non-final line. The final
    // word is intentionally an indivisible shaping cluster and must not move
    // merely because the first line used Justified alignment.
    const QString wrappedSource = QStringLiteral("a a BIG");
    const TypographyRegion wrappedRegion = rectangleRegion(
        QStringLiteral("wrapped-justification-oracle"), QRectF(0.0, 0.0, 30.0, 100.0));
    VectorGeometry wrappedLeft = glyphGeometryWithWidths(
        {0, 1, 2, 3, 4}, {1, 1, 1, 1, 3},
        {8.0, 2.0, 8.0, 2.0, 20.0},
        {8.0, 2.0, 8.0, 2.0, 20.0});
    VectorGeometry wrappedJustified = wrappedLeft;
    RegionTypographyProperties wrappedLeftSettings = regionSettings(wrappedRegion);
    RegionTypographyProperties wrappedJustifiedSettings = wrappedLeftSettings;
    wrappedJustifiedSettings.horizontalAlignment = RegionHorizontalAlignment::Justified;
    QVERIFY2(RegionLayoutEngine::apply(&wrappedLeft, oneLineShape(30.0), wrappedSource,
                                       wrappedRegion, wrappedLeftSettings, 1.0, &error),
             qPrintable(error));
    QVERIFY2(RegionLayoutEngine::apply(&wrappedJustified, oneLineShape(30.0), wrappedSource,
                                       wrappedRegion, wrappedJustifiedSettings, 1.0, &error),
             qPrintable(error));
    QVERIFY(std::abs(wrappedJustified.pieces.at(2).path.boundingRect().left()
                     - wrappedLeft.pieces.at(2).path.boundingRect().left() - 5.0) < 1.0e-4);
    QVERIFY(std::abs(wrappedJustified.pieces.at(4).path.boundingRect().left()
                     - wrappedLeft.pieces.at(4).path.boundingRect().left()) < 1.0e-4);
}

void RegionTypographyTests::regionLayoutRejectsBetweenSampleBandConcavityAndHole()
{
    QString error;
    TypographyRegion notch = rectangleRegion(QStringLiteral("between-sample-notch"),
                                             QRectF(0.0, 0.0, 100.0, 20.0));
    notch.outer = polygonContour(
        QStringLiteral("between-sample-notch-outer"),
        {{0.0, 0.0}, {100.0, 0.0}, {100.0, 4.6}, {40.0, 4.6},
         {40.0, 4.9}, {100.0, 4.9}, {100.0, 20.0}, {0.0, 20.0}});
    QVERIFY2(notch.validate(&error), qPrintable(error));
    VectorGeometry notchGeometry = glyphGeometry({0}, {1}, {60.0}, 60.0);
    QVERIFY2(RegionLayoutEngine::apply(&notchGeometry, oneLineShape(60.0),
                                       QStringLiteral("a"), notch,
                                       regionSettings(notch), 1.0, &error),
             qPrintable(error));
    QVERIFY(!notch.toPainterPath().contains(QPointF(50.0, 4.75)));
    QVERIFY2(notchGeometry.pieces.front().path
                 .subtracted(notch.toPainterPath()).isEmpty(),
             "A glyph may not leak through a concavity between scanline samples.");

    TypographyRegion hole = rectangleRegion(QStringLiteral("between-sample-hole"),
                                             QRectF(0.0, 0.0, 100.0, 20.0));
    hole.holes.push_back(rectangleContour(QStringLiteral("between-sample-hole-contour"),
                                          QRectF(40.0, 4.6, 20.0, 0.3)));
    QVERIFY2(hole.validate(&error), qPrintable(error));
    VectorGeometry holeGeometry = glyphGeometry({0}, {1}, {80.0}, 80.0);
    QVERIFY2(RegionLayoutEngine::apply(&holeGeometry, oneLineShape(80.0),
                                       QStringLiteral("a"), hole,
                                       regionSettings(hole), 1.0, &error),
             qPrintable(error));
    QVERIFY(!hole.toPainterPath().contains(QPointF(50.0, 4.75)));
    QVERIFY2(holeGeometry.pieces.front().path
                 .subtracted(hole.toPainterPath()).isEmpty(),
             "A glyph may not leak through a hole between scanline samples.");
}

void RegionTypographyTests::regionLayoutDefinesUnbreakableWordFallback()
{
    const TypographyRegion region = rectangleRegion(
        QStringLiteral("unbreakable-word"), QRectF(0.0, 0.0, 20.0, 100.0));
    const RegionTypographyProperties settings = regionSettings(region);
    QString error;
    for (const QString& source : {QStringLiteral("abcdefgh"), QString::fromUtf8("абвг")}) {
        VectorGeometry geometry = glyphGeometry({0, 1, 2, 3}, {1, 1, 1, 1},
                                                 {10.0, 10.0, 10.0, 10.0}, 10.0);
        QVERIFY2(RegionLayoutEngine::apply(&geometry, oneLineShape(40.0), source,
                                           region, settings, 1.0, &error),
                 qPrintable(error));
        QVERIFY(!geometry.pieces.at(0).path.isEmpty());
        QVERIFY(!geometry.pieces.at(1).path.isEmpty());
        QVERIFY(!geometry.pieces.at(2).path.isEmpty());
        QVERIFY(!geometry.pieces.at(3).path.isEmpty());
        QVERIFY(std::abs(geometry.pieces.at(0).path.boundingRect().top()
                         - geometry.pieces.at(1).path.boundingRect().top()) < 1.0e-4);
        QVERIFY(geometry.pieces.at(2).path.boundingRect().top()
                > geometry.pieces.at(1).path.boundingRect().top());
    }

    const QString emojiWord = QString::fromUtf8("😀😀");
    VectorGeometry emojiGeometry = glyphGeometry({0, 2}, {2, 2}, {12.0, 12.0}, 12.0);
    QVERIFY2(RegionLayoutEngine::apply(&emojiGeometry, oneLineShape(24.0), emojiWord,
                                       region, settings, 1.0, &error), qPrintable(error));
    QVERIFY(!emojiGeometry.pieces.at(0).path.isEmpty());
    QVERIFY(!emojiGeometry.pieces.at(1).path.isEmpty());
    QCOMPARE(emojiGeometry.pieces.at(0).sourceClusterLength, 2);
    QCOMPARE(emojiGeometry.pieces.at(1).sourceClusterLength, 2);
    QVERIFY(emojiGeometry.pieces.at(1).path.boundingRect().top()
            > emojiGeometry.pieces.at(0).path.boundingRect().top());
}

void RegionTypographyTests::regionLayoutShapesRealBidiTextWithoutClusterLoss()
{
    TextObject object;
    object.sourceText = QString::fromUtf8("אבגדהוזחטיכלמנס 123, A");
    object.font.family = test::deterministicTestFamily();
    object.typography.fontSize = 28.0;
    QVERIFY(!object.font.family.isEmpty());

    TextEngine engine;
    const ShapedText shaped = engine.shape(object);
    QVERIFY2(shaped.error.isEmpty(), qPrintable(shaped.error));
    QVERIFY(!shaped.glyphs.isEmpty());
    VectorGeometry geometry = GlyphGeometryBuilder::build(shaped,
                                                          object.typography.fontSize);
    const VectorGeometry original = geometry;
    QHash<int, QVector<int>> originalPiecesByCluster;
    for (int index = 0; index < original.pieces.size(); ++index) {
        const GeometryPiece& piece = original.pieces.at(index);
        if (piece.sourceGlyphIndex >= 0) {
            originalPiecesByCluster[piece.sourceClusterStart].push_back(index);
        }
    }

    const TypographyRegion region = rectangleRegion(
        QStringLiteral("real-bidi-region"), QRectF(0.0, 0.0, 140.0, 500.0));
    const RegionTypographyProperties settings = regionSettings(region);
    QString error;
    QVERIFY2(RegionLayoutEngine::apply(&geometry, shaped, object.sourceText, region,
                                       settings, 1.0, &error), qPrintable(error));

    QHash<int, QVector<int>> visiblePiecesByCluster;
    QHash<int, QVector<int>> piecesByOutputLine;
    for (int index = 0; index < geometry.pieces.size(); ++index) {
        const GeometryPiece& piece = geometry.pieces.at(index);
        if (piece.sourceGlyphIndex < 0 || piece.path.isEmpty()) continue;
        QCOMPARE(piece.sourceClusterStart,
                 original.pieces.at(index).sourceClusterStart);
        QCOMPARE(piece.sourceClusterLength,
                 original.pieces.at(index).sourceClusterLength);
        QVERIFY(piece.sourceClusterStart >= 0);
        QVERIFY(piece.sourceClusterStart + piece.sourceClusterLength
                <= object.sourceText.size());
        visiblePiecesByCluster[piece.sourceClusterStart].push_back(index);
        piecesByOutputLine[piece.sourceLineIndex].push_back(index);
    }
    for (auto it = originalPiecesByCluster.cbegin(); it != originalPiecesByCluster.cend(); ++it) {
        const int visibleCount = visiblePiecesByCluster.value(it.key()).size();
        QVERIFY2(visibleCount == 0 || visibleCount == it.value().size(),
                 "Region wrapping must clip or preserve a complete shaping cluster.");
    }
    QVERIFY(piecesByOutputLine.size() >= 2);
    for (auto it = piecesByOutputLine.cbegin(); it != piecesByOutputLine.cend(); ++it) {
        QVector<int> byVisualX = it.value();
        std::sort(byVisualX.begin(), byVisualX.end(), [&geometry](int left, int right) {
            return geometry.pieces.at(left).path.boundingRect().left()
                < geometry.pieces.at(right).path.boundingRect().left();
        });
        for (int index = 1; index < byVisualX.size(); ++index) {
            QVERIFY(byVisualX.at(index - 1) < byVisualX.at(index));
        }
    }
}

void RegionTypographyTests::regionValidationRejectsAdversarialTopologyAndCancellation()
{
    const auto reject = [](const TypographyRegion& candidate, const char* reason) {
        QString error;
        const bool valid = candidate.validate(&error);
        QVERIFY2(!valid, reason);
        QVERIFY2(!error.isEmpty(), "Rejected region validation must explain the failure.");
    };

    TypographyRegion bowTie = rectangleRegion(QStringLiteral("bow-tie"),
                                               QRectF(0.0, 0.0, 100.0, 100.0));
    bowTie.outer = polygonContour(QStringLiteral("bow-tie-outer"),
                                  {{0.0, 0.0}, {100.0, 100.0},
                                   {0.0, 100.0}, {100.0, 0.0}});
    reject(bowTie, "A self-intersecting outer contour must be rejected.");

    TypographyRegion zeroArea = rectangleRegion(QStringLiteral("zero-area"),
                                                QRectF(0.0, 0.0, 100.0, 100.0));
    zeroArea.outer = polygonContour(QStringLiteral("zero-area-outer"),
                                    {{0.0, 0.0}, {50.0, 0.0}, {100.0, 0.0}});
    reject(zeroArea, "A zero-area outer contour must be rejected.");

    TypographyRegion touchingOuter = rectangleRegion(QStringLiteral("touching-outer"),
                                                      QRectF(0.0, 0.0, 100.0, 100.0));
    touchingOuter.holes.push_back(rectangleContour(QStringLiteral("touching-hole"),
                                                    QRectF(20.0, 0.0, 20.0, 20.0)));
    reject(touchingOuter, "A hole tangent to the outer boundary must be rejected.");

    TypographyRegion overlappingHoles = rectangleRegion(
        QStringLiteral("overlapping-holes"), QRectF(0.0, 0.0, 100.0, 100.0));
    overlappingHoles.holes.push_back(rectangleContour(QStringLiteral("overlap-a"),
                                                       QRectF(20.0, 20.0, 40.0, 40.0)));
    overlappingHoles.holes.push_back(rectangleContour(QStringLiteral("overlap-b"),
                                                       QRectF(50.0, 30.0, 30.0, 40.0)));
    reject(overlappingHoles, "Overlapping holes must be rejected.");

    TypographyRegion nestedHoles = rectangleRegion(QStringLiteral("nested-holes"),
                                                    QRectF(0.0, 0.0, 100.0, 100.0));
    nestedHoles.holes.push_back(rectangleContour(QStringLiteral("nested-a"),
                                                  QRectF(20.0, 20.0, 60.0, 60.0)));
    nestedHoles.holes.push_back(rectangleContour(QStringLiteral("nested-b"),
                                                  QRectF(35.0, 35.0, 30.0, 30.0)));
    reject(nestedHoles, "Nested holes must be rejected.");

    TypographyRegion sharedVertex = rectangleRegion(QStringLiteral("shared-vertex"),
                                                     QRectF(0.0, 0.0, 100.0, 100.0));
    sharedVertex.holes.push_back(rectangleContour(QStringLiteral("shared-a"),
                                                   QRectF(20.0, 20.0, 30.0, 30.0)));
    sharedVertex.holes.push_back(rectangleContour(QStringLiteral("shared-b"),
                                                   QRectF(50.0, 50.0, 30.0, 30.0)));
    reject(sharedVertex, "Holes sharing a vertex must be rejected.");

    TypographyRegion nearValidGap = rectangleRegion(QStringLiteral("near-valid-gap"),
                                                    QRectF(0.0, 0.0, 100.0, 100.0));
    nearValidGap.holes.push_back(rectangleContour(QStringLiteral("gap-a"),
                                                   QRectF(20.0, 20.0, 20.0, 20.0)));
    nearValidGap.holes.push_back(rectangleContour(QStringLiteral("gap-b"),
                                                   QRectF(40.001, 20.0, 20.0, 20.0)));
    QString error;
    QVERIFY2(nearValidGap.validate(&error), qPrintable(error));

    const TypographyRegion largeLegal = rectangleRegion(
        QStringLiteral("large-legal"), QRectF(-999999999.0, -999999999.0,
                                                1999999998.0, 1999999998.0));
    QVERIFY2(largeLegal.validate(&error), qPrintable(error));

    TypographyRegion nonFinite = rectangleRegion(QStringLiteral("non-finite"),
                                                  QRectF(0.0, 0.0, 100.0, 100.0));
    nonFinite.outer.nodes.front().anchor.setX(
        std::numeric_limits<qreal>::quiet_NaN());
    reject(nonFinite, "Non-finite contour coordinates must be rejected.");

    const TypographyRegion beforeCancellation = rectangleRegion(
        QStringLiteral("cancelled-region"), QRectF(0.0, 0.0, 100.0, 100.0));
    TypographyRegion cancellationCandidate = beforeCancellation;
    WorkControl work = WorkControl::unlimited();
    work.setCheckpointCallback([&work](qint64 consumed) {
        if (consumed >= 3) work.cancel();
    });
    error.clear();
    QVERIFY2(!cancellationCandidate.validate(&error, work), qPrintable(error));
    QCOMPARE(cancellationCandidate, beforeCancellation);
    QCOMPARE(work.status(), WorkControlStatus::Cancelled);
}

void RegionTypographyTests::regionSerializationMigratesAndRejectsDuplicateIdentity()
{
    TextObject object;
    object.id = QStringLiteral("region-object");
    object.sourceText = QStringLiteral("Persistent region");
    object.region = TypographyRegion::makeEllipse(QRectF(0.0, 0.0, 240.0, 120.0));
    object.region->id = QStringLiteral("persistent-region");
    PathGeometry persistentHole =
        TypographyRegion::makeEllipse(QRectF(80.0, 30.0, 80.0, 40.0)).outer;
    persistentHole.id = QStringLiteral("persistent-hole");
    object.region->holes.push_back(persistentHole);
    object.layoutMode = TypographyLayoutMode::Region;
    object.regionLayout = regionSettings(*object.region);
    object.regionLayout.paddingLeft = 7.0;
    object.regionLayout.paddingRight = 11.0;
    object.regionLayout.paddingTop = 13.0;
    object.regionLayout.paddingBottom = 17.0;
    object.regionLayout.horizontalAlignment = RegionHorizontalAlignment::Justified;
    object.regionLayout.verticalAlignment = RegionVerticalAlignment::Bottom;
    object.regionLayout.overflow = RegionOverflowMode::Clip;

    const QJsonObject serialized = ProjectSerializer::textObjectToJson(object);
    TextObject restored;
    QString error;
    QVERIFY2(ProjectSerializer::textObjectFromJson(serialized, &restored, &error),
             qPrintable(error));
    QCOMPARE(restored.layoutMode, TypographyLayoutMode::Region);
    QVERIFY(restored.region.has_value());
    QVERIFY(restored.region.value() == object.region.value());
    QVERIFY(restored.regionLayout == object.regionLayout);

    const auto assertRejectedAtomically = [&](const QJsonObject& malformed,
                                               const QString& expectedError) {
        TextObject target = object;
        QString malformedError;
        QVERIFY(!ProjectSerializer::textObjectFromJson(malformed, &target, &malformedError));
        QVERIFY2(malformedError.contains(expectedError, Qt::CaseInsensitive),
                 qPrintable(malformedError));
        QCOMPARE(ProjectSerializer::textObjectToJson(target), serialized);
    };

    QJsonObject wrongRegionReference = serialized;
    QJsonObject wrongRegionSettings = wrongRegionReference
        .value(QStringLiteral("regionLayout")).toObject();
    wrongRegionSettings.insert(QStringLiteral("regionId"), QStringLiteral("not-owned"));
    wrongRegionReference.insert(QStringLiteral("regionLayout"), wrongRegionSettings);
    assertRejectedAtomically(wrongRegionReference, QStringLiteral("different"));

    QJsonObject conflictingPath = serialized;
    conflictingPath.insert(QStringLiteral("path"), PathGeometry::makeDefault(300.0).toJson());
    QJsonObject conflictingPathSettings = conflictingPath
        .value(QStringLiteral("pathLayout")).toObject();
    conflictingPathSettings.insert(QStringLiteral("enabled"), true);
    conflictingPathSettings.insert(QStringLiteral("pathId"),
                                   conflictingPath.value(QStringLiteral("path"))
                                       .toObject().value(QStringLiteral("id")));
    conflictingPath.insert(QStringLiteral("pathLayout"), conflictingPathSettings);
    assertRejectedAtomically(conflictingPath, QStringLiteral("conflicting"));

    QJsonObject missingRegionLayout = serialized;
    missingRegionLayout.remove(QStringLiteral("regionLayout"));
    assertRejectedAtomically(missingRegionLayout, QStringLiteral("regionLayout"));

    ProjectResourceLimits limited = ProjectSerializer::resourceLimits();
    limited.maximumRegionNodesPerContour = 3;
    QString budgetError;
    QVERIFY(!ProjectSerializer::validateResourceBudget(
        QJsonDocument(QJsonArray{serialized}), limited, &budgetError));
    QVERIFY(budgetError.contains(QStringLiteral("region nodes per contour")));

    TextObject pathObject;
    pathObject.sourceText = QStringLiteral("v7 path");
    pathObject.path = PathGeometry::makeDefault(300.0);
    pathObject.pathLayout.enabled = true;
    pathObject.pathLayout.pathId = pathObject.path->id;
    QJsonObject legacy = ProjectSerializer::textObjectToJson(pathObject);
    legacy.remove(QStringLiteral("layoutMode"));
    legacy.remove(QStringLiteral("region"));
    legacy.remove(QStringLiteral("regionLayout"));
    TextObject migrated;
    QVERIFY2(ProjectSerializer::textObjectFromJson(legacy, &migrated, &error),
             qPrintable(error));
    QCOMPARE(activeTypographyLayoutMode(migrated), TypographyLayoutMode::Path);
    QVERIFY(migrated.pathLayout == pathObject.pathLayout);

    Document document;
    TextObject& first = document.primaryTextObject();
    first.sourceText = object.sourceText;
    first.region = object.region;
    first.layoutMode = object.layoutMode;
    first.regionLayout = object.regionLayout;
    auto duplicate = std::make_unique<TextObject>(first);
    duplicate->id = QStringLiteral("different-object");
    document.currentPage()->layers.front()->objects.push_back(std::move(duplicate));
    const QJsonDocument duplicateJson = ProjectSerializer::toJson(document);
    Document rejected;
    QVERIFY(!ProjectSerializer::fromJson(duplicateJson, &rejected, &error));
    QVERIFY(error.contains(QStringLiteral("duplicate region"), Qt::CaseInsensitive));

    const TypographyRegion fresh = object.region->duplicatedFresh();
    QVERIFY(fresh.id != object.region->id);
    QVERIFY(fresh.outer.id != object.region->outer.id);
    QVERIFY(fresh.holes.front().id != object.region->holes.front().id);
    QVERIFY(fresh.outer.nodes.front().id != object.region->outer.nodes.front().id);

    TypographyRegion openRegion = rectangleRegion(QStringLiteral("open-region"),
                                                  QRectF(0.0, 0.0, 100.0, 100.0));
    openRegion.outer.closed = false;
    QVERIFY(!openRegion.validate(&budgetError));

    TypographyRegion outsideHole = rectangleRegion(QStringLiteral("outside-hole-region"),
                                                   QRectF(0.0, 0.0, 100.0, 100.0));
    outsideHole.holes.push_back(rectangleContour(QStringLiteral("outside-hole"),
                                                 QRectF(80.0, 80.0, 40.0, 40.0)));
    QVERIFY(!outsideHole.validate(&budgetError));

    TypographyRegion duplicateNodeRegion = rectangleRegion(
        QStringLiteral("duplicate-node-region"), QRectF(0.0, 0.0, 100.0, 100.0));
    PathGeometry duplicateNodeHole = rectangleContour(
        QStringLiteral("duplicate-node-hole"), QRectF(20.0, 20.0, 30.0, 30.0));
    duplicateNodeHole.nodes.front().id = duplicateNodeRegion.outer.nodes.front().id;
    duplicateNodeRegion.holes.push_back(duplicateNodeHole);
    QVERIFY(!duplicateNodeRegion.validate(&budgetError));

    QJsonObject badSettings = object.regionLayout.toJson();
    badSettings.insert(QStringLiteral("horizontalAlignment"), QStringLiteral("diagonal"));
    RegionTypographyProperties parsedSettings;
    QVERIFY(!RegionTypographyProperties::fromJson(badSettings, &parsedSettings, &budgetError));
    badSettings.insert(QStringLiteral("paddingLeft"),
                       std::numeric_limits<double>::quiet_NaN());
    QVERIFY(!RegionTypographyProperties::fromJson(badSettings, &parsedSettings, &budgetError));

    ProjectResourceLimits tinyWork = ProjectSerializer::resourceLimits();
    tinyWork.maximumRegionWork = 1;
    QVERIFY(!ProjectSerializer::validateResourceBudget(
        QJsonDocument(QJsonArray{serialized}), tinyWork, &budgetError));
}

void RegionTypographyTests::controllerRegionEditsUndoAndFreshenDuplicateIdentity()
{
    EditorController controller;
    const QString objectId = controller.createTextObject(QPointF(80.0, 80.0),
                                                         QStringLiteral("controller region"));
    QVERIFY(!objectId.isEmpty());
    controller.setTypographyLayoutMode(TypographyLayoutMode::Region);
    TextObject* object = controller.document().objectById(objectId);
    QVERIFY(object);
    QVERIFY(object->region.has_value());
    QCOMPARE(activeTypographyLayoutMode(*object), TypographyLayoutMode::Region);
    controller.createRegionCustom();
    object = controller.document().objectById(objectId);
    QVERIFY(object && object->region.has_value());
    QVERIFY(object->region->outer.nodes.size() > 4);

    const QString beforePadding = test::semanticFingerprint(controller.document());
    controller.beginRegionPaddingGesture(RegionPaddingSide::Left);
    controller.setRegionPadding(RegionPaddingSide::Left, 33.0);
    controller.endRegionPaddingGesture();
    QVERIFY(std::abs(controller.document().objectById(objectId)->regionLayout.paddingLeft - 33.0)
            < 1.0e-4);
    controller.undoStack()->undo();
    QCOMPARE(test::semanticFingerprint(controller.document()), beforePadding);
    controller.undoStack()->redo();
    QVERIFY(std::abs(controller.document().objectById(objectId)->regionLayout.paddingLeft - 33.0)
            < 1.0e-4);

    controller.addRegionHole();
    object = controller.document().objectById(objectId);
    QVERIFY(object && object->region.has_value());
    QCOMPARE(object->region->holes.size(), 1);
    controller.editRegionHoleContour();
    QVERIFY(controller.activeRegionContour());
    QCOMPARE(controller.activeRegionContour()->id, object->region->holes.front().id);
    controller.editRegionOuterContour();
    QCOMPARE(controller.activeRegionContour()->id, object->region->outer.id);
    controller.duplicateSelectedObjects();
    const Layer* layer = controller.document().activeLayer();
    QVERIFY(layer);
    QCOMPARE(layer->objects.size(), size_t(2));
    QVERIFY(layer->objects.at(0)->region.has_value());
    QVERIFY(layer->objects.at(1)->region.has_value());
    QVERIFY(layer->objects.at(0)->region->id != layer->objects.at(1)->region->id);
    QVERIFY(layer->objects.at(0)->region->outer.id != layer->objects.at(1)->region->outer.id);
}

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    RegionTypographyTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "region_typography_tests.moc"
