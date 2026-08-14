#include "core/document/document.h"
#include "core/evaluation/work_control.h"
#include "core/region/region_layout.h"
#include "core/serialization/project_serializer.h"
#include "tests/support/state_fingerprint.h"
#include "ui/editor_controller.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

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
    void regionLayoutUsesLogicalJustificationAndCancellation();
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

void RegionTypographyTests::regionSerializationMigratesAndRejectsDuplicateIdentity()
{
    TextObject object;
    object.id = QStringLiteral("region-object");
    object.sourceText = QStringLiteral("Persistent region");
    object.region = rectangleRegion(QStringLiteral("persistent-region"),
                                    QRectF(0.0, 0.0, 240.0, 120.0));
    object.region->holes.push_back(rectangleContour(QStringLiteral("persistent-hole"),
                                                    QRectF(80.0, 30.0, 80.0, 40.0)));
    object.layoutMode = TypographyLayoutMode::Region;
    object.regionLayout = regionSettings(*object.region);
    object.regionLayout.paddingLeft = 7.0;
    object.regionLayout.horizontalAlignment = RegionHorizontalAlignment::Center;
    object.regionLayout.verticalAlignment = RegionVerticalAlignment::Bottom;

    const QJsonObject serialized = ProjectSerializer::textObjectToJson(object);
    TextObject restored;
    QString error;
    QVERIFY2(ProjectSerializer::textObjectFromJson(serialized, &restored, &error),
             qPrintable(error));
    QCOMPARE(restored.layoutMode, TypographyLayoutMode::Region);
    QVERIFY(restored.region.has_value());
    QVERIFY(restored.region.value() == object.region.value());
    QVERIFY(restored.regionLayout == object.regionLayout);

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
