#include "core/document/document.h"
#include "core/deformation/contour_sampler.h"
#include "core/deformation/manual_deformation.h"
#include "core/evaluation/work_control.h"
#include "core/effects/glyph_jitter_effect.h"
#include "core/effects/effect_mask_distance.h"
#include "core/effects/effect_registry.h"
#include "core/effects/text_range_rebaser.h"
#include "core/effects/procedural_effect.h"
#include "core/effects/stretch_effect.h"
#include "core/effects/geometry_warp_effect.h"
#include "core/effects/trail_effect.h"
#include "core/effects/wave_effect.h"
#include "core/export/svg_exporter.h"
#include "core/export/export_payload_builder.h"
#include "core/presets/preset.h"
#include "core/presets/preset_manager.h"
#include "core/presets/preset_catalog.h"
#include "core/path/path_layout.h"
#include "core/serialization/project_serializer.h"
#include "core/scene/scene_evaluator.h"
#include "core/scene/object_frame.h"
#include "core/text/text_engine.h"
#include "core/undo/document_commands.h"
#include "ui/deformation_tool_state.h"
#include "ui/editor_controller.h"
#include "ui/selection_model.h"
#include "ui/shortcut_manager.h"
#include "tests/support/semantic_geometry.h"
#include "tests/support/state_fingerprint.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineF>
#include <QPainterPath>
#include <QPolygonF>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTransform>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace vt;

namespace {

QString availableFamily()
{
    return QFontDatabase::families().value(0);
}

QString cyrillicFamily()
{
    return QFontDatabase::families(QFontDatabase::Cyrillic).value(0);
}

TextObject configuredText(const QString& text = QStringLiteral("Hello РњРёСЂ"))
{
    TextObject object;
    object.sourceText = text;
    object.font.family = availableFamily();
    object.font.styleName = QFontDatabase::styles(object.font.family).value(0);
    object.font.weight = static_cast<int>(QFont::Normal);
    object.typography.fontSize = 80.0;
    object.typography.trackingEm = 0.025;
    object.fill = QColor(40, 70, 120, 220);
    return object;
}

VectorGeometry baseGeometry(const TextObject& object)
{
    TextEngine engine;
    const ShapedText shaped = engine.shape(object);
    return GlyphGeometryBuilder::build(shaped, object.typography.fontSize);
}

QByteArray geometrySignature(const VectorGeometry& geometry)
{
    QByteArray signature;
    for (const GeometryPiece& piece : geometry.pieces) {
        signature.append(QByteArray::number(piece.sourceGlyphIndex));
        signature.append('|');
        signature.append(QByteArray::number(piece.anchor.x(), 'f', 8));
        signature.append(',');
        signature.append(QByteArray::number(piece.anchor.y(), 'f', 8));
        signature.append('|');
        for (int index = 0; index < piece.path.elementCount(); ++index) {
            const auto element = piece.path.elementAt(index);
            signature.append(QByteArray::number(static_cast<int>(element.type)));
            signature.append(':');
            signature.append(QByteArray::number(element.x, 'f', 8));
            signature.append(',');
            signature.append(QByteArray::number(element.y, 'f', 8));
            signature.append(';');
        }
    }
    return signature;
}

double effectParameterValue(const Effect& effect, const QString& id)
{
    const QVector<EffectParameter> parameters = effect.parameterDefinitions();
    for (const EffectParameter& parameter : parameters) {
        if (parameter.id == id) {
            return parameter.value;
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

VectorGeometry rectangleGeometry()
{
    VectorGeometry geometry;
    GeometryPiece first;
    first.path.addRect(QRectF(0.0, 0.0, 20.0, 20.0));
    first.anchor = QPointF(10.0, 10.0);
    first.originalAnchor = first.anchor;
    first.sourceGlyphIndex = 0;
    geometry.pieces.push_back(first);

    GeometryPiece second;
    second.path.addRect(QRectF(40.0, 0.0, 20.0, 20.0));
    second.anchor = QPointF(50.0, 10.0);
    second.originalAnchor = second.anchor;
    second.sourceGlyphIndex = 1;
    geometry.pieces.push_back(second);
    geometry.setReferenceBounds(QRectF(0.0, 0.0, 60.0, 20.0));
    geometry.recomputeBounds();
    return geometry;
}

DeformationStroke pushStroke(BrushTarget target = BrushTarget::Shape)
{
    DeformationStroke stroke;
    stroke.mode = BrushMode::Push;
    stroke.target = target;
    stroke.radius = 32.0;
    stroke.strength = 0.8;
    stroke.hardness = 0.5;
    stroke.samples = {
        {QPointF(2.0, 10.0), QPointF(), 1.0},
        {QPointF(12.0, 10.0), QPointF(10.0, 0.0), 1.0},
        {QPointF(22.0, 10.0), QPointF(10.0, 0.0), 1.0},
    };
    return stroke;
}

QPointF firstPathElement(const QPainterPath& path)
{
    if (path.elementCount() == 0) {
        return {};
    }
    const QPainterPath::Element element = path.elementAt(0);
    return QPointF(element.x, element.y);
}

} // namespace

class CoreTests final : public QObject {
    Q_OBJECT

private slots:
    void projectSerializationRoundTrip();
    void pathGeometryRoundTripAndReverseIsInvolutive();
    void pathArcLengthUsesBoundedDistanceQueries();
    void pathArcLengthMatchesIndependentDenseOracle();
    void pathClosedSeamTransformAndSideSemantics();
    void pathSubdivisionAndDegenerateGeometryStayBounded();
    void pathLayoutClipsOpenOverflowWithoutEndpointPileup();
    void pathLayoutPreservesClustersThroughEffects();
    void postPathEffectsFollowPathProgressInsteadOfSourceAnchors();
    void complexShapingPlacementHasFiniteAdvancesAndClusters();
    void pathPipelineAppliesEffectsAndDeformationToFinalGeometry();
    void pathLayoutHonorsCancellationBudget();
    void pathCancellationThresholdsDoNotPoisonPathStageCaches();
    void pathControllerDuplicateAndStaleGestureKeepIdentitySafe();
    void pathSerializationRejectsCorruptionAndBudgets();
    void pathUndoTargetsExplicitObjectAndRestoresFingerprint();
    void projectV1TrackingMigrates();
    void projectV2DeformationDefaults();
    void presetSerializationRoundTrip();
    void unicodePresetStorageIsCollisionSafe();
    void deterministicJitter();
    void effectOrderingIsDeterministic();
    void effectMasksAttenuateGeometry();
    void textReplacementPreservesEffects();
    void presetApplicationClonesEffects();
    void svgExportContainsPaths();
    void portableExportPayloadScopesAndOpacity();
    void payloadSvgPreservesRecordsAndWinding();
    void exportEligibilityDoesNotEvaluateEmptyText();
    void cyrillicTextProducesGeometry();
    void glyphFallbackIsReportedWhenAvailable();
    void missingFontStatesAreDistinguished();
    void trackingScalesWithFontSize();
    void trackingUsesTrueEmDistance();
    void decorationsFollowTrackedLineExtents();
    void deformationSerializationRoundTrip();
    void deformationResamplingIsBoundedAndDeterministic();
    void pushStrokeIsDeterministic();
    void glyphPushMovesRigidUnits();
    void shapePushBendsContours();
    void inflateMovesPointsOutward();
    void pinchMovesPointsInward();
    void pullMovesPointsTowardCenter();
    void smoothBrushReducesLocalIrregularity();
    void deformationStrengthAndToggleAreNondestructive();
    void deformationPreservesMultipleContours();
    void openContourWithSeveralPointsStaysOpen();
    void closedContourStaysClosed();
    void mixedContourClosureSurvivesReconstruction();
    void shapeDeformationPreservesOpenContours();
    void cyrillicShapeDeformationProducesGeometry();
    void selectToolIsNotADeformationStroke();
    void smoothToolForcesShapeAndRestoresTarget();
    void smoothGlyphTargetIsNormalized();
    void controllerUndoRedoAndMerge();
    void controllerEffectCommandsAreGranular();
    void controllerDeformationCommandsAreGranular();
    void controllerCleanStateFollowsUndoStack();
    void documentHierarchyHasStableIds();
    void legacyFlatProjectMigratesToPageAndLayer();
    void multilineShapingPreservesLinesAndClusters();
    void logicalClusterSpansUseWholeLineContext();
    void mixedUtf16ShapingUsesGlobalClusterSpans();
    void mixedUtf16ClustersSurviveEffectsPersistenceAndExport();
    void scopedEffectsOnlyTouchSelectedClusters();
    void proceduralEffectsAreDeterministicAndAvailable();
    void selectionModelSupportsSingleAndRangeSelection();
    void controllerSceneCommandsMoveDuplicateAndDeleteObjects();
    void controllerUndoTargetsStableObject();
    void controllerPageAndLayerCommandsAreUndoable();
    void controllerMoveObjectBetweenLayersIsUndoable();
    void controllerLockedLayerObjectsAreNotEditable();
    void controllerMaskUsesObjectLocalScale();
    void pageReorderIsUndoableAndSerializable();
    void shapingCacheKeyIgnoresFillButTracksLayoutInputs();
    void shortcutManagerDetectsConflictsAndPersists();
    void asyncEvaluationPublishesLatestGeneration();
    void objectFrameRoundTripsPointsAndVectors();
    void deformationBrushModesProduceDistinctGeometry();
    void fontTraitsInvalidateSceneShapingAndProduceVectorDecorations();
    void masterStrengthSupportsAmplificationAndRoundTrip();
    void geometrySourceMetadataStaysImmutableUnderEffectsAndTransforms();
    void missingUndoTargetDoesNotRedirectToAnotherObject();
    void ambiguousLegacyDeformationStrokeIsSkipped();
    void capturedMoveIdsDoNotFollowSelectionChanges();
    void fontDescriptorTraitsAndExactStylesRemainConsistent();
    void transformScaleDomainAndPivotRoundTrip();
    void maskUsesPieceGeometryWhenAnchorIsOutsideBrush();
    void contourMaskDistanceRejectsHolesAndConcavities();
    void contourMaskDistanceHandlesAdversarialGeometryAndCancellation();
    void workControlHasExactSharedTerminalBoundaries();
    void evaluationCancellationDoesNotPoisonWorkerCaches();
    void fontCacheEpochInvalidatesWorkerShapingKeys();
    void effectRegistryDescriptorsAgreeWithFactories();
    void builtInPresetCatalogParses();
    void effectStackStrengthZeroIsIdentity();
    void textRangeRebasingUsesUtf16Offsets();
};

void CoreTests::projectSerializationRoundTrip()
{
    Document original;
    original.title = QStringLiteral("Round trip");
    TextObject& object = original.primaryTextObject();
    object = configuredText();
    object.path = PathGeometry::makeDefault(420.0, 0.0);
    object.pathLayout.enabled = true;
    object.pathLayout.pathId = object.path->id;
    object.pathLayout.startOffset = 12.5;
    object.pathLayout.baselineOffset = -4.0;
    object.pathLayout.reverse = true;
    object.pathLayout.flip = true;
    original.activeObjectId = object.id;

    auto wave = std::make_unique<WaveEffect>();
    wave->amplitude = 0.21;
    wave->frequency = 2.5;
    wave->phase = -0.25;
    EffectMaskStroke mask;
    mask.points = {QPointF(12.0, 18.0), QPointF(30.0, 18.0)};
    mask.radius = 24.0;
    mask.opacity = 0.8;
    mask.hardness = 0.65;
    mask.restore = true;
    wave->maskStrokes.push_back(mask);
    object.effects.append(std::move(wave));
    auto jitter = std::make_unique<GlyphJitterEffect>();
    jitter->amount = 0.04;
    jitter->seed = 987654321U;
    jitter->enabled = false;
    object.effects.append(std::move(jitter));

    const QJsonObject typography = ProjectSerializer::toJson(original)
                                       .object()
                                       .value(QStringLiteral("objects"))
                                       .toArray()
                                       .at(0)
                                       .toObject()
                                       .value(QStringLiteral("typography"))
                                       .toObject();
    QVERIFY(typography.contains(QStringLiteral("trackingEm")));
    QCOMPARE(typography.value(QStringLiteral("trackingUnit")).toString(), QStringLiteral("em"));
    QVERIFY(!typography.contains(QStringLiteral("tracking")));

    QString error;
    Document restored;
    QVERIFY(ProjectSerializer::fromJson(ProjectSerializer::toJson(original), &restored, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(restored.title, original.title);
    QCOMPARE(restored.primaryTextObject().sourceText, object.sourceText);
    QVERIFY(restored.primaryTextObject().font == object.font);
    QCOMPARE(restored.primaryTextObject().typography.fontSize, object.typography.fontSize);
    QCOMPARE(restored.primaryTextObject().typography.trackingEm, object.typography.trackingEm);
    QVERIFY(restored.primaryTextObject().path.has_value());
    QVERIFY(restored.primaryTextObject().path.value() == object.path.value());
    QVERIFY(restored.primaryTextObject().pathLayout == object.pathLayout);
    QCOMPARE(restored.primaryTextObject().fill, object.fill);
    QCOMPARE(restored.primaryTextObject().effects.size(), 2);
    QCOMPARE(restored.primaryTextObject().effects.at(0)->typeId(), QStringLiteral("wave"));
    QCOMPARE(restored.primaryTextObject().effects.at(0)->maskStrokes.size(), 1);
    QCOMPARE(restored.primaryTextObject().effects.at(0)->maskStrokes.front().restore, true);
    QCOMPARE(restored.primaryTextObject().effects.at(1)->enabled, false);
    const auto* restoredJitter = dynamic_cast<const GlyphJitterEffect*>(restored.primaryTextObject().effects.at(1));
    QVERIFY(restoredJitter);
    QCOMPARE(restoredJitter->seed, 987654321U);
}

void CoreTests::pathGeometryRoundTripAndReverseIsInvolutive()
{
    PathGeometry path;
    path.id = QStringLiteral("path-test");
    path.closed = true;
    PathNode first;
    first.id = QStringLiteral("node-a");
    first.anchor = QPointF(0.0, 0.0);
    first.hasOutgoingHandle = true;
    first.outgoingHandle = QPointF(25.0, 60.0);
    PathNode second;
    second.id = QStringLiteral("node-b");
    second.anchor = QPointF(100.0, 0.0);
    second.hasIncomingHandle = true;
    second.incomingHandle = QPointF(75.0, 60.0);
    PathNode third;
    third.id = QStringLiteral("node-c");
    third.anchor = QPointF(100.0, 100.0);
    path.nodes = {first, second, third};
    QVERIFY(path.validate());

    PathGeometry restored;
    QString error;
    QVERIFY2(PathGeometry::fromJson(path.toJson(), &restored, &error), qPrintable(error));
    QVERIFY(restored == path);
    restored.reverseDirection();
    QVERIFY(restored != path);
    restored.reverseDirection();
    QVERIFY(restored == path);
}

void CoreTests::pathArcLengthUsesBoundedDistanceQueries()
{
    const PathGeometry line = PathGeometry::makeDefault(100.0);
    const std::optional<PathArcLengthTable> lineTable = PathArcLengthTable::build(line);
    QVERIFY(lineTable.has_value());
    QVERIFY(std::abs(lineTable->totalLength() - 100.0) < 1.0e-6);
    const PathPosition quarter = lineTable->positionAt(25.0, false);
    QVERIFY(quarter.valid);
    QVERIFY(std::abs(quarter.point.x() - 25.0) < 1.0e-6);
    QVERIFY(std::abs(quarter.point.y()) < 1.0e-6);
    QVERIFY(std::abs(quarter.tangent.x() - 1.0) < 1.0e-6);
    QVERIFY(std::abs(quarter.tangent.y()) < 1.0e-6);
    QVERIFY(!lineTable->positionAt(101.0, false).valid);
    const PathPosition wrapped = lineTable->positionAt(-25.0, true);
    QVERIFY(wrapped.valid);
    QVERIFY(std::abs(wrapped.point.x() - 75.0) < 1.0e-6);

    PathGeometry cubic;
    cubic.id = QStringLiteral("cubic-test");
    PathNode start;
    start.id = QStringLiteral("cubic-start");
    start.anchor = QPointF(0.0, 0.0);
    start.hasOutgoingHandle = true;
    start.outgoingHandle = QPointF(0.0, 100.0);
    PathNode end;
    end.id = QStringLiteral("cubic-end");
    end.anchor = QPointF(100.0, 0.0);
    end.hasIncomingHandle = true;
    end.incomingHandle = QPointF(100.0, 100.0);
    cubic.nodes = {start, end};
    const std::optional<PathArcLengthTable> cubicTable = PathArcLengthTable::build(cubic);
    QVERIFY(cubicTable.has_value());
    QVERIFY(cubicTable->totalLength() > 100.0);
    const PathPosition endPosition = cubicTable->positionAt(cubicTable->totalLength(), false);
    QVERIFY(endPosition.valid);
    QVERIFY(QLineF(endPosition.point, end.anchor).length() < 1.0e-5);
}

void CoreTests::pathArcLengthMatchesIndependentDenseOracle()
{
    PathGeometry path;
    path.id = QStringLiteral("dense-oracle");
    PathNode start;
    start.id = QStringLiteral("dense-start");
    start.anchor = QPointF(0.0, 0.0);
    start.hasOutgoingHandle = true;
    start.outgoingHandle = QPointF(10.0, 160.0);
    PathNode end;
    end.id = QStringLiteral("dense-end");
    end.anchor = QPointF(220.0, 0.0);
    end.hasIncomingHandle = true;
    end.incomingHandle = QPointF(210.0, -160.0);
    path.nodes = {start, end};

    const std::optional<PathArcLengthTable> table = PathArcLengthTable::build(path);
    QVERIFY(table.has_value());

    // This is intentionally a dense fixed-parameter polyline oracle. It does
    // not reuse the production subdivision or distance inversion code.
    const QPainterPath painterPath = path.toPainterPath();
    constexpr int DenseSamples = 20000;
    QVector<QPointF> points;
    QVector<qreal> distances;
    points.reserve(DenseSamples + 1);
    distances.reserve(DenseSamples + 1);
    points.push_back(painterPath.pointAtPercent(0.0));
    distances.push_back(0.0);
    for (int index = 1; index <= DenseSamples; ++index) {
        const QPointF point = painterPath.pointAtPercent(
            static_cast<qreal>(index) / DenseSamples);
        points.push_back(point);
        distances.push_back(distances.constLast() + QLineF(points.at(index - 1), point).length());
    }
    const qreal denseLength = distances.constLast();
    QVERIFY(denseLength > 0.0);
    QVERIFY(std::abs(table->totalLength() - denseLength) < 0.8);

    for (const qreal fraction : {0.15, 0.35, 0.6, 0.85}) {
        const qreal requested = table->totalLength() * fraction;
        const PathPosition actual = table->positionAt(requested, false);
        QVERIFY(actual.valid);
        const qreal denseDistance = denseLength * fraction;
        const auto upper = std::lower_bound(distances.cbegin(), distances.cend(), denseDistance);
        const int rightIndex = qBound(1, static_cast<int>(upper - distances.cbegin()),
                                      distances.size() - 1);
        const qreal leftDistance = distances.at(rightIndex - 1);
        const qreal step = distances.at(rightIndex) - leftDistance;
        const qreal localFraction = step > 0.0
            ? (denseDistance - leftDistance) / step : 0.0;
        const QPointF expected = points.at(rightIndex - 1) * (1.0 - localFraction)
            + points.at(rightIndex) * localFraction;
        QVERIFY2(QLineF(actual.point, expected).length() < 1.0,
                 qPrintable(QStringLiteral("arc-distance error at fraction %1")
                                .arg(fraction)));
    }
}

void CoreTests::pathClosedSeamTransformAndSideSemantics()
{
    PathGeometry closed = PathGeometry::makeDefault(100.0);
    closed.closed = true;
    const auto table = PathArcLengthTable::build(closed);
    QVERIFY(table.has_value());
    QVERIFY(table->positionAt(0.0, true).valid);
    const PathPosition wrappedForward = table->positionAt(110.0, true);
    const PathPosition wrappedBackward = table->positionAt(-10.0, true);
    QVERIFY(wrappedForward.valid);
    QVERIFY(wrappedBackward.valid);
    // A closed two-node path contains the forward segment and its explicit
    // closing segment, so its perimeter is 200 rather than 100.
    QVERIFY(std::abs(wrappedForward.point.x() - 90.0) < 1.0e-6);
    QVERIFY(std::abs(wrappedBackward.point.x() - 10.0) < 1.0e-6);…30062 tokens truncated…ltiline = base;
    multiline.sourceText = QStringLiteral("AB\nWXYZ");
    multiline.typography.trackingEm = 0.08;
    engine.clearCache();
    const ShapedText trackedMultiline = engine.shape(multiline);
    QVERIFY2(trackedMultiline.error.isEmpty(), qPrintable(trackedMultiline.error));
    TextObject multilineUntracked = multiline;
    multilineUntracked.typography.trackingEm = 0.0;
    engine.clearCache();
    const ShapedText untrackedMultiline = engine.shape(multilineUntracked);
    QCOMPARE(trackedMultiline.lineBounds.size(), 2);
    QCOMPARE(untrackedMultiline.lineBounds.size(), 2);
    for (int line = 0; line < trackedMultiline.lineBounds.size(); ++line) {
        const int lineGlyphs = std::count_if(trackedMultiline.glyphs.cbegin(), trackedMultiline.glyphs.cend(),
                                             [line](const ShapedGlyph& glyph) {
                                                 return glyph.lineIndex == line;
                                             });
        const qreal expectedWidth = untrackedMultiline.lineBounds.at(line).width()
            + multiline.typography.trackingEm * trackedMultiline.resolvedEmSize * qMax(0, lineGlyphs - 1);
        QVERIFY(std::abs(trackedMultiline.lineBounds.at(line).width() - expectedWidth) < 0.01);
    }
    QVERIFY(trackedMultiline.lineBounds.at(0).width() != trackedMultiline.lineBounds.at(1).width());

    const VectorGeometry decorated = GlyphGeometryBuilder::build(trackedMultiline,
                                                                   multiline.typography.fontSize,
                                                                   true, true);
    for (int line = 0; line < trackedMultiline.lineBounds.size(); ++line) {
        int decorationCount = 0;
        for (const GeometryPiece& piece : decorated.pieces) {
            if (piece.sourceGlyphIndex == -1 && piece.sourceLineIndex == line) {
                ++decorationCount;
                QVERIFY(std::abs(piece.path.boundingRect().width()
                                 - trackedMultiline.lineBounds.at(line).width()) < 0.01);
            }
        }
        QCOMPARE(decorationCount, 2);
    }
    Document document;
    document.primaryTextObject() = multiline;
    SvgExporter exporter;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString error;
    const QString filePath = directory.filePath(QStringLiteral("tracked-decorations.svg"));
    QVERIFY2(exporter.exportGeometry(document, decorated, filePath, &error), qPrintable(error));
    QFile svg(filePath);
    QVERIFY(svg.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(svg.readAll().contains("<path"));
}

void CoreTests::capturedMoveIdsDoNotFollowSelectionChanges()
{
    EditorController controller;
    const QString first = controller.createTextObject(QPointF(10.0, 10.0), QStringLiteral("A"));
    const QString second = controller.createTextObject(QPointF(50.0, 10.0), QStringLiteral("B"));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    controller.selectObject(first);
    // This models a Layers-panel selection change between mouse press and release.
    controller.selectObject(second);
    const QPointF firstBefore = controller.document().objectById(first)->transform.position;
    const QPointF secondBefore = controller.document().objectById(second)->transform.position;
    controller.moveObjects({first}, QPointF(23.0, -7.0));
    QCOMPARE(controller.document().objectById(first)->transform.position, firstBefore + QPointF(23.0, -7.0));
    QCOMPARE(controller.document().objectById(second)->transform.position, secondBefore);
    controller.undoStack()->undo();
    QCOMPARE(controller.document().objectById(first)->transform.position, firstBefore);
    QCOMPARE(controller.document().objectById(second)->transform.position, secondBefore);
}

void CoreTests::fontDescriptorTraitsAndExactStylesRemainConsistent()
{
    const QStringList families = QFontDatabase::families();
    if (families.isEmpty()) {
        QSKIP("No installed font family is available in this test environment.");
    }
    EditorController controller;
    const QString id = controller.createTextObject(QPointF(), QStringLiteral("Font"));
    QVERIFY(!id.isEmpty());
    const QString family = families.front();
    controller.setFontFamily(family);
    const QString style = QFontDatabase::styles(family).value(0);
    QVERIFY(!style.isEmpty());
    controller.setFontStyle(style);
    const TextObject* object = controller.activeObject();
    QVERIFY(object);
    QCOMPARE(object->font.styleName, style);
    const QFontInfo exactInfo(object->font.toQFont(object->typography.fontSize));
    QCOMPARE(exactInfo.family(), QFontInfo(QFontDatabase::font(family, style, 12)).family());
    controller.setFontWeight(static_cast<int>(QFont::Bold));
    controller.setFontItalic(true);
    object = controller.activeObject();
    QVERIFY(object->font.styleName.isEmpty());
    const QFontInfo traitInfo(object->font.toQFont(object->typography.fontSize));
    QVERIFY(traitInfo.bold() || traitInfo.weight() >= QFont::Bold);
    QVERIFY(traitInfo.italic() || object->font.italic);
    controller.undoStack()->undo();
    controller.undoStack()->undo();
    QCOMPARE(controller.activeObject()->font.styleName, style);
}

void CoreTests::transformScaleDomainAndPivotRoundTrip()
{
    ObjectTransform transform;
    transform.scale = QPointF(0.0, -1.0e-12);
    transform.pivotLocal = QPointF(31.0, -14.0);
    transform.hasPivot = true;
    const ObjectTransform restored = ObjectTransform::fromJson(transform.toJson());
    QVERIFY(std::abs(restored.scale.x()) >= ObjectTransform::MinimumScale);
    QVERIFY(std::abs(restored.scale.y()) >= ObjectTransform::MinimumScale);
    QCOMPARE(restored.pivotLocal, transform.pivotLocal);
    QVERIFY(restored.hasPivot);
    const ObjectFrame frame = ObjectFrame::fromTransform(restored, QRectF(0, 0, 400, 200));
    QCOMPARE(frame.pivotLocal, transform.pivotLocal);
}

void CoreTests::maskUsesPieceGeometryWhenAnchorIsOutsideBrush()
{
    VectorGeometry geometry = rectangleGeometry();
    WaveEffect effect;
    effect.amplitude = 0.5;
    effect.frequency = 1.0;
    effect.phase = 0.25;
    EffectMaskStroke stroke;
    stroke.points = {QPointF(19.0, 10.0)}; // intersects the first path, not its anchor at x=10
    stroke.radius = 3.0;
    stroke.opacity = 1.0;
    effect.maskStrokes.push_back(stroke);
    // A painted stroke normally removes influence.  Inverting it turns this
    // into a positive assertion: only an intersecting contour receives Wave.
    effect.maskInverted = true;
    EffectStack stack;
    stack.append(effect.clone());
    stack.apply(geometry);
    QVERIFY(geometry.pieces.at(0).anchor.y() > 10.0);
}

void CoreTests::contourMaskDistanceRejectsHolesAndConcavities()
{
    QPainterPath donut;
    donut.setFillRule(Qt::OddEvenFill);
    donut.addRect(QRectF(0.0, 0.0, 100.0, 100.0));
    donut.addRect(QRectF(20.0, 20.0, 60.0, 60.0));

    // This brush stays wholly inside the counter. Its AABB overlaps the glyph,
    // but it remains far from both actual contours.
    const QVector<QPointF> throughHole = {QPointF(40.0, 50.0), QPointF(60.0, 50.0)};
    QCOMPARE(EffectMaskDistance::strokeInfluence(
                 donut, throughHole, 8.0, 0.5, 1.0), 0.0);
    QVERIFY(EffectMaskDistance::strokeInfluence(
                donut, {QPointF(20.0, 35.0), QPointF(20.0, 65.0)}, 8.0, 0.5, 1.0) > 0.99);
    QVERIFY(EffectMaskDistance::strokeInfluence(
                donut, {QPointF(10.0, 50.0)}, 2.0, 0.5, 1.0) > 0.99);

    QPainterPath concave;
    QPolygonF lShape;
    lShape << QPointF(0.0, 0.0) << QPointF(100.0, 0.0)
           << QPointF(100.0, 20.0) << QPointF(20.0, 20.0)
           << QPointF(20.0, 100.0) << QPointF(0.0, 100.0);
    concave.addPolygon(lShape);
    concave.closeSubpath();
    QCOMPARE(EffectMaskDistance::strokeInfluence(
                 concave, {QPointF(70.0, 70.0)}, 10.0, 0.4, 1.0), 0.0);
    QVERIFY(EffectMaskDistance::strokeInfluence(
                concave, {QPointF(20.0, 70.0)}, 10.0, 0.4, 1.0) > 0.99);

    const qreal near = EffectMaskDistance::strokeInfluence(
        concave, {QPointF(23.0, 70.0)}, 12.0, 0.0, 1.0);
    const qreal farther = EffectMaskDistance::strokeInfluence(
        concave, {QPointF(28.0, 70.0)}, 12.0, 0.0, 1.0);
    QVERIFY(near >= farther);
    QVERIFY(near >= 0.0 && near <= 1.0);
    QVERIFY(farther >= 0.0 && farther <= 1.0);
}

void CoreTests::contourMaskDistanceHandlesAdversarialGeometryAndCancellation()
{
    QPainterPath rectangle;
    rectangle.addRect(QRectF(0.0, 0.0, 100.0, 100.0));

    // Neither endpoint is in the fill, but the painted segment crosses it.
    QVERIFY(EffectMaskDistance::strokeInfluence(
                rectangle, {QPointF(-20.0, 50.0), QPointF(120.0, 50.0)},
                1.0, 1.0, 1.0) > 0.99);
    // The radius boundary is deliberately exclusive: zero remaining falloff
    // cannot authorize an effect.
    QCOMPARE(EffectMaskDistance::strokeInfluence(
                 rectangle, {QPointF(105.0, 50.0)}, 5.0, 0.0, 1.0), 0.0);

    QPainterPath open;
    open.moveTo(0.0, 0.0);
    open.lineTo(100.0, 0.0);
    QVERIFY(EffectMaskDistance::strokeInfluence(
                open, {QPointF(50.0, 1.0)}, 3.0, 0.5, 1.0) > 0.0);

    QPainterPath multiple;
    multiple.addRect(QRectF(0.0, 0.0, 10.0, 10.0));
    multiple.moveTo(40.0, 0.0);
    multiple.lineTo(60.0, 0.0);
    QVERIFY(EffectMaskDistance::strokeInfluence(
                multiple, {QPointF(50.0, 0.0), QPointF(50.0, 0.0)},
                2.0, 0.5, 1.0) > 0.99);

    QPainterPath nested;
    nested.addRect(QRectF(0.0, 0.0, 100.0, 100.0));
    nested.addRect(QRectF(20.0, 20.0, 60.0, 60.0));
    nested.setFillRule(Qt::WindingFill);
    QVERIFY(EffectMaskDistance::strokeInfluence(
                nested, {QPointF(50.0, 50.0)}, 2.0, 0.5, 1.0) > 0.99);
    nested.setFillRule(Qt::OddEvenFill);
    QCOMPARE(EffectMaskDistance::strokeInfluence(
                 nested, {QPointF(50.0, 50.0)}, 10.0, 0.5, 1.0), 0.0);

    QPainterPath ellipse;
    ellipse.addEllipse(QRectF(0.0, 0.0, 100.0, 100.0));
    QVERIFY(EffectMaskDistance::strokeInfluence(
                ellipse, {QPointF(100.5, 50.0)}, 2.0, 0.0, 1.0) > 0.0);

    QCOMPARE(EffectMaskDistance::strokeInfluence(
                 rectangle, {QPointF(0.0, 0.0)}, 0.0, 0.5, 1.0), 0.0);
    QCOMPARE(EffectMaskDistance::strokeInfluence(
                 rectangle, {QPointF(0.0, 0.0)},
                 std::numeric_limits<qreal>::quiet_NaN(), 0.5, 1.0), 0.0);
    QCOMPARE(EffectMaskDistance::strokeInfluence(
                 rectangle, {QPointF(0.0, 0.0)}, 2.0,
                 std::numeric_limits<qreal>::quiet_NaN(), 1.0), 0.0);
    QCOMPARE(EffectMaskDistance::strokeInfluence(
                 rectangle, {QPointF(0.0, 0.0)}, 2.0, 0.5,
                 std::numeric_limits<qreal>::quiet_NaN()), 0.0);
    QCOMPARE(EffectMaskDistance::strokeInfluence(
                 rectangle,
                 {QPointF(std::numeric_limits<qreal>::quiet_NaN(), 0.0)},
                 2.0, 0.5, 1.0), 0.0);

    const WorkControl cancelled = WorkControl::withBudget();
    cancelled.setCheckpointCallback([&cancelled](qint64) { cancelled.cancel(); });
    QCOMPARE(EffectMaskDistance::strokeInfluence(
                 ellipse, {QPointF(101.0, 50.0)}, 4.0, 0.5, 1.0, cancelled), 0.0);
    QCOMPARE(cancelled.status(), WorkControlStatus::Cancelled);
    QVERIFY(std::isinf(EffectMaskDistance::minimumContourDistance(
        ellipse, {QPointF(101.0, 50.0)}, 0.25, cancelled)));
}

void CoreTests::workControlHasExactSharedTerminalBoundaries()
{
    const WorkControl exact = WorkControl::withBudget(5);
    const WorkControl copy = exact;
    QVERIFY(exact.consume(2));
    QVERIFY(copy.consume(3));
    QCOMPARE(exact.unitsConsumed(), qint64(5));
    QCOMPARE(copy.unitsConsumed(), qint64(5));
    QCOMPARE(exact.status(), WorkControlStatus::Running);
    QVERIFY(!copy.consume(1));
    QCOMPARE(exact.unitsConsumed(), qint64(5));
    QCOMPARE(exact.status(), WorkControlStatus::BudgetExceeded);
    exact.cancel();
    exact.cancel();
    QCOMPARE(copy.status(), WorkControlStatus::BudgetExceeded);

    const WorkControl cancelledFirst = WorkControl::withBudget(1);
    cancelledFirst.cancel();
    cancelledFirst.cancel();
    QVERIFY(!cancelledFirst.consume(1));
    QCOMPARE(cancelledFirst.unitsConsumed(), qint64(0));
    QCOMPARE(cancelledFirst.status(), WorkControlStatus::Cancelled);

    const WorkControl callback = WorkControl::withBudget(10);
    const WorkControl callbackCopy = callback;
    callbackCopy.setCheckpointCallback(
        [&callback](qint64 consumed) {
            if (consumed == 3) callback.cancel();
        });
    QVERIFY(!callback.consume(3));
    QCOMPARE(callback.unitsConsumed(), qint64(3));
    QCOMPARE(callbackCopy.status(), WorkControlStatus::Cancelled);

    const qint64 maximum = std::numeric_limits<qint64>::max();
    const WorkControl wide = WorkControl::withBudget(maximum);
    QVERIFY(wide.consume(maximum - 1));
    QVERIFY(wide.consume(1));
    QCOMPARE(wide.unitsConsumed(), maximum);
    QVERIFY(!wide.consume(1));
    QCOMPARE(wide.unitsConsumed(), maximum);
    QCOMPARE(wide.status(), WorkControlStatus::BudgetExceeded);

    const WorkControl noOp = WorkControl::withBudget(2);
    QVERIFY(noOp.consume(0));
    QVERIFY(noOp.consume(-1));
    QCOMPARE(noOp.unitsConsumed(), qint64(0));
}

void CoreTests::evaluationCancellationDoesNotPoisonWorkerCaches()
{
    TextObject fixture = configuredText(QString(96, QLatin1Char('W')));
    fixture.id = QStringLiteral("cache-baseline-object");
    std::unique_ptr<Effect> wave = EffectRegistry::instance().create(QStringLiteral("wave"));
    QVERIFY(wave);
    QVERIFY(wave->setParameter(QStringLiteral("amplitude"), 0.75));
    EffectMaskStroke mask;
    mask.points = {QPointF(-20.0, 0.0), QPointF(300.0, 40.0), QPointF(600.0, 0.0)};
    mask.radius = 80.0;
    wave->maskStrokes.push_back(mask);
    fixture.effects.append(std::move(wave));
    fixture.deformation.strokes.push_back(pushStroke());

    auto pageFor = [](const TextObject& source) {
        Page page;
        page.id = QStringLiteral("cache-page");
        page.layers.front()->id = QStringLiteral("cache-layer");
        page.layers.front()->objects.push_back(std::make_unique<TextObject>(source));
        return page;
    };

    SceneEvaluator::invalidateFontCaches();
    const WorkControl baselineWork = WorkControl::unlimited();
    const SceneGeometry baseline = SceneEvaluator::evaluate(pageFor(fixture), 71, baselineWork);
    QCOMPARE(baseline.evaluationStatus, EvaluationStatus::Complete);
    QCOMPARE(baseline.objects.size(), 1);
    QVERIFY(baselineWork.unitsConsumed() > 8);
    const test::GeometrySignature expected = test::geometrySignature(
        baseline.objects.front().geometry);

    fixture.id = QStringLiteral("cache-poison-object");
    QVector<qint64> checkpoints = {
        1,
        qMax<qint64>(2, baselineWork.unitsConsumed() / 4),
        qMax<qint64>(3, baselineWork.unitsConsumed() / 2),
        qMax<qint64>(4, baselineWork.unitsConsumed() * 3 / 4),
        qMax<qint64>(5, baselineWork.unitsConsumed() - 1),
    };
    std::sort(checkpoints.begin(), checkpoints.end());
    checkpoints.erase(std::unique(checkpoints.begin(), checkpoints.end()), checkpoints.end());

    for (const qint64 checkpoint : checkpoints) {
        SceneEvaluator::invalidateFontCaches();
        const WorkControl interrupted = WorkControl::withBudget();
        interrupted.setCheckpointCallback(
            [&interrupted, checkpoint](qint64 consumed) {
                if (consumed >= checkpoint) interrupted.cancel();
            });
        const SceneGeometry partial = SceneEvaluator::evaluate(pageFor(fixture), 72, interrupted);
        QCOMPARE(partial.evaluationStatus, EvaluationStatus::Cancelled);
        QVERIFY(partial.objects.isEmpty());

        const WorkControl recoveredWork = WorkControl::unlimited();
        const SceneGeometry recovered = SceneEvaluator::evaluate(pageFor(fixture), 73, recoveredWork);
        QCOMPARE(recovered.evaluationStatus, EvaluationStatus::Complete);
        QCOMPARE(recovered.objects.size(), 1);
        QString difference;
        QVERIFY2(test::compareGeometry(
                     expected, test::geometrySignature(recovered.objects.front().geometry),
                     &difference),
                 qPrintable(QStringLiteral("cache recovery after checkpoint %1: %2")
                                .arg(checkpoint).arg(difference)));
    }
}

void CoreTests::fontCacheEpochInvalidatesWorkerShapingKeys()
{
    const TextObject object = configuredText(QStringLiteral("Epoch"));
    const QByteArray before = SceneEvaluator::shapingCacheKey(object);
    const quint64 epoch = SceneEvaluator::fontCacheEpoch();
    SceneEvaluator::invalidateFontCaches();
    QVERIFY(SceneEvaluator::fontCacheEpoch() > epoch);
    QVERIFY(SceneEvaluator::shapingCacheKey(object) != before);
}

void CoreTests::effectRegistryDescriptorsAgreeWithFactories()
{
    QString error;
    QVERIFY2(EffectRegistry::instance().validate(&error), qPrintable(error));
    QSet<QString> ids;
    for (const EffectDescriptor& descriptor : EffectRegistry::instance().descriptors()) {
        QVERIFY(!ids.contains(descriptor.typeId));
        ids.insert(descriptor.typeId);
        QVERIFY(EffectRegistry::instance().create(descriptor.typeId) != nullptr);
    }
}

void CoreTests::builtInPresetCatalogParses()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PresetManager manager(directory.path());
    PresetCatalog catalog(manager);
    QString diagnostics;
    const QVector<PresetCatalogEntry> entries = catalog.entries(&diagnostics);
    QVERIFY2(diagnostics.isEmpty(), qPrintable(diagnostics));
    QCOMPARE(entries.size(), 16);
    for (const PresetCatalogEntry& entry : entries) {
        QVERIFY(entry.builtIn);
        QVERIFY(entry.preset.id.startsWith(QStringLiteral("builtin.")));
        QVERIFY(!entry.preset.effects.isEmpty());
    }
}

void CoreTests::effectStackStrengthZeroIsIdentity()
{
    VectorGeometry geometry = rectangleGeometry();
    const QByteArray before = geometrySignature(geometry);
    for (const EffectDescriptor& descriptor : EffectRegistry::instance().descriptors()) {
        EffectStack stack;
        stack.append(EffectRegistry::instance().create(descriptor.typeId));
        VectorGeometry candidate = geometry;
        stack.apply(candidate, 0.0);
        QCOMPARE(geometrySignature(candidate), before);
    }
}

void CoreTests::textRangeRebasingUsesUtf16Offsets()
{
    EffectScope range{EffectScopeKind::TextRange, 2, 4};
    const EffectScope shifted = TextRangeRebaser::rebase(range, QStringLiteral("abCD"), QStringLiteral("XabCD"));
    QCOMPARE(shifted.start, 3);
    QCOMPARE(shifted.end, 5);
    const QString emoji = QString::fromUtf8("AрџЂBC");
    const EffectScope emojiRange{EffectScopeKind::TextRange, 3, 5}; // B/C after surrogate pair
    const EffectScope unchanged = TextRangeRebaser::rebase(emojiRange, emoji, emoji + QStringLiteral("!"));
    QCOMPARE(unchanged.start, 3);
    QCOMPARE(unchanged.end, 5);
    const EffectScope deleted = TextRangeRebaser::rebase(range, QStringLiteral("abCD"), QStringLiteral("ab"));
    QCOMPARE(deleted.start, 2);
    QCOMPARE(deleted.end, 2);
}

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    CoreTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "core_tests.moc"

