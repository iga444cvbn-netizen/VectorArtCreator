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
#include <utility>

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

TextObject configuredText(const QString& text = QStringLiteral("Hello Мир"))
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

VectorGeometry pathEffectFixture(const QVector<qreal>& offsets,
                                 qreal advance = 8.0)
{
    VectorGeometry geometry;
    qreal maximumOffset = 0.0;
    for (int index = 0; index < offsets.size(); ++index) {
        const qreal offset = offsets.at(index);
        maximumOffset = qMax(maximumOffset, offset);
        GeometryPiece piece;
        piece.path.addRect(QRectF(offset - 4.0, -4.0, 8.0, 8.0));
        piece.anchor = QPointF(offset, 0.0);
        piece.originalAnchor = piece.anchor;
        piece.layoutOrigin = piece.anchor;
        piece.layoutAdvance = advance;
        piece.sourceGlyphIndex = index;
        piece.sourceClusterStart = index;
        geometry.pieces.push_back(std::move(piece));
    }
    geometry.setReferenceBounds(QRectF(0.0, -8.0,
                                       maximumOffset + advance, 16.0));
    geometry.recomputeBounds();
    return geometry;
}

QVector<qreal> effectAnchorDisplacements(VectorGeometry geometry,
                                         const Effect& effect)
{
    QVector<qreal> before;
    before.reserve(geometry.pieces.size());
    for (const GeometryPiece& piece : geometry.pieces) {
        before.push_back(piece.anchor.y());
    }
    effect.apply(geometry, {geometry.referenceBounds, geometry.referenceHeight});

    QVector<qreal> result;
    result.reserve(geometry.pieces.size());
    for (int index = 0; index < geometry.pieces.size(); ++index) {
        result.push_back(geometry.pieces.at(index).anchor.y() - before.at(index));
    }
    return result;
}

PathGeometry verticalEffectPath()
{
    PathGeometry path;
    path.id = QStringLiteral("effect-vertical");
    path.nodes = {
        {QStringLiteral("vertical-start"), QPointF(500.0, 100.0), {}, {}, false, false},
        {QStringLiteral("vertical-end"), QPointF(500.0, 700.0), {}, {}, false, false},
    };
    return path;
}

PathGeometry backtrackingEffectPath()
{
    PathGeometry path;
    path.id = QStringLiteral("effect-backtracking");
    path.nodes = {
        {QStringLiteral("backtracking-0"), QPointF(0.0, 0.0), {}, {}, false, false},
        {QStringLiteral("backtracking-1"), QPointF(200.0, 0.0), {}, {}, false, false},
        {QStringLiteral("backtracking-2"), QPointF(0.0, 200.0), {}, {}, false, false},
        {QStringLiteral("backtracking-3"), QPointF(200.0, 200.0), {}, {}, false, false},
    };
    return path;
}

PathGeometry curvedClosedEffectPath()
{
    PathGeometry path;
    path.id = QStringLiteral("effect-curved-closed");
    PathNode first;
    first.id = QStringLiteral("curved-0");
    first.anchor = QPointF(0.0, 0.0);
    first.incomingHandle = QPointF(-80.0, 100.0);
    first.hasIncomingHandle = true;
    first.outgoingHandle = QPointF(100.0, -80.0);
    first.hasOutgoingHandle = true;

    PathNode second;
    second.id = QStringLiteral("curved-1");
    second.anchor = QPointF(400.0, 0.0);
    second.incomingHandle = QPointF(300.0, -80.0);
    second.hasIncomingHandle = true;
    second.outgoingHandle = QPointF(480.0, 100.0);
    second.hasOutgoingHandle = true;

    PathNode third;
    third.id = QStringLiteral("curved-2");
    third.anchor = QPointF(400.0, 400.0);
    third.incomingHandle = QPointF(480.0, 300.0);
    third.hasIncomingHandle = true;
    third.outgoingHandle = QPointF(300.0, 480.0);
    third.hasOutgoingHandle = true;

    PathNode fourth;
    fourth.id = QStringLiteral("curved-3");
    fourth.anchor = QPointF(0.0, 400.0);
    fourth.incomingHandle = QPointF(100.0, 480.0);
    fourth.hasIncomingHandle = true;
    fourth.outgoingHandle = QPointF(-80.0, 300.0);
    fourth.hasOutgoingHandle = true;

    path.nodes = {first, second, third, fourth};
    path.closed = true;
    return path;
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
    void postPathEffectsFollowArcLengthTraversal();
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
    QVERIFY(std::abs(wrappedBackward.point.x() - 10.0) < 1.0e-6);

    PathGeometry reversed = closed;
    reversed.reverseDirection();
    const auto reversedTable = PathArcLengthTable::build(reversed);
    QVERIFY(reversedTable.has_value());
    const PathPosition reversedStart = reversedTable->positionAt(0.0, false);
    QVERIFY(reversedStart.valid);
    QVERIFY(QLineF(reversedStart.point, QPointF(100.0, 0.0)).length() < 1.0e-6);
    QVERIFY(reversedStart.tangent.x() < -0.99);
    reversed.reverseDirection();
    QVERIFY(reversed == closed);

    ShapedText shaped;
    shaped.lineBounds = {QRectF(0.0, 0.0, 100.0, 20.0)};
    VectorGeometry base;
    GeometryPiece piece;
    piece.path.addRect(QRectF(0.0, -10.0, 10.0, 10.0));
    piece.anchor = QPointF();
    piece.originalAnchor = piece.anchor;
    piece.layoutOrigin = piece.anchor;
    piece.layoutAdvance = 10.0;
    base.pieces.push_back(piece);
    base.setReferenceBounds(QRectF(0.0, -10.0, 10.0, 10.0));
    base.recomputeBounds();
    PathTypographyProperties settings;
    settings.enabled = true;
    settings.pathId = closed.id;
    settings.baselineOffset = 5.0;

    QString error;
    VectorGeometry normal = base;
    QVERIFY2(PathLayoutEngine::apply(&normal, shaped, closed, settings, &error),
             qPrintable(error));
    settings.flip = true;
    VectorGeometry flipped = base;
    QVERIFY2(PathLayoutEngine::apply(&flipped, shaped, closed, settings, &error),
             qPrintable(error));
    QCOMPARE(normal.pieces.front().anchor.x(), flipped.pieces.front().anchor.x());
    QVERIFY(std::abs(normal.pieces.front().anchor.y()
                     + flipped.pieces.front().anchor.y()) < 1.0e-6);
    const auto normalFirst = normal.pieces.front().path.elementAt(0);
    const auto normalSecond = normal.pieces.front().path.elementAt(1);
    const auto flippedFirst = flipped.pieces.front().path.elementAt(0);
    const auto flippedSecond = flipped.pieces.front().path.elementAt(1);
    QVERIFY(normalSecond.x - normalFirst.x > 0.0);
    QVERIFY(flippedSecond.x - flippedFirst.x > 0.0);
    QVERIFY(std::abs(normalSecond.y - normalFirst.y) < 1.0e-6);
    QVERIFY(std::abs(flippedSecond.y - flippedFirst.y) < 1.0e-6);

    ObjectTransform transform;
    transform.position = QPointF(80.0, -35.0);
    transform.rotation = 23.0;
    transform.scale = QPointF(-1.5, 0.75);
    transform.hasPivot = true;
    transform.pivotLocal = QPointF(50.0, 0.0);
    const ObjectFrame frame = ObjectFrame::fromTransform(
        transform, QRectF(0.0, -20.0, 100.0, 40.0));
    const QPointF localPoint = normal.pieces.front().anchor;
    const QPointF pagePoint = frame.localPointToPage(localPoint);
    QVERIFY(QLineF(frame.pagePointToLocal(pagePoint), localPoint).length() < 1.0e-8);
}

void CoreTests::pathSubdivisionAndDegenerateGeometryStayBounded()
{
    // The following two cubic segments are the exact de Casteljau split of
    // one cubic at t=.5. Their arc lengths should agree independently of the
    // implementation's internal lookup segmentation.
    PathGeometry unsplit;
    unsplit.id = QStringLiteral("unsplit");
    PathNode u0;
    u0.id = QStringLiteral("u0");
    u0.anchor = QPointF(0.0, 0.0);
    u0.hasOutgoingHandle = true;
    u0.outgoingHandle = QPointF(0.0, 120.0);
    PathNode u1;
    u1.id = QStringLiteral("u1");
    u1.anchor = QPointF(160.0, 0.0);
    u1.hasIncomingHandle = true;
    u1.incomingHandle = QPointF(160.0, 120.0);
    unsplit.nodes = {u0, u1};

    PathGeometry split;
    split.id = QStringLiteral("split");
    PathNode s0;
    s0.id = QStringLiteral("s0");
    s0.anchor = QPointF(0.0, 0.0);
    s0.hasOutgoingHandle = true;
    s0.outgoingHandle = QPointF(0.0, 60.0);
    PathNode s1;
    s1.id = QStringLiteral("s1");
    s1.anchor = QPointF(80.0, 90.0);
    s1.hasIncomingHandle = true;
    s1.incomingHandle = QPointF(40.0, 90.0);
    s1.hasOutgoingHandle = true;
    s1.outgoingHandle = QPointF(120.0, 90.0);
    PathNode s2;
    s2.id = QStringLiteral("s2");
    s2.anchor = QPointF(160.0, 0.0);
    s2.hasIncomingHandle = true;
    s2.incomingHandle = QPointF(160.0, 60.0);
    split.nodes = {s0, s1, s2};

    const auto unsplitTable = PathArcLengthTable::build(unsplit);
    const auto splitTable = PathArcLengthTable::build(split);
    QVERIFY(unsplitTable.has_value());
    QVERIFY(splitTable.has_value());
    QVERIFY(std::abs(unsplitTable->totalLength() - splitTable->totalLength()) < 0.8);

    PathGeometry degenerate;
    degenerate.id = QStringLiteral("degenerate");
    PathNode d0;
    d0.id = QStringLiteral("d0");
    d0.anchor = QPointF(12.0, 12.0);
    PathNode d1;
    d1.id = QStringLiteral("d1");
    d1.anchor = d0.anchor;
    degenerate.nodes = {d0, d1};
    const auto degenerateTable = PathArcLengthTable::build(degenerate);
    QVERIFY(degenerateTable.has_value());
    QCOMPARE(degenerateTable->totalLength(), 0.0);
    QVERIFY(!degenerateTable->positionAt(0.0, false).valid);

    ShapedText shaped;
    shaped.lineBounds = {QRectF(0.0, 0.0, 100.0, 20.0)};
    VectorGeometry geometry;
    for (int index = 0; index < 3; ++index) {
        GeometryPiece piece;
        piece.path.addRect(QRectF(index * 15.0, -10.0, 8.0, 10.0));
        piece.anchor = QPointF(index * 15.0, 0.0);
        piece.originalAnchor = piece.anchor;
        piece.layoutOrigin = piece.anchor;
        piece.layoutAdvance = 8.0;
        geometry.pieces.push_back(piece);
    }
    geometry.setReferenceBounds(QRectF(0.0, -10.0, 38.0, 10.0));
    geometry.recomputeBounds();
    PathTypographyProperties settings;
    settings.enabled = true;
    settings.pathId = degenerate.id;
    QString error;
    QVERIFY2(PathLayoutEngine::apply(&geometry, shaped, degenerate, settings, &error),
             qPrintable(error));
    for (const GeometryPiece& piece : geometry.pieces) {
        QVERIFY(piece.path.isEmpty());
    }
}

void CoreTests::pathLayoutClipsOpenOverflowWithoutEndpointPileup()
{
    ShapedText shaped;
    shaped.lineBounds = {QRectF(0.0, 0.0, 100.0, 20.0)};
    shaped.lineCount = 1;
    VectorGeometry geometry;
    for (const qreal x : {0.0, 20.0, 95.0}) {
        GeometryPiece piece;
        piece.path.addRect(QRectF(x, -10.0, 10.0, 10.0));
        piece.anchor = QPointF(x, 0.0);
        piece.originalAnchor = piece.anchor;
        piece.layoutOrigin = piece.anchor;
        piece.layoutAdvance = 10.0;
        geometry.pieces.push_back(piece);
    }
    geometry.setReferenceBounds(QRectF(0.0, -10.0, 105.0, 10.0));
    geometry.recomputeBounds();

    const PathGeometry path = PathGeometry::makeDefault(100.0);
    PathTypographyProperties settings;
    settings.enabled = true;
    settings.pathId = path.id;
    QString error;
    QVERIFY2(PathLayoutEngine::apply(&geometry, shaped, path, settings, &error),
             qPrintable(error));
    QVERIFY(!geometry.pieces.at(0).path.isEmpty());
    QVERIFY(!geometry.pieces.at(1).path.isEmpty());
    QVERIFY(geometry.pieces.at(2).path.isEmpty());
    QVERIFY(std::abs(geometry.pieces.at(0).anchor.x()) < 1.0e-6);
    QVERIFY(std::abs(geometry.pieces.at(1).anchor.x() - 20.0) < 1.0e-6);
    QCOMPARE(geometry.pieces.at(0).originalAnchor, QPointF(0.0, 0.0));
    QCOMPARE(geometry.pieces.at(1).originalAnchor, QPointF(20.0, 0.0));
}

void CoreTests::pathLayoutPreservesClustersThroughEffects()
{
    TextObject object = configuredText(
        QStringLiteral("A\u0301 \u041f\u0440\u0438\u0432\u0435\u0442\nemoji \U0001F600"));
    TextEngine engine;
    const ShapedText shaped = engine.shape(object);
    QVERIFY2(shaped.error.isEmpty(), qPrintable(shaped.error));
    VectorGeometry geometry = GlyphGeometryBuilder::build(
        shaped, object.typography.fontSize, object.font.underline, object.font.strikeOut);
    QVector<QPair<int, int>> metadata;
    for (const GeometryPiece& piece : geometry.pieces) {
        metadata.push_back({piece.sourceClusterStart, piece.sourceClusterLength});
    }

    const PathGeometry path = PathGeometry::makeDefault(1600.0);
    PathTypographyProperties settings;
    settings.enabled = true;
    settings.pathId = path.id;
    settings.baselineOffset = 8.0;
    QString error;
    QVERIFY2(PathLayoutEngine::apply(&geometry, shaped, path, settings, &error),
             qPrintable(error));
    QCOMPARE(geometry.pieces.size(), metadata.size());
    for (int index = 0; index < geometry.pieces.size(); ++index) {
        QCOMPARE(geometry.pieces.at(index).sourceClusterStart, metadata.at(index).first);
        QCOMPARE(geometry.pieces.at(index).sourceClusterLength, metadata.at(index).second);
    }

    auto wave = std::make_unique<WaveEffect>();
    wave->amplitude = 0.4;
    wave->frequency = 1.7;
    wave->scope = {EffectScopeKind::TextRange, 0,
                   qMax<int>(1, static_cast<int>(object.sourceText.size() / 2))};
    EffectStack effects;
    effects.append(std::move(wave));
    const QByteArray before = geometrySignature(geometry);
    effects.apply(geometry);
    QVERIFY(geometrySignature(geometry) != before);
    for (int index = 0; index < geometry.pieces.size(); ++index) {
        QCOMPARE(geometry.pieces.at(index).sourceClusterStart, metadata.at(index).first);
        QCOMPARE(geometry.pieces.at(index).sourceClusterLength, metadata.at(index).second);
    }
}

void CoreTests::postPathEffectsFollowPathProgressInsteadOfSourceAnchors()
{
    ShapedText shaped;
    shaped.lineBounds = {QRectF(0.0, 0.0, 80.0, 24.0)};

    VectorGeometry geometry;
    for (int index = 0; index < 4; ++index) {
        GeometryPiece piece;
        const qreal x = index * 20.0;
        piece.path.addRect(QRectF(x, -8.0, 12.0, 16.0));
        piece.anchor = QPointF(x, 0.0);
        piece.originalAnchor = piece.anchor;
        piece.layoutOrigin = piece.anchor;
        piece.layoutAdvance = 12.0;
        piece.sourceGlyphIndex = index;
        piece.sourceClusterStart = index;
        geometry.pieces.push_back(piece);
    }
    geometry.setReferenceBounds(QRectF(0.0, -8.0, 92.0, 16.0));
    geometry.recomputeBounds();

    PathGeometry translated;
    translated.id = QStringLiteral("translated-effect-reference");
    PathNode start;
    start.id = QStringLiteral("translated-start");
    start.anchor = QPointF(500.0, 0.0);
    PathNode end;
    end.id = QStringLiteral("translated-end");
    end.anchor = QPointF(900.0, 0.0);
    translated.nodes = {start, end};

    PathTypographyProperties settings;
    settings.enabled = true;
    settings.pathId = translated.id;
    QString error;
    QVERIFY2(PathLayoutEngine::apply(&geometry, shaped, translated, settings, &error),
             qPrintable(error));
    QVERIFY(geometry.pieces.at(3).anchor.x() > geometry.pieces.at(0).anchor.x());

    WaveEffect wave;
    wave.amplitude = 1.0;
    wave.frequency = 1.0;
    wave.phase = 0.125;
    wave.apply(geometry, {geometry.referenceBounds, geometry.referenceHeight});

    // The path is deliberately translated far beyond the source layout. A
    // source-anchor/reference-bounds mixup clamps every piece to progress 0,
    // so this assertion fails at the reviewed Phase 4D head while remaining
    // independent of any particular font outline.
    const qreal firstDisplacement = geometry.pieces.at(0).anchor.y();
    const qreal lastDisplacement = geometry.pieces.at(3).anchor.y();
    QVERIFY2(std::abs(lastDisplacement - firstDisplacement) > 0.01,
             "post-path wave progress must follow the translated path traversal");

    // Reverse traversal and a closed curved path exercise the same contract
    // at the two boundaries where a simple x-normalization is least useful.
    settings.reverse = true;
    geometry = [&] {
        VectorGeometry rebuilt;
        for (int index = 0; index < 4; ++index) {
            GeometryPiece piece;
            const qreal x = index * 20.0;
            piece.path.addRect(QRectF(x, -8.0, 12.0, 16.0));
            piece.anchor = QPointF(x, 0.0);
            piece.originalAnchor = piece.anchor;
            piece.layoutOrigin = piece.anchor;
            piece.layoutAdvance = 12.0;
            piece.sourceGlyphIndex = index;
            piece.sourceClusterStart = index;
            rebuilt.pieces.push_back(piece);
        }
        rebuilt.setReferenceBounds(QRectF(0.0, -8.0, 92.0, 16.0));
        rebuilt.recomputeBounds();
        return rebuilt;
    }();
    QVERIFY2(PathLayoutEngine::apply(&geometry, shaped, translated, settings, &error),
             qPrintable(error));
    wave.apply(geometry, {geometry.referenceBounds, geometry.referenceHeight});
    QVERIFY(std::abs(geometry.pieces.at(3).anchor.y()
                     - geometry.pieces.at(0).anchor.y()) > 0.01);

    PathGeometry curved = translated;
    curved.closed = true;
    curved.nodes.front().hasOutgoingHandle = true;
    curved.nodes.front().outgoingHandle = QPointF(500.0, 280.0);
    curved.nodes.back().hasIncomingHandle = true;
    curved.nodes.back().incomingHandle = QPointF(900.0, 280.0);
    curved.nodes.push_back({QStringLiteral("translated-bottom"),
                            QPointF(900.0, 360.0), {}, {}, false, false});
    settings.reverse = false;
    geometry = [&] {
        VectorGeometry rebuilt;
        for (int index = 0; index < 4; ++index) {
            GeometryPiece piece;
            const qreal x = index * 20.0;
            piece.path.addRect(QRectF(x, -8.0, 12.0, 16.0));
            piece.anchor = QPointF(x, 0.0);
            piece.originalAnchor = piece.anchor;
            piece.layoutOrigin = piece.anchor;
            piece.layoutAdvance = 12.0;
            piece.sourceGlyphIndex = index;
            piece.sourceClusterStart = index;
            rebuilt.pieces.push_back(piece);
        }
        rebuilt.setReferenceBounds(QRectF(0.0, -8.0, 92.0, 16.0));
        rebuilt.recomputeBounds();
        return rebuilt;
    }();
    QVERIFY2(PathLayoutEngine::apply(&geometry, shaped, curved, settings, &error),
             qPrintable(error));
    wave.apply(geometry, {geometry.referenceBounds, geometry.referenceHeight});
    QVERIFY(std::abs(geometry.pieces.at(3).anchor.y()
                     - geometry.pieces.at(0).anchor.y()) > 0.01);
}

void CoreTests::postPathEffectsFollowArcLengthTraversal()
{
    ShapedText shaped;
    shaped.lineBounds = {QRectF(0.0, 0.0, 2400.0, 24.0)};

    WaveEffect wave;
    wave.amplitude = 1.0;
    wave.frequency = 0.25;
    wave.phase = 0.0;

    auto applyPath = [&](VectorGeometry* geometry,
                         const PathGeometry& path,
                         const PathTypographyProperties& settings,
                         QString* error) {
        return PathLayoutEngine::apply(geometry, shaped, path, settings, error);
    };

    {
        const PathGeometry path = verticalEffectPath();
        PathTypographyProperties settings;
        settings.enabled = true;
        settings.pathId = path.id;
        VectorGeometry geometry = pathEffectFixture(
            {0.0, 120.0, 240.0, 360.0}, 12.0);
        QString error;
        QVERIFY2(applyPath(&geometry, path, settings, &error), qPrintable(error));
        const QVector<qreal> displacements = effectAnchorDisplacements(geometry, wave);
        const QVector<qreal> expectedProgress = {0.0, 0.2, 0.4, 0.6};
        for (int index = 0; index < geometry.pieces.size(); ++index) {
            QVERIFY(geometry.pieces.at(index).hasEffectReferenceProgress);
            const QString progressDescription = QStringLiteral(
                "vertical index %1 actual %2 expected %3")
                .arg(index)
                .arg(geometry.pieces.at(index).effectReferenceProgress, 0, 'g', 16)
                .arg(expectedProgress.at(index), 0, 'g', 16);
            if (std::abs(geometry.pieces.at(index).effectReferenceProgress
                         - expectedProgress.at(index)) >= 1.0e-6) {
                QFAIL(qPrintable(progressDescription));
            }
        }
        for (int index = 1; index < displacements.size(); ++index) {
            QVERIFY2(displacements.at(index) > displacements.at(index - 1) + 1.0e-4,
                     "vertical path progression must increase with arc distance");
        }

        PathTypographyProperties shiftedSettings = settings;
        shiftedSettings.startOffset = 60.0;
        VectorGeometry shifted = pathEffectFixture(
            {0.0, 120.0, 240.0, 360.0}, 12.0);
        QVERIFY2(applyPath(&shifted, path, shiftedSettings, &error), qPrintable(error));
        QVERIFY(std::abs(shifted.pieces.at(0).effectReferenceProgress - 0.1) < 1.0e-6);

        PathTypographyProperties baselineSettings = settings;
        baselineSettings.baselineOffset = 42.0;
        VectorGeometry baseline = pathEffectFixture(
            {0.0, 120.0, 240.0, 360.0}, 12.0);
        QVERIFY2(applyPath(&baseline, path, baselineSettings, &error), qPrintable(error));
        PathTypographyProperties flipSettings = settings;
        flipSettings.flip = true;
        VectorGeometry flipped = pathEffectFixture(
            {0.0, 120.0, 240.0, 360.0}, 12.0);
        QVERIFY2(applyPath(&flipped, path, flipSettings, &error), qPrintable(error));
        for (int index = 0; index < geometry.pieces.size(); ++index) {
            QVERIFY(std::abs(baseline.pieces.at(index).effectReferenceProgress
                             - geometry.pieces.at(index).effectReferenceProgress) < 1.0e-6);
            QVERIFY(std::abs(flipped.pieces.at(index).effectReferenceProgress
                             - geometry.pieces.at(index).effectReferenceProgress) < 1.0e-6);
        }

        QTransform objectTransform;
        objectTransform.translate(120.0, -55.0);
        VectorGeometry transformed = geometry;
        transformed.transformAll(objectTransform);
        for (int index = 0; index < geometry.pieces.size(); ++index) {
            QVERIFY(std::abs(transformed.pieces.at(index).effectReferenceProgress
                             - geometry.pieces.at(index).effectReferenceProgress) < 1.0e-6);
        }

        VectorGeometry clipped = pathEffectFixture({0.0, 1000.0}, 12.0);
        for (GeometryPiece& piece : clipped.pieces) {
            piece.effectReferenceProgress = 0.9;
            piece.hasEffectReferenceProgress = true;
        }
        QVERIFY2(applyPath(&clipped, path, settings, &error), qPrintable(error));
        QVERIFY(clipped.pieces.at(0).hasEffectReferenceProgress);
        QVERIFY(!clipped.pieces.at(1).hasEffectReferenceProgress);

        const test::GeometrySignature signature = test::geometrySignature(geometry);
        QVERIFY(signature.pieces.at(1).hasEffectReferenceProgress);
        QVERIFY(signature.pieces.at(1).effectReferenceProgress != 0);
        VectorGeometry changedProgress = geometry;
        changedProgress.pieces[1].effectReferenceProgress += 0.1;
        QString difference;
        QVERIFY2(!test::compareGeometry(
                      signature, test::geometrySignature(changedProgress), &difference),
                  "structured geometry signatures must observe path progress metadata");
        QVERIFY(difference.contains(QStringLiteral("effectReferenceProgress")));

        TrailEffect trail(QStringLiteral("trail"), QStringLiteral("Trail"));
        QVERIFY(trail.setParameter(QStringLiteral("copyCount"), 1.0));
        VectorGeometry generated = geometry;
        trail.apply(generated, {generated.referenceBounds, generated.referenceHeight});
        QCOMPARE(generated.pieces.size(), geometry.pieces.size() * 2);
        for (int index = geometry.pieces.size(); index < generated.pieces.size(); ++index) {
            const int sourceIndex = index - geometry.pieces.size();
            QVERIFY(generated.pieces.at(index).hasEffectReferenceProgress);
            QVERIFY(std::abs(generated.pieces.at(index).effectReferenceProgress
                             - geometry.pieces.at(sourceIndex).effectReferenceProgress) < 1.0e-6);
        }
    }

    {
        const PathGeometry path = backtrackingEffectPath();
        PathTypographyProperties settings;
        settings.enabled = true;
        settings.pathId = path.id;
        VectorGeometry geometry = pathEffectFixture(
            {0.0, 150.0, 250.0, 350.0, 500.0}, 10.0);
        QString error;
        QVERIFY2(applyPath(&geometry, path, settings, &error), qPrintable(error));
        const QVector<qreal> displacements = effectAnchorDisplacements(geometry, wave);
        const QVector<qreal> expectedProgress = {0.0, 0.25, 250.0 / 600.0,
                                                 350.0 / 600.0, 500.0 / 600.0};
        for (int index = 0; index < geometry.pieces.size(); ++index) {
            QVERIFY(geometry.pieces.at(index).hasEffectReferenceProgress);
            const QString progressDescription = QStringLiteral(
                "backtracking index %1 actual %2 expected %3")
                .arg(index)
                .arg(geometry.pieces.at(index).effectReferenceProgress, 0, 'g', 16)
                .arg(expectedProgress.at(index), 0, 'g', 16);
            if (std::abs(geometry.pieces.at(index).effectReferenceProgress
                         - expectedProgress.at(index)) >= 1.0e-6) {
                QFAIL(qPrintable(progressDescription));
            }
        }
        for (int index = 1; index < displacements.size(); ++index) {
            QVERIFY2(displacements.at(index) > displacements.at(index - 1) + 1.0e-4,
                     "backtracking path progression must follow distance, not X");
        }
    }

    {
        const PathGeometry path = PathGeometry::makeDefault(600.0);
        PathTypographyProperties settings;
        settings.enabled = true;
        settings.pathId = path.id;
        settings.reverse = true;
        VectorGeometry geometry = pathEffectFixture(
            {0.0, 120.0, 240.0, 360.0, 480.0}, 10.0);
        QString error;
        QVERIFY2(applyPath(&geometry, path, settings, &error), qPrintable(error));
        QVERIFY(geometry.pieces.at(1).anchor.x() < geometry.pieces.at(0).anchor.x());
        const QVector<qreal> displacements = effectAnchorDisplacements(geometry, wave);
        for (int index = 0; index < geometry.pieces.size(); ++index) {
            QVERIFY(geometry.pieces.at(index).hasEffectReferenceProgress);
            QVERIFY(std::abs(geometry.pieces.at(index).effectReferenceProgress
                             - index * 0.2) < 1.0e-6);
        }
        for (int index = 1; index < displacements.size(); ++index) {
            QVERIFY2(displacements.at(index) > displacements.at(index - 1) + 1.0e-4,
                     "reverse traversal must still progress from its own start");
        }
    }

    {
        const PathGeometry path = curvedClosedEffectPath();
        const std::optional<PathArcLengthTable> table = PathArcLengthTable::build(path);
        QVERIFY(table.has_value());
        const qreal total = table->totalLength();
        QVERIFY(total > 100.0);

        PathTypographyProperties settings;
        settings.enabled = true;
        settings.pathId = path.id;
        VectorGeometry geometry = pathEffectFixture(
            {total - 30.0, total + 90.0}, 8.0);
        QString error;
        QVERIFY2(applyPath(&geometry, path, settings, &error), qPrintable(error));
        ProceduralEffect verticalSpread(
            QStringLiteral("verticalSpread"), QStringLiteral("Vertical Spread"),
            ProceduralEffect::Mode::VerticalSpread);
        QVERIFY(verticalSpread.setParameter(QStringLiteral("strength"), 1.0));
        const QVector<qreal> displacements = effectAnchorDisplacements(
            geometry, verticalSpread);
        const PathPosition beforeSeam = table->positionAt(total - 30.0, true);
        const PathPosition afterSeam = table->positionAt(total + 90.0, true);
        QVERIFY(std::abs(geometry.pieces.at(0).effectReferenceProgress
                         - beforeSeam.distance / total) < 1.0e-6);
        QVERIFY(std::abs(geometry.pieces.at(1).effectReferenceProgress
                         - afterSeam.distance / total) < 1.0e-6);
        QVERIFY2(displacements.at(0) > 0.0,
                 "closed-path progress must remain near one before the seam");
        QVERIFY2(displacements.at(1) < 0.0,
                 "closed-path progress must wrap after the seam");
    }

    {
        VectorGeometry ordinary = pathEffectFixture({0.0, 100.0}, 8.0);
        ordinary.setReferenceBounds(QRectF(0.0, -8.0, 200.0, 16.0));
        ordinary.recomputeBounds();
        const QVector<qreal> displacements = effectAnchorDisplacements(ordinary, wave);
        QCOMPARE(displacements.size(), 2);
        for (const GeometryPiece& piece : ordinary.pieces) {
            QVERIFY(!piece.hasEffectReferenceProgress);
        }
        QVERIFY(std::abs(displacements.at(0)) < 1.0e-6);
        const qreal expectedSecond = ordinary.referenceHeight
            * std::sin(6.28318530717958647692 * wave.frequency * 0.5);
        QVERIFY(std::abs(displacements.at(1) - expectedSecond) < 1.0e-6);
    }
}

void CoreTests::complexShapingPlacementHasFiniteAdvancesAndClusters()
{
    const QString rtl = QString(QChar(0x05d0)) + QChar(0x05d1)
        + QChar(0x05d2) + QChar(0x05d3);
    const QStringList samples = {
        QStringLiteral("AV fi"),
        QStringLiteral("A\u0301"),
        QStringLiteral("\u041f\u0440\u0438\u0432\u0435\u0442"),
        QString::fromUtf8("emoji \xF0\x9F\x98\x80"),
        rtl,
        QStringLiteral("abc ") + rtl + QStringLiteral(" xyz"),
    };

    for (const QString& source : samples) {
        TextObject object = configuredText(source);
        TextEngine engine;
        const ShapedText shaped = engine.shape(object);
        QVERIFY2(shaped.error.isEmpty(), qPrintable(shaped.error));
        QVERIFY2(!shaped.glyphs.isEmpty(), qPrintable(source));

        int distinctPositions = 0;
        QPointF previous;
        bool havePrevious = false;
        for (const ShapedGlyph& glyph : shaped.glyphs) {
            QVERIFY(std::isfinite(glyph.position.x()));
            QVERIFY(std::isfinite(glyph.position.y()));
            QVERIFY(std::isfinite(glyph.advance));
            QVERIFY(glyph.advance >= 0.0);
            QVERIFY(glyph.clusterStart >= 0);
            QVERIFY(glyph.clusterStart + glyph.clusterLength <= source.size());
            if (havePrevious && glyph.position != previous) {
                ++distinctPositions;
            }
            previous = glyph.position;
            havePrevious = true;
        }
        if (shaped.glyphs.size() > 1) {
            QVERIFY2(distinctPositions > 0,
                     "complex shaped glyphs must not collapse onto one source position");
        }

        VectorGeometry geometry = GlyphGeometryBuilder::build(
            shaped, object.typography.fontSize, object.font.underline,
            object.font.strikeOut);
        PathGeometry path = PathGeometry::makeDefault(
            qMax<qreal>(1600.0, shaped.logicalBounds.width() + 800.0), 0.0);
        PathTypographyProperties settings;
        settings.enabled = true;
        settings.pathId = path.id;
        settings.startOffset = 37.0;
        QString error;
        QVERIFY2(PathLayoutEngine::apply(&geometry, shaped, path, settings, &error),
                 qPrintable(error));

        int visiblePieces = 0;
        QSet<qint64> anchorX;
        for (const GeometryPiece& piece : geometry.pieces) {
            if (piece.path.isEmpty()) continue;
            ++visiblePieces;
            QVERIFY(std::isfinite(piece.anchor.x()));
            QVERIFY(std::isfinite(piece.anchor.y()));
            anchorX.insert(qRound64(piece.anchor.x() * 1000.0));
            QVERIFY(piece.sourceClusterStart >= 0);
            QVERIFY(piece.sourceClusterStart + piece.sourceClusterLength <= source.size());
        }
        QVERIFY(visiblePieces > 0);
        if (visiblePieces > 1) {
            QVERIFY2(anchorX.size() > 1,
                     "path placement must not pile complex-shaped glyphs at one endpoint");
        }

        VectorGeometry reversed = GlyphGeometryBuilder::build(
            shaped, object.typography.fontSize, object.font.underline,
            object.font.strikeOut);
        settings.reverse = true;
        QVERIFY2(PathLayoutEngine::apply(&reversed, shaped, path, settings, &error),
                 qPrintable(error));
        QCOMPARE(reversed.pieces.size(), geometry.pieces.size());
        for (int index = 0; index < reversed.pieces.size(); ++index) {
            QCOMPARE(reversed.pieces.at(index).sourceClusterStart,
                     geometry.pieces.at(index).sourceClusterStart);
            QCOMPARE(reversed.pieces.at(index).sourceClusterLength,
                     geometry.pieces.at(index).sourceClusterLength);
        }
    }
}

void CoreTests::pathPipelineAppliesEffectsAndDeformationToFinalGeometry()
{
    TextObject base = configuredText(QStringLiteral("Path pipeline"));
    base.path = PathGeometry::makeDefault(1400.0, 0.0);
    base.pathLayout.enabled = true;
    base.pathLayout.pathId = base.path->id;

    Page page;
    QVERIFY(!page.layers.empty());
    page.layers.front()->objects.clear();
    page.layers.front()->objects.push_back(std::make_unique<TextObject>(base));
    page.layers.front()->objects.front()->id = QStringLiteral("path-pipeline-object");
    page.layers.front()->objects.front()->path->id = QStringLiteral("path-pipeline-path");
    page.layers.front()->objects.front()->pathLayout.pathId =
        page.layers.front()->objects.front()->path->id;

    const SceneGeometry pathOnly = SceneEvaluator::evaluate(page, 1);
    const SceneObjectGeometry* pathOnlyObject =
        pathOnly.objectById(QStringLiteral("path-pipeline-object"));
    QVERIFY(pathOnlyObject);
    QVERIFY(!pathOnlyObject->geometry.pieces.isEmpty());
    const QByteArray pathSignature = geometrySignature(pathOnlyObject->geometry);

    TextObject& finalObject = *page.layers.front()->objects.front();
    auto wave = std::make_unique<WaveEffect>();
    wave->amplitude = 0.35;
    wave->frequency = 2.0;
    EffectMaskStroke mask;
    mask.points = {pathOnlyObject->geometry.bounds.center()};
    mask.radius = qMax<qreal>(100.0, pathOnlyObject->geometry.bounds.width());
    mask.opacity = 1.0;
    mask.hardness = 0.5;
    wave->maskStrokes.push_back(mask);
    finalObject.effects.append(std::move(wave));

    auto warp = std::make_unique<GeometryWarpEffect>(
        QStringLiteral("waveWarp"), QStringLiteral("Wave Warp"),
        GeometryWarpEffect::Mode::WaveWarp);
    QVERIFY(warp->setParameter(QStringLiteral("amount"), 0.18));
    finalObject.effects.append(std::move(warp));

    auto echo = std::make_unique<TrailEffect>(QStringLiteral("echo"), QStringLiteral("Echo"));
    QVERIFY(echo->setParameter(QStringLiteral("copyCount"), 2.0));
    finalObject.effects.append(std::move(echo));

    DeformationStroke deformation;
    deformation.mode = BrushMode::Push;
    deformation.target = BrushTarget::Shape;
    deformation.coordinateSpace = DeformationCoordinateSpace::ObjectLocal;
    deformation.radius = qMax<qreal>(80.0, pathOnlyObject->geometry.bounds.height() * 2.0);
    deformation.strength = 0.9;
    deformation.hardness = 0.4;
    const QPointF center = pathOnlyObject->geometry.bounds.center();
    deformation.samples = {{center - QPointF(20.0, 0.0), QPointF(), 1.0},
                            {center, QPointF(20.0, 0.0), 1.0},
                            {center + QPointF(20.0, 0.0), QPointF(20.0, 0.0), 1.0}};
    finalObject.deformation.enabled = true;
    finalObject.deformation.strokes.push_back(deformation);

    const SceneGeometry finalScene = SceneEvaluator::evaluate(page, 2);
    const SceneObjectGeometry* finalSceneObject =
        finalScene.objectById(QStringLiteral("path-pipeline-object"));
    QVERIFY(finalSceneObject);
    QVERIFY(finalSceneObject->geometry.pieces.size() > pathOnlyObject->geometry.pieces.size());
    QVERIFY(geometrySignature(finalSceneObject->geometry) != pathSignature);
    QVERIFY(finalSceneObject->geometry.bounds != pathOnlyObject->geometry.bounds);

    Document exportDocument;
    VectorExportPayload payload;
    QString exportError;
    QVERIFY2(ExportPayloadBuilder::build(
                 exportDocument, page, finalScene, ExportScope::CurrentPage,
                 {}, &payload, &exportError), qPrintable(exportError));
    QCOMPARE(payload.plainText, base.sourceText);
    QVERIFY(!payload.records.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString svgPath = directory.filePath(QStringLiteral("path-pipeline.svg"));
    SvgExporter exporter;
    QVERIFY2(exporter.exportPayload(payload, svgPath, &exportError), qPrintable(exportError));
    QFile svg(svgPath);
    QVERIFY(svg.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray svgBytes = svg.readAll();
    QVERIFY(svgBytes.contains("<path"));
    QVERIFY(!svgBytes.contains("textPath"));
}

void CoreTests::pathLayoutHonorsCancellationBudget()
{
    ShapedText shaped;
    shaped.lineBounds = {QRectF(0.0, 0.0, 100.0, 20.0)};
    GeometryPiece piece;
    piece.path.addRect(QRectF(0.0, -10.0, 10.0, 10.0));
    piece.anchor = QPointF();
    piece.originalAnchor = piece.anchor;
    piece.layoutOrigin = piece.anchor;
    piece.layoutAdvance = 10.0;
    piece.effectReferenceProgress = 0.25;
    piece.hasEffectReferenceProgress = true;
    VectorGeometry geometry;
    geometry.pieces.push_back(piece);
    piece.path = QPainterPath();
    piece.path.addRect(QRectF(20.0, -10.0, 10.0, 10.0));
    piece.anchor = QPointF(20.0, 0.0);
    piece.originalAnchor = piece.anchor;
    piece.layoutOrigin = piece.anchor;
    geometry.pieces.push_back(piece);
    geometry.setReferenceBounds(QRectF(0.0, -10.0, 10.0, 10.0));
    geometry.recomputeBounds();
    const QByteArray before = geometrySignature(geometry);
    const PathGeometry path = PathGeometry::makeDefault(100.0);
    PathTypographyProperties settings;
    settings.enabled = true;
    settings.pathId = path.id;
    QString error;
    WorkControl work = WorkControl::unlimited();
    work.setCheckpointCallback([work](qint64 consumed) {
        if (consumed >= 4) {
            work.cancel();
        }
    });
    QVERIFY(!PathLayoutEngine::apply(&geometry, shaped, path, settings, &error, work));
    QCOMPARE(geometrySignature(geometry), before);
}

void CoreTests::pathCancellationThresholdsDoNotPoisonPathStageCaches()
{
    TextObject fixture = configuredText(QString(96, QLatin1Char('W')));
    fixture.id = QStringLiteral("path-cache-threshold-object");
    fixture.path = PathGeometry::makeDefault(3600.0, 0.0);
    fixture.path->nodes.front().hasOutgoingHandle = true;
    fixture.path->nodes.front().outgoingHandle = QPointF(520.0, 420.0);
    fixture.path->nodes.back().hasIncomingHandle = true;
    fixture.path->nodes.back().incomingHandle = QPointF(3080.0, 420.0);
    fixture.pathLayout.enabled = true;
    fixture.pathLayout.pathId = fixture.path->id;
    fixture.pathLayout.baselineOffset = 11.0;
    auto procedural = std::make_unique<ProceduralEffect>(
        QStringLiteral("path-cache-procedural"), QStringLiteral("Path cache procedural"),
        ProceduralEffect::Mode::VerticalSpread);
    QVERIFY(procedural->setParameter(QStringLiteral("strength"), 0.8));
    fixture.effects.append(std::move(procedural));

    auto pageFor = [](const TextObject& source) {
        Page page;
        page.id = QStringLiteral("path-cache-page");
        page.layers.front()->id = QStringLiteral("path-cache-layer");
        page.layers.front()->objects.push_back(std::make_unique<TextObject>(source));
        return page;
    };

    SceneEvaluator::invalidateFontCaches();
    const WorkControl baselineWork = WorkControl::unlimited();
    const SceneGeometry baseline = SceneEvaluator::evaluate(
        pageFor(fixture), 201, baselineWork);
    QCOMPARE(baseline.evaluationStatus, EvaluationStatus::Complete);
    QCOMPARE(baseline.objects.size(), 1);
    QVERIFY(baselineWork.unitsConsumed() > 32);
    const test::GeometrySignature expected = test::geometrySignature(
        baseline.objects.front().geometry);

    // The worker's TextEngine intentionally reuses a shaped result across
    // object IDs. Measure the warm, path-cold workload used by the threshold
    // loop so the cancellation checkpoints remain semantic rather than tied
    // to the one-time cold shaping cost of the baseline evaluation.
    TextObject warmFixture = fixture;
    warmFixture.id = QStringLiteral("path-cache-threshold-warmup");
    const WorkControl warmWork = WorkControl::unlimited();
    const SceneGeometry warm = SceneEvaluator::evaluate(
        pageFor(warmFixture), 202, warmWork);
    QCOMPARE(warm.evaluationStatus, EvaluationStatus::Complete);
    QCOMPARE(warm.objects.size(), 1);
    QVERIFY(warmWork.unitsConsumed() > 32);

    QVector<qint64> checkpoints = {
        1,
        2,
        qMax<qint64>(3, warmWork.unitsConsumed() / 4),
        qMax<qint64>(4, warmWork.unitsConsumed() / 2),
        qMax<qint64>(5, warmWork.unitsConsumed() - 1),
    };
    std::sort(checkpoints.begin(), checkpoints.end());
    checkpoints.erase(std::unique(checkpoints.begin(), checkpoints.end()), checkpoints.end());

    for (int checkpointIndex = 0; checkpointIndex < checkpoints.size(); ++checkpointIndex) {
        const qint64 checkpoint = checkpoints.at(checkpointIndex);
        TextObject attempt = fixture;
        attempt.id = QStringLiteral("path-cache-threshold-attempt-%1").arg(checkpointIndex);
        const WorkControl interrupted = WorkControl::withBudget();
        interrupted.setCheckpointCallback(
            [&interrupted, checkpoint](qint64 consumed) {
                if (consumed >= checkpoint) interrupted.cancel();
            });
        const SceneGeometry partial = SceneEvaluator::evaluate(
            pageFor(attempt), 202, interrupted);
        QCOMPARE(partial.evaluationStatus, EvaluationStatus::Cancelled);
        QVERIFY(partial.objects.isEmpty());

        const WorkControl recoveredWork = WorkControl::unlimited();
        const SceneGeometry recovered = SceneEvaluator::evaluate(
            pageFor(attempt), 203, recoveredWork);
        QCOMPARE(recovered.evaluationStatus, EvaluationStatus::Complete);
        QCOMPARE(recovered.objects.size(), 1);
        QString difference;
        QVERIFY2(test::compareGeometry(
                     expected, test::geometrySignature(recovered.objects.front().geometry),
                     &difference),
                 qPrintable(QStringLiteral("path cache recovery after threshold %1: %2")
                                .arg(checkpoint).arg(difference)));
    }

    // A path-only key change must rebuild the path stage while retaining the
    // shaped/base stages; compare it with a cold worker result and retain the
    // work-unit signal as an independent cache-preservation assertion.
    Page changedPath = pageFor(fixture);
    changedPath.layers.front()->objects.front()->path->nodes.front().anchor
        += QPointF(35.0, -20.0);
    const WorkControl changedPathWork = WorkControl::unlimited();
    const SceneGeometry changedPathScene = SceneEvaluator::evaluate(
        changedPath, 204, changedPathWork);
    QCOMPARE(changedPathScene.evaluationStatus, EvaluationStatus::Complete);
    SceneEvaluator::invalidateFontCaches();
    const WorkControl coldPathWork = WorkControl::unlimited();
    const SceneGeometry coldPathScene = SceneEvaluator::evaluate(
        changedPath, 205, coldPathWork);
    QCOMPARE(coldPathScene.evaluationStatus, EvaluationStatus::Complete);
    QString difference;
    QVERIFY2(test::compareGeometry(
                 test::geometrySignature(coldPathScene.objects.front().geometry),
                 test::geometrySignature(changedPathScene.objects.front().geometry),
                 &difference),
             qPrintable(difference));
    QVERIFY(changedPathWork.unitsConsumed() < coldPathWork.unitsConsumed());

    // An effect-only key change must reuse the completed path stage and still
    // produce the same final geometry as a cold evaluation.
    Page changedEffect = pageFor(fixture);
    auto extraWave = std::make_unique<WaveEffect>();
    extraWave->amplitude = 0.23;
    extraWave->phase = 0.19;
    changedEffect.layers.front()->objects.front()->effects.append(std::move(extraWave));
    const WorkControl changedEffectWork = WorkControl::unlimited();
    const SceneGeometry changedEffectScene = SceneEvaluator::evaluate(
        changedEffect, 206, changedEffectWork);
    QCOMPARE(changedEffectScene.evaluationStatus, EvaluationStatus::Complete);
    SceneEvaluator::invalidateFontCaches();
    const WorkControl coldEffectWork = WorkControl::unlimited();
    const SceneGeometry coldEffectScene = SceneEvaluator::evaluate(
        changedEffect, 207, coldEffectWork);
    QCOMPARE(coldEffectScene.evaluationStatus, EvaluationStatus::Complete);
    QVERIFY2(test::compareGeometry(
                 test::geometrySignature(coldEffectScene.objects.front().geometry),
                 test::geometrySignature(changedEffectScene.objects.front().geometry),
                 &difference),
             qPrintable(difference));
    QVERIFY(changedEffectWork.unitsConsumed() < coldEffectWork.unitsConsumed());
}

void CoreTests::pathControllerDuplicateAndStaleGestureKeepIdentitySafe()
{
    EditorController controller;
    const QString originalId = controller.createTextObject(QPointF(50.0, 50.0), QStringLiteral("Path"));
    QVERIFY(!originalId.isEmpty());
    controller.setPathLayoutEnabled(true);
    const TextObject* original = controller.document().objectById(originalId);
    QVERIFY(original && original->path.has_value());
    const QString originalPathId = original->path->id;
    const QString originalNodeId = original->path->nodes.front().id;
    const QPointF originalAnchor = original->path->nodes.front().anchor;
    const quint64 oldRevision = controller.spatialRevision();

    PathGeometry stalePath = *original->path;
    stalePath.nodes.front().anchor += QPointF(30.0, 5.0);
    controller.setText(QStringLiteral("Changed"));
    controller.setPathGeometry(originalId, stalePath, oldRevision);
    const TextObject* afterStale = controller.document().objectById(originalId);
    QVERIFY(afterStale && afterStale->path.has_value());
    QCOMPARE(afterStale->path->nodes.front().anchor, originalAnchor);

    controller.selectObject(originalId);
    controller.duplicateSelectedObjects();
    const QStringList selected = controller.selectedObjectIds();
    QCOMPARE(selected.size(), 1);
    const QString duplicateId = selected.last();
    const TextObject* duplicate = controller.document().objectById(duplicateId);
    QVERIFY(duplicate && duplicate->path.has_value());
    QVERIFY(duplicate->path->id != originalPathId);
    QVERIFY(duplicate->path->nodes.front().id != originalNodeId);
    QCOMPARE(duplicate->pathLayout.pathId, duplicate->path->id);
}

void CoreTests::pathSerializationRejectsCorruptionAndBudgets()
{
    Document document;
    TextObject& object = document.primaryTextObject();
    object.sourceText = QStringLiteral("serialized path");
    object.path = PathGeometry::makeDefault(240.0);
    object.pathLayout.enabled = true;
    object.pathLayout.pathId = object.path->id;
    const QJsonObject original = ProjectSerializer::toJson(document).object();

    const auto pagesWithObject = [&original](const QJsonObject& serializedObject) {
        QJsonObject root = original;
        QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
        QJsonObject page = pages.at(0).toObject();
        QJsonArray layers = page.value(QStringLiteral("layers")).toArray();
        QJsonObject layer = layers.at(0).toObject();
        QJsonArray objects = layer.value(QStringLiteral("objects")).toArray();
        objects.replace(0, serializedObject);
        layer.insert(QStringLiteral("objects"), objects);
        layers.replace(0, layer);
        page.insert(QStringLiteral("layers"), layers);
        pages.replace(0, page);
        root.insert(QStringLiteral("pages"), pages);
        return root;
    };
    const QJsonObject serializedObject = original.value(QStringLiteral("pages"))
        .toArray().at(0).toObject().value(QStringLiteral("layers"))
        .toArray().at(0).toObject().value(QStringLiteral("objects"))
        .toArray().at(0).toObject();

    QJsonObject duplicatePathObject = serializedObject;
    duplicatePathObject.insert(QStringLiteral("id"), QStringLiteral("second-object"));
    QJsonObject path = duplicatePathObject.value(QStringLiteral("path")).toObject();
    path.insert(QStringLiteral("id"), object.path->id);
    duplicatePathObject.insert(QStringLiteral("path"), path);
    QJsonObject duplicateRoot = pagesWithObject(serializedObject);
    QJsonArray pages = duplicateRoot.value(QStringLiteral("pages")).toArray();
    QJsonObject page = pages.at(0).toObject();
    QJsonArray layers = page.value(QStringLiteral("layers")).toArray();
    QJsonObject layer = layers.at(0).toObject();
    QJsonArray objects = layer.value(QStringLiteral("objects")).toArray();
    objects.append(duplicatePathObject);
    layer.insert(QStringLiteral("objects"), objects);
    layers.replace(0, layer);
    page.insert(QStringLiteral("layers"), layers);
    pages.replace(0, page);
    duplicateRoot.insert(QStringLiteral("pages"), pages);
    QString error;
    Document rejected;
    QVERIFY(!ProjectSerializer::fromJson(QJsonDocument(duplicateRoot), &rejected, &error));
    QVERIFY(error.contains(QStringLiteral("duplicate path ID")));

    QJsonObject duplicateNodeObject = serializedObject;
    path = duplicateNodeObject.value(QStringLiteral("path")).toObject();
    QJsonArray nodes = path.value(QStringLiteral("nodes")).toArray();
    QJsonObject secondNode = nodes.at(1).toObject();
    secondNode.insert(QStringLiteral("id"), nodes.at(0).toObject().value(QStringLiteral("id")));
    nodes.replace(1, secondNode);
    path.insert(QStringLiteral("nodes"), nodes);
    duplicateNodeObject.insert(QStringLiteral("path"), path);
    const QJsonObject duplicateNodeRoot = pagesWithObject(duplicateNodeObject);
    QVERIFY(!ProjectSerializer::fromJson(QJsonDocument(duplicateNodeRoot), &rejected, &error));
    QVERIFY(error.contains(QStringLiteral("duplicate node ID")));

    QJsonObject invalidLayoutObject = serializedObject;
    QJsonObject layout = invalidLayoutObject.value(QStringLiteral("pathLayout")).toObject();
    layout.insert(QStringLiteral("overflow"), QStringLiteral("wrap"));
    invalidLayoutObject.insert(QStringLiteral("pathLayout"), layout);
    QVERIFY(!ProjectSerializer::fromJson(
        QJsonDocument(pagesWithObject(invalidLayoutObject)), &rejected, &error));
    QVERIFY(error.contains(QStringLiteral("overflow")));

    QJsonObject wrongTypeLayoutObject = serializedObject;
    layout = wrongTypeLayoutObject.value(QStringLiteral("pathLayout")).toObject();
    layout.insert(QStringLiteral("enabled"), QStringLiteral("true"));
    wrongTypeLayoutObject.insert(QStringLiteral("pathLayout"), layout);
    QVERIFY(!ProjectSerializer::fromJson(
        QJsonDocument(pagesWithObject(wrongTypeLayoutObject)), &rejected, &error));
    QVERIFY(error.contains(QStringLiteral("JSON type")));

    QJsonObject wrongTypePathObject = serializedObject;
    path = wrongTypePathObject.value(QStringLiteral("path")).toObject();
    path.insert(QStringLiteral("closed"), QStringLiteral("false"));
    wrongTypePathObject.insert(QStringLiteral("path"), path);
    QVERIFY(!ProjectSerializer::fromJson(
        QJsonDocument(pagesWithObject(wrongTypePathObject)), &rejected, &error));
    QVERIFY(error.contains(QStringLiteral("closed")));

    QJsonObject missingPathObject = serializedObject;
    missingPathObject.remove(QStringLiteral("path"));
    QVERIFY(!ProjectSerializer::fromJson(
        QJsonDocument(pagesWithObject(missingPathObject)), &rejected, &error));
    QVERIFY(error.contains(QStringLiteral("missing")));

    ProjectResourceLimits limits = ProjectSerializer::resourceLimits();
    limits.maximumPathNodesPerPath = 1;
    QVERIFY(!ProjectSerializer::validateResourceBudget(
        QJsonDocument(QJsonArray{serializedObject}), limits, &error));
    QVERIFY(error.contains(QStringLiteral("path nodes per path")));

    // Exact aggregate/per-entry limits are admissible; the first value beyond
    // each boundary is rejected without allowing a path to consume a hidden
    // extra unit of resource budget.
    const ProjectResourceLimits exact = ProjectSerializer::resourceLimits();
    QVERIFY(ProjectSerializer::validateResourceBudget(
        QJsonDocument(QJsonArray{serializedObject}), exact, &error));
    ProjectResourceLimits exactPathNodes = exact;
    exactPathNodes.maximumPathNodesPerPath = 2;
    exactPathNodes.maximumPathNodes = 2;
    exactPathNodes.maximumPaths = 1;
    QVERIFY(ProjectSerializer::validateResourceBudget(
        QJsonDocument(QJsonArray{serializedObject}), exactPathNodes, &error));
    exactPathNodes.maximumPathNodes = 1;
    QVERIFY(!ProjectSerializer::validateResourceBudget(
        QJsonDocument(QJsonArray{serializedObject}), exactPathNodes, &error));
    QVERIFY(error.contains(QStringLiteral("aggregate path nodes")));
    exactPathNodes = exact;
    exactPathNodes.maximumPaths = 0;
    QVERIFY(!ProjectSerializer::validateResourceBudget(
        QJsonDocument(QJsonArray{serializedObject}), exactPathNodes, &error));
    QVERIFY(error.contains(QStringLiteral("aggregate paths")));
    exactPathNodes = exact;
    exactPathNodes.maximumPathNodesPerPath = 2;
    exactPathNodes.maximumPathWork = 2;
    QVERIFY(ProjectSerializer::validateResourceBudget(
        QJsonDocument(QJsonArray{serializedObject}), exactPathNodes, &error));
    exactPathNodes.maximumPathWork = 1;
    QVERIFY(!ProjectSerializer::validateResourceBudget(
        QJsonDocument(QJsonArray{serializedObject}), exactPathNodes, &error));
    QVERIFY(error.contains(QStringLiteral("path work")));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString filePath = directory.filePath(QStringLiteral("sentinel.vtp"));
    QFile sentinel(filePath);
    QVERIFY(sentinel.open(QIODevice::WriteOnly));
    sentinel.write("sentinel");
    sentinel.close();
    Document invalidDocument;
    invalidDocument.primaryTextObject().path = PathGeometry::makeDefault(100.0);
    invalidDocument.primaryTextObject().pathLayout.pathId =
        invalidDocument.primaryTextObject().path->id;
    invalidDocument.primaryTextObject().path->nodes[1].id =
        invalidDocument.primaryTextObject().path->nodes[0].id;
    QVERIFY(!ProjectSerializer::saveToFile(invalidDocument, filePath, &error));
    QFile preserved(filePath);
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    QCOMPARE(preserved.readAll(), QByteArrayLiteral("sentinel"));
}

void CoreTests::pathUndoTargetsExplicitObjectAndRestoresFingerprint()
{
    EditorController controller;
    const QString firstId = controller.createTextObject(QPointF(40.0, 40.0), QStringLiteral("first"));
    QVERIFY(!firstId.isEmpty());
    controller.setPathLayoutEnabled(true);
    const TextObject* first = controller.document().objectById(firstId);
    QVERIFY(first && first->path.has_value());
    const QString secondId = controller.createTextObject(QPointF(260.0, 40.0), QStringLiteral("second"));
    QVERIFY(!secondId.isEmpty());
    QVERIFY(controller.activeObject());
    const QString before = test::semanticFingerprint(controller.document());

    PathGeometry candidate = *first->path;
    candidate.nodes.front().anchor += QPointF(18.0, 11.0);
    controller.setPathGeometry(firstId, candidate, controller.spatialRevision());
    QCOMPARE(controller.document().objectById(firstId)->path->nodes.front().anchor,
             candidate.nodes.front().anchor);
    QCOMPARE(controller.document().objectById(secondId)->path.has_value(), false);
    const QString changed = test::semanticFingerprint(controller.document());
    QVERIFY(changed != before);

    controller.undoStack()->undo();
    QCOMPARE(test::semanticFingerprint(controller.document()), before);
    controller.undoStack()->redo();
    QCOMPARE(test::semanticFingerprint(controller.document()), changed);
}

void CoreTests::projectV1TrackingMigrates()
{
    Document original;
    original.primaryTextObject() = configuredText(QStringLiteral("Migration"));
    QJsonObject root = ProjectSerializer::toJson(original).object();
    root.insert(QStringLiteral("formatVersion"), 1);

    QJsonArray objects = root.value(QStringLiteral("objects")).toArray();
    QJsonObject textObject = objects.at(0).toObject();
    QJsonObject typography = textObject.value(QStringLiteral("typography")).toObject();
    typography.remove(QStringLiteral("trackingEm"));
    typography.remove(QStringLiteral("trackingUnit"));
    typography.insert(QStringLiteral("tracking"), 8.0);
    typography.insert(QStringLiteral("fontSize"), 80.0);
    textObject.insert(QStringLiteral("typography"), typography);
    objects.replace(0, textObject);
    root.insert(QStringLiteral("objects"), objects);

    Document migrated;
    QString error;
    QVERIFY(ProjectSerializer::fromJson(QJsonDocument(root), &migrated, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(migrated.formatVersion, Document::CurrentFormatVersion);
    QCOMPARE(migrated.primaryTextObject().typography.trackingEm, 0.1);
}

void CoreTests::projectV2DeformationDefaults()
{
    Document original;
    original.primaryTextObject() = configuredText(QStringLiteral("Phase 1"));
    QJsonObject root = ProjectSerializer::toJson(original).object();
    root.insert(QStringLiteral("formatVersion"), 2);
    QJsonArray objects = root.value(QStringLiteral("objects")).toArray();
    QJsonObject textObject = objects.at(0).toObject();
    textObject.remove(QStringLiteral("deformation"));
    objects.replace(0, textObject);
    root.insert(QStringLiteral("objects"), objects);

    Document migrated;
    QString error;
    QVERIFY2(ProjectSerializer::fromJson(QJsonDocument(root), &migrated, &error), qPrintable(error));
    QCOMPARE(migrated.formatVersion, Document::CurrentFormatVersion);
    QVERIFY(migrated.primaryTextObject().deformation.enabled);
    QCOMPARE(migrated.primaryTextObject().deformation.strength, 1.0);
    QVERIFY(migrated.primaryTextObject().deformation.strokes.isEmpty());
}

void CoreTests::presetSerializationRoundTrip()
{
    Preset original;
    original.id = QStringLiteral("7b1ce63d-1111-4111-8111-111111111111");
    original.name = QStringLiteral("Unstable");
    original.effects.append(std::make_unique<WaveEffect>());
    original.effects.append(std::make_unique<StretchEffect>());
    auto* stretch = dynamic_cast<StretchEffect*>(original.effects.at(1));
    QVERIFY(stretch);
    stretch->horizontal = 1.8;
    stretch->vertical = 0.75;

    Preset restored;
    QString error;
    QVERIFY(Preset::fromJson(original.toJson(), &restored, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(restored.id, original.id);
    QCOMPARE(restored.name, original.name);
    QCOMPARE(restored.effects.size(), 2);
    const auto* restoredStretch = dynamic_cast<const StretchEffect*>(restored.effects.at(1));
    QVERIFY(restoredStretch);
    QCOMPARE(restoredStretch->horizontal, 1.8);
    QCOMPARE(restoredStretch->vertical, 0.75);
}

void CoreTests::unicodePresetStorageIsCollisionSafe()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PresetManager manager(directory.path());

    const QStringList names = {
        QStringLiteral("\u0411\u0435\u0437\u0434\u043d\u0430"),
        QStringLiteral("\u041f\u0430\u043d\u0438\u043a\u0430"),
        QStringLiteral("\u0422\u0443\u043c\u0430\u043d"),
        QStringLiteral("\u0428\u0451\u043f\u043e\u0442"),
        QStringLiteral("\u0418\u0441\u043a\u0430\u0436\u0435\u043d\u0438\u0435 \u0440\u0435\u0430\u043b\u044c\u043d\u043e\u0441\u0442\u0438"),
    };
    QCOMPARE(names.at(2).size(), names.at(3).size());
    for (const QString& name : names) {
        Preset preset;
        preset.name = name;
        preset.effects.append(std::make_unique<WaveEffect>());
        QString error;
        QVERIFY2(manager.savePreset(preset, &error), qPrintable(error));
    }

    const QStringList listedNames = manager.listPresetNames();
    for (const QString& name : names) {
        QVERIFY(listedNames.contains(name));
    }
    QCOMPARE(QDir(directory.path()).entryList({QStringLiteral("*.json")}, QDir::Files).size(), names.size());

    const QVector<PresetInfo> infos = manager.listPresets();
    QCOMPARE(infos.size(), names.size());
    QSet<QString> ids;
    for (const PresetInfo& info : infos) {
        QVERIFY(!info.id.isEmpty());
        QVERIFY(!QUuid::fromString(info.id).isNull());
        QVERIFY(!ids.contains(info.id));
        ids.insert(info.id);

        Preset loaded;
        QString error;
        QVERIFY2(manager.loadPresetById(info.id, &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.id, info.id);
        QCOMPARE(loaded.name, info.name);
    }

    Preset loadedByName;
    QString error;
    QVERIFY2(manager.loadPreset(names.at(0), &loadedByName, &error), qPrintable(error));
    QCOMPARE(loadedByName.name, names.at(0));
    QVERIFY2(manager.deletePreset(names.at(1), &error), qPrintable(error));
    QVERIFY2(manager.deletePresetById(loadedByName.id, &error), qPrintable(error));
    QVERIFY(!manager.listPresetNames().contains(names.at(0)));
    QVERIFY(!manager.listPresetNames().contains(names.at(1)));
    QVERIFY(manager.listPresetNames().contains(names.at(2)));
    QVERIFY(manager.listPresetNames().contains(names.at(3)));
    QVERIFY(manager.listPresetNames().contains(names.at(4)));
}

void CoreTests::deterministicJitter()
{
    const TextObject object = configuredText();
    VectorGeometry first = baseGeometry(object);
    VectorGeometry second = first;

    GlyphJitterEffect jitter;
    jitter.amount = 0.08;
    jitter.seed = 42;
    const EffectContext context{first.referenceBounds, first.referenceHeight};
    jitter.apply(first, context);
    jitter.apply(second, context);
    QCOMPARE(geometrySignature(first), geometrySignature(second));

    VectorGeometry different = baseGeometry(object);
    jitter.seed = 43;
    jitter.apply(different, context);
    QVERIFY(geometrySignature(first) != geometrySignature(different));
}

void CoreTests::effectOrderingIsDeterministic()
{
    const TextObject object = configuredText();
    EffectStack stack;
    stack.append(std::make_unique<WaveEffect>());
    stack.append(std::make_unique<GlyphJitterEffect>());
    stack.append(std::make_unique<StretchEffect>());
    auto* wave = dynamic_cast<WaveEffect*>(stack.at(0));
    auto* jitter = dynamic_cast<GlyphJitterEffect*>(stack.at(1));
    auto* stretch = dynamic_cast<StretchEffect*>(stack.at(2));
    QVERIFY(wave);
    QVERIFY(jitter);
    QVERIFY(stretch);
    wave->amplitude = 0.24;
    wave->frequency = 2.5;
    jitter->amount = 0.08;
    jitter->seed = 42;
    stretch->horizontal = 1.6;
    stretch->vertical = 0.75;

    VectorGeometry first = baseGeometry(object);
    VectorGeometry second = baseGeometry(object);
    stack.apply(first);
    stack.apply(second);
    QCOMPARE(geometrySignature(first), geometrySignature(second));

    EffectStack reordered = stack;
    reordered.move(0, 2);
    VectorGeometry changed = baseGeometry(object);
    reordered.apply(changed);
    QVERIFY(geometrySignature(first) != geometrySignature(changed));
}

void CoreTests::geometrySourceMetadataStaysImmutableUnderEffectsAndTransforms()
{
    const VectorGeometry original = rectangleGeometry();
    VectorGeometry geometry = original;
    StretchEffect stretch;
    stretch.horizontal = 1.7;
    stretch.vertical = 0.6;
    stretch.apply(geometry, {geometry.referenceBounds, geometry.referenceHeight});

    QTransform pageTransform;
    pageTransform.translate(80.0, -35.0);
    pageTransform.rotate(23.0);
    geometry.transformAll(pageTransform);

    QCOMPARE(geometry.referenceBounds, original.referenceBounds);
    for (int index = 0; index < geometry.pieces.size(); ++index) {
        QCOMPARE(geometry.pieces.at(index).originalAnchor,
                 original.pieces.at(index).originalAnchor);
    }

    WaveEffect wave;
    wave.amplitude = 0.2;
    wave.apply(geometry, {geometry.referenceBounds, geometry.referenceHeight});
    QCOMPARE(geometry.referenceBounds, original.referenceBounds);
    for (int index = 0; index < geometry.pieces.size(); ++index) {
        QCOMPARE(geometry.pieces.at(index).originalAnchor,
                 original.pieces.at(index).originalAnchor);
    }
}

void CoreTests::fontTraitsInvalidateSceneShapingAndProduceVectorDecorations()
{
    TextObject object = configuredText(QStringLiteral("Decorated"));
    const QByteArray regularKey = SceneEvaluator::shapingCacheKey(object);
    object.font.italic = true;
    QVERIFY(SceneEvaluator::shapingCacheKey(object) != regularKey);

    TextEngine engine;
    const ShapedText shaped = engine.shape(object);
    const VectorGeometry plain = GlyphGeometryBuilder::build(shaped, object.typography.fontSize);
    const VectorGeometry decorated = GlyphGeometryBuilder::build(shaped,
                                                                   object.typography.fontSize,
                                                                   true,
                                                                   true);
    QVERIFY(decorated.pieces.size() >= plain.pieces.size() + 2);
    QVERIFY(geometrySignature(decorated) != geometrySignature(plain));
}

void CoreTests::masterStrengthSupportsAmplificationAndRoundTrip()
{
    VectorGeometry normal = rectangleGeometry();
    VectorGeometry amplified = normal;
    WaveEffect effect;
    effect.amplitude = 0.25;
    effect.masterStrength = 1.0;
    effect.apply(normal, {normal.referenceBounds, normal.referenceHeight});
    effect.masterStrength = 2.5;
    effect.apply(amplified, {amplified.referenceBounds, amplified.referenceHeight});
    QVERIFY(geometrySignature(normal) != geometrySignature(amplified));

    EffectStack stack;
    auto stored = std::make_unique<WaveEffect>();
    stored->masterStrength = 2.5;
    stack.append(std::move(stored));
    QString error;
    EffectStack restored = EffectStack::fromJson(stack.toJson(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(restored.at(0)->masterStrength, 2.5);

    for (const qreal strength : {0.0, 0.5, 1.0, 2.0, 3.0}) {
        VectorGeometry stretched = rectangleGeometry();
        StretchEffect stretch;
        stretch.horizontal = 0.1;
        stretch.vertical = 0.2;
        stretch.masterStrength = strength;
        stretch.apply(stretched, {stretched.referenceBounds, stretched.referenceHeight});
        const QRectF bounds = stretched.bounds;
        QVERIFY(std::isfinite(bounds.width()));
        QVERIFY(std::isfinite(bounds.height()));
        QVERIFY(bounds.width() > 0.0);
        QVERIFY(bounds.height() > 0.0);
    }
}

void CoreTests::missingUndoTargetDoesNotRedirectToAnotherObject()
{
    Document document;
    Layer* layer = document.activeLayer();
    QVERIFY(layer);
    auto first = std::make_unique<TextObject>();
    first->id = QStringLiteral("first");
    first->sourceText = QStringLiteral("A stays unchanged");
    auto second = std::make_unique<TextObject>();
    second->id = QStringLiteral("second");
    second->sourceText = QStringLiteral("B");
    layer->objects.push_back(std::move(first));
    layer->objects.push_back(std::move(second));
    document.activeObjectId = QStringLiteral("second");

    SetTextCommand command(document, QStringLiteral("B"), QStringLiteral("changed"), {});
    command.redo();
    QCOMPARE(document.objectById(QStringLiteral("second"))->sourceText, QStringLiteral("changed"));
    layer->objects.erase(layer->objects.begin() + 1);
    document.activeObjectId = QStringLiteral("first");
    command.undo();

    QCOMPARE(document.objectById(QStringLiteral("first"))->sourceText,
             QStringLiteral("A stays unchanged"));
}

void CoreTests::ambiguousLegacyDeformationStrokeIsSkipped()
{
    const VectorGeometry original = rectangleGeometry();
    VectorGeometry geometry = original;
    ManualDeformation deformation;
    DeformationStroke stroke = pushStroke();
    stroke.coordinateSpace = DeformationCoordinateSpace::LegacyPageAmbiguous;
    deformation.strokes.push_back(stroke);
    deformation.apply(geometry);
    QCOMPARE(geometrySignature(geometry), geometrySignature(original));
}

void CoreTests::effectMasksAttenuateGeometry()
{
    const VectorGeometry original = rectangleGeometry();
    EffectStack stack;
    auto stretch = std::make_unique<StretchEffect>();
    stretch->horizontal = 2.0;
    stretch->vertical = 1.0;
    EffectMaskStroke mask;
    mask.points = {QPointF(10.0, 10.0)};
    mask.radius = 18.0;
    mask.opacity = 1.0;
    mask.hardness = 1.0;
    stretch->maskStrokes.push_back(mask);
    stack.append(std::move(stretch));

    VectorGeometry masked = original;
    stack.apply(masked);
    QCOMPARE(firstPathElement(masked.pieces.at(0).path),
             firstPathElement(original.pieces.at(0).path));
    QVERIFY(firstPathElement(masked.pieces.at(1).path)
            != firstPathElement(original.pieces.at(1).path));
}

void CoreTests::textReplacementPreservesEffects()
{
    Document document;
    document.primaryTextObject().effects.append(std::make_unique<WaveEffect>());
    document.primaryTextObject().effects.append(std::make_unique<GlyphJitterEffect>());
    document.primaryTextObject().deformation.strokes.push_back(pushStroke());
    const QJsonArray before = document.primaryTextObject().effects.toJson();
    document.primaryTextObject().sourceText = QStringLiteral("Другой текст");
    QCOMPARE(document.primaryTextObject().effects.toJson(), before);
    QCOMPARE(document.primaryTextObject().deformation.strokes.size(), 1);
}

void CoreTests::presetApplicationClonesEffects()
{
    Preset preset;
    preset.name = QStringLiteral("Copy me");
    preset.effects.append(std::make_unique<WaveEffect>());

    Document document;
    document.primaryTextObject().effects = preset.effects;
    auto* presetWave = dynamic_cast<WaveEffect*>(preset.effects.at(0));
    auto* documentWave = dynamic_cast<WaveEffect*>(document.primaryTextObject().effects.at(0));
    QVERIFY(presetWave);
    QVERIFY(documentWave);
    presetWave->amplitude = 0.9;
    QVERIFY(!qFuzzyCompare(presetWave->amplitude, documentWave->amplitude));
}

void CoreTests::svgExportContainsPaths()
{
    const TextObject object = configuredText();
    Document document;
    document.primaryTextObject() = object;
    VectorGeometry geometry = baseGeometry(object);
    ManualDeformation deformation;
    deformation.strokes.push_back(pushStroke());
    deformation.apply(geometry);
    SvgExporter exporter;

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString filePath = directory.filePath(QStringLiteral("export.svg"));
    QString error;
    QVERIFY(exporter.exportGeometry(document, geometry, filePath, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray svg = file.readAll();
    QVERIFY(svg.contains("<path"));
    QVERIFY(!svg.contains("<text"));
    QVERIFY(!svg.contains("<image"));
    QVERIFY(!svg.contains("base64"));
}

void CoreTests::portableExportPayloadScopesAndOpacity()
{
    Document document;
    Page page;
    page.size = QSizeF(640.0, 480.0);
    SceneGeometry scene;
    scene.pageSize = page.size;

    SceneObjectGeometry first;
    first.objectId = QStringLiteral("first");
    first.sourceText = QStringLiteral("\u041f\u0440\u0438\u0432\u0435\u0442");
    first.fill = QColor(200, 20, 30, 128);
    GeometryPiece firstPiece;
    firstPiece.path.addRect(QRectF(20.0, 40.0, 30.0, 20.0));
    firstPiece.opacityMultiplier = 0.5;
    first.geometry.pieces.push_back(firstPiece);
    scene.objects.push_back(first);

    SceneObjectGeometry second;
    second.objectId = QStringLiteral("second");
    second.sourceText = QStringLiteral("World");
    second.fill = QColor(20, 40, 200);
    GeometryPiece secondPiece;
    secondPiece.path.addRect(QRectF(100.0, 50.0, 10.0, 10.0));
    second.geometry.pieces.push_back(secondPiece);
    scene.objects.push_back(second);

    VectorExportPayload selection;
    QString error;
    QVERIFY(ExportPayloadBuilder::build(document, page, scene, ExportScope::Selection,
                                        {QStringLiteral("first")}, &selection, &error));
    QCOMPARE(selection.records.size(), 1);
    QCOMPARE(selection.bounds, QRectF(0.0, 0.0, 30.0, 20.0));
    QCOMPARE(selection.records.front().path.boundingRect(), selection.bounds);
    QVERIFY(qAbs(selection.records.front().opacity - 0.25) < 0.001);
    QCOMPARE(selection.plainText, QStringLiteral("\u041f\u0440\u0438\u0432\u0435\u0442"));

    VectorExportPayload pagePayload;
    QVERIFY(ExportPayloadBuilder::build(document, page, scene, ExportScope::CurrentPage,
                                        {}, &pagePayload, &error));
    QCOMPARE(pagePayload.records.size(), 2);
    QCOMPARE(pagePayload.bounds, QRectF(QPointF(), page.size));
    QCOMPARE(pagePayload.plainText, QStringLiteral("\u041f\u0440\u0438\u0432\u0435\u0442\nWorld"));
}

void CoreTests::payloadSvgPreservesRecordsAndWinding()
{
    VectorExportPayload payload;
    payload.bounds = QRectF(0.0, 0.0, 40.0, 40.0);
    VectorExportRecord first;
    first.fill = QColor(220, 10, 20);
    first.opacity = 0.4;
    first.path.addRect(QRectF(0.0, 0.0, 40.0, 40.0));
    first.path.addRect(QRectF(10.0, 10.0, 20.0, 20.0));
    payload.records.push_back(first);
    VectorExportRecord second;
    second.fill = QColor(10, 20, 220);
    second.path.addRect(QRectF(2.0, 2.0, 4.0, 4.0));
    payload.records.push_back(second);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString filePath = directory.filePath(QStringLiteral("payload.svg"));
    QString error;
    SvgExporter exporter;
    QVERIFY2(exporter.exportPayload(payload, filePath, &error), qPrintable(error));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray svg = file.readAll();
    QCOMPARE(svg.count("<path"), 2);
    QVERIFY(svg.contains("fill-opacity=\"0.4"));
    QVERIFY(svg.contains("fill-rule=\"nonzero\""));
    QVERIFY(svg.contains("viewBox=\""));
    QVERIFY(svg.contains("40"));
}

void CoreTests::exportEligibilityDoesNotEvaluateEmptyText()
{
    EditorController controller;
    const QString objectId = controller.createTextObject(QPointF(140.0, 120.0));
    QVERIFY(!objectId.isEmpty());
    // An empty object has no renderable outline.  Eligibility must nevertheless
    // be cheap and true; a call through buildExportPayload would synchronously
    // evaluate and reject it, which is exactly what refreshUi must not do.
    QVERIFY(controller.canExport(ExportScope::Selection));
    QVERIFY(controller.canExport(ExportScope::CurrentPage));
}

void CoreTests::cyrillicTextProducesGeometry()
{
    const QString family = cyrillicFamily();
    if (family.isEmpty()) {
        QSKIP("No installed font advertises Cyrillic support in this environment.");
    }

    TextObject object = configuredText(QStringLiteral("Привет мир"));
    object.sourceText = QStringLiteral("\u041f\u0440\u0438\u0432\u0435\u0442 \u043c\u0438\u0440");
    object.font.family = family;
    object.font.styleName = QFontDatabase::styles(family).value(0);
    TextEngine engine;
    const ShapedText shaped = engine.shape(object);
    QVERIFY2(shaped.error.isEmpty(), qPrintable(shaped.error));
    const VectorGeometry geometry = GlyphGeometryBuilder::build(shaped, object.typography.fontSize);
    QVERIFY(geometry.hasVisibleGeometry());
    QVERIFY(!geometry.combinedPath().isEmpty());
}

void CoreTests::glyphFallbackIsReportedWhenAvailable()
{
    QString fallbackFamily;
    for (const QString& family : QFontDatabase::families()) {
        const QList<QFontDatabase::WritingSystem> systems = QFontDatabase::writingSystems(family);
        if (systems.contains(QFontDatabase::Latin) && !systems.contains(QFontDatabase::Cyrillic)) {
            fallbackFamily = family;
            break;
        }
    }
    if (fallbackFamily.isEmpty()) {
        QSKIP("No installed Latin-only font is available for a deterministic fallback test.");
    }

    TextObject object = configuredText(QStringLiteral("Привет мир"));
    object.sourceText = QStringLiteral("\u041f\u0440\u0438\u0432\u0435\u0442 \u043c\u0438\u0440");
    object.font.family = fallbackFamily;
    object.font.styleName = QFontDatabase::styles(fallbackFamily).value(0);
    TextEngine engine;
    const ShapedText shaped = engine.shape(object);
    if (shaped.fontResolutionStatus != FontResolutionStatus::GlyphFallback) {
        QSKIP("The selected candidate did not produce a glyph-level fallback on this platform.");
    }
    QVERIFY(shaped.requestedFontAvailable);
    QVERIFY(shaped.fallbackGlyphCount > 0);
    QVERIFY(!shaped.fallbackFonts.isEmpty());
    QVERIFY(shaped.warning.contains(QStringLiteral("fallback"), Qt::CaseInsensitive));
    QVERIFY(std::any_of(shaped.glyphs.cbegin(), shaped.glyphs.cend(), [](const ShapedGlyph& glyph) {
        return glyph.usesFallback;
    }));
}

void CoreTests::missingFontStatesAreDistinguished()
{
    TextObject object = configuredText(QStringLiteral("Missing family"));
    object.font.family = QStringLiteral("VectorTypographyDefinitelyMissingFamily");
    object.font.styleName.clear();
    TextEngine engine;
    const ShapedText missingFamily = engine.shape(object);
    QCOMPARE(static_cast<int>(missingFamily.fontResolutionStatus),
             static_cast<int>(FontResolutionStatus::MissingFamily));
    QVERIFY(!missingFamily.requestedFontAvailable);

    const QString family = availableFamily();
    if (family.isEmpty()) {
        QSKIP("No installed family is available for the missing-style check.");
    }
    object.font.family = family;
    object.font.styleName = QStringLiteral("VectorTypographyDefinitelyMissingStyle");
    const ShapedText missingStyle = engine.shape(object);
    QCOMPARE(static_cast<int>(missingStyle.fontResolutionStatus),
             static_cast<int>(FontResolutionStatus::MissingStyle));
    QVERIFY(!missingStyle.requestedFontAvailable);
}

void CoreTests::trackingScalesWithFontSize()
{
    TextObject small = configuredText(QStringLiteral("WWWWWW"));
    small.typography.fontSize = 40.0;
    small.typography.trackingEm = 0.05;
    TextObject large = small;
    large.typography.fontSize = 80.0;

    TextEngine engine;
    const ShapedText smallShape = engine.shape(small);
    const ShapedText largeShape = engine.shape(large);
    QVERIFY(smallShape.logicalBounds.width() > 0.0);
    QVERIFY(largeShape.logicalBounds.width() > smallShape.logicalBounds.width());
    QVERIFY(smallShape.resolvedEmSize > 0.0);
    QVERIFY(largeShape.resolvedEmSize > smallShape.resolvedEmSize);
    const qreal ratio = largeShape.logicalBounds.width() / smallShape.logicalBounds.width();
    QVERIFY2(std::abs(ratio - 2.0) < 0.15, qPrintable(QStringLiteral("tracking ratio was %1").arg(ratio)));

    TextObject smallUntracked = small;
    smallUntracked.typography.trackingEm = 0.0;
    TextObject largeUntracked = large;
    largeUntracked.typography.trackingEm = 0.0;
    const qreal smallDelta = smallShape.logicalBounds.width()
        - engine.shape(smallUntracked).logicalBounds.width();
    const qreal largeDelta = largeShape.logicalBounds.width()
        - engine.shape(largeUntracked).logicalBounds.width();
    const qreal emRatio = largeShape.resolvedEmSize / smallShape.resolvedEmSize;
    QVERIFY(smallDelta > 0.0);
    QVERIFY2(std::abs((largeDelta / smallDelta) - emRatio) < 0.02,
             qPrintable(QStringLiteral("tracking/em ratio was %1/%2")
                            .arg(largeDelta / smallDelta)
                            .arg(emRatio)));
}

void CoreTests::trackingUsesTrueEmDistance()
{
    TextObject narrow = configuredText(QStringLiteral("iiii"));
    narrow.typography.trackingEm = 0.1;
    TextObject narrowUntracked = narrow;
    narrowUntracked.typography.trackingEm = 0.0;

    TextObject wide = configuredText(QStringLiteral("WWWW"));
    wide.typography.trackingEm = 0.1;
    TextObject wideUntracked = wide;
    wideUntracked.typography.trackingEm = 0.0;

    TextEngine engine;
    const ShapedText narrowShape = engine.shape(narrow);
    const ShapedText narrowUntrackedShape = engine.shape(narrowUntracked);
    const ShapedText wideShape = engine.shape(wide);
    const ShapedText wideUntrackedShape = engine.shape(wideUntracked);
    QVERIFY(narrowShape.resolvedEmSize > 0.0);
    QVERIFY(wideShape.resolvedEmSize > 0.0);
    const qreal narrowDelta = narrowShape.logicalBounds.width()
        - narrowUntrackedShape.logicalBounds.width();
    const qreal wideDelta = wideShape.logicalBounds.width()
        - wideUntrackedShape.logicalBounds.width();

    const qreal expectedNarrow = 3.0 * narrowShape.resolvedEmSize * narrow.typography.trackingEm;
    const qreal expectedWide = 3.0 * wideShape.resolvedEmSize * wide.typography.trackingEm;
    QVERIFY2(std::abs(narrowDelta - expectedNarrow) < 0.01,
             qPrintable(QStringLiteral("narrow tracking delta was %1").arg(narrowDelta)));
    QVERIFY2(std::abs(wideDelta - expectedWide) < 0.01,
             qPrintable(QStringLiteral("wide tracking delta was %1").arg(wideDelta)));
    QVERIFY(std::abs(narrowDelta - wideDelta) < 0.01);
}

void CoreTests::deformationSerializationRoundTrip()
{
    Document original;
    original.primaryTextObject() = configuredText(QStringLiteral("Deform me"));
    original.primaryTextObject().deformation.enabled = false;
    original.primaryTextObject().deformation.strength = 1.35;
    original.primaryTextObject().deformation.strokes.push_back(pushStroke(BrushTarget::Shape));

    const QJsonObject textObject = ProjectSerializer::toJson(original)
                                       .object()
                                       .value(QStringLiteral("objects"))
                                       .toArray()
                                       .at(0)
                                       .toObject();
    QVERIFY(textObject.value(QStringLiteral("deformation")).isObject());
    QCOMPARE(ProjectSerializer::toJson(original).object().value(QStringLiteral("formatVersion")).toInt(),
             Document::CurrentFormatVersion);

    Document restored;
    QString error;
    QVERIFY2(ProjectSerializer::fromJson(ProjectSerializer::toJson(original), &restored, &error),
             qPrintable(error));
    const ManualDeformation& deformation = restored.primaryTextObject().deformation;
    QCOMPARE(deformation.enabled, false);
    QCOMPARE(deformation.strength, 1.35);
    QCOMPARE(deformation.strokes.size(), 1);
    QCOMPARE(deformation.strokes.first().mode, BrushMode::Push);
    QCOMPARE(deformation.strokes.first().target, BrushTarget::Shape);
    QCOMPARE(deformation.strokes.first().samples.size(), 3);
    QCOMPARE(deformation.strokes.first().samples.at(1).delta, QPointF(10.0, 0.0));
}

void CoreTests::deformationResamplingIsBoundedAndDeterministic()
{
    QVector<QPointF> positions;
    positions.reserve(10000);
    for (int index = 0; index < 10000; ++index) {
        positions.push_back(QPointF(index * 0.25, std::sin(index * 0.05)));
    }

    const QVector<BrushSample> first = resampleBrushStroke(positions, 0.5, 1.0, 64);
    const QVector<BrushSample> second = resampleBrushStroke(positions, 0.5, 1.0, 64);
    QVERIFY(!first.isEmpty());
    QVERIFY(first.size() <= 64);
    QCOMPARE(first, second);
    QCOMPARE(first.first().position, positions.first());
    QCOMPARE(first.last().position, positions.last());
    QCOMPARE(first.first().delta, QPointF());
    for (int index = 1; index < first.size(); ++index) {
        QCOMPARE(first.at(index).delta,
                 first.at(index).position - first.at(index - 1).position);
    }
}

void CoreTests::pushStrokeIsDeterministic()
{
    const VectorGeometry original = rectangleGeometry();
    ManualDeformation deformation;
    deformation.strokes.push_back(pushStroke());

    VectorGeometry first = original;
    VectorGeometry second = original;
    deformation.apply(first);
    deformation.apply(second);
    QVERIFY(geometrySignature(first) != geometrySignature(original));
    QCOMPARE(geometrySignature(first), geometrySignature(second));
}

void CoreTests::glyphPushMovesRigidUnits()
{
    const VectorGeometry original = rectangleGeometry();
    ManualDeformation deformation;
    deformation.strokes.push_back(pushStroke(BrushTarget::Glyphs));

    VectorGeometry deformed = original;
    deformation.apply(deformed);
    const QPointF shift = deformed.pieces.at(0).anchor - original.pieces.at(0).anchor;
    QVERIFY(QLineF(QPointF(), shift).length() > 0.01);

    const QPainterPath& beforePath = original.pieces.at(0).path;
    const QPainterPath& afterPath = deformed.pieces.at(0).path;
    QCOMPARE(beforePath.elementCount(), afterPath.elementCount());
    for (int index = 0; index < beforePath.elementCount(); ++index) {
        const QPainterPath::Element before = beforePath.elementAt(index);
        const QPainterPath::Element after = afterPath.elementAt(index);
        QCOMPARE(QPointF(after.x - before.x, after.y - before.y), shift);
    }
}

void CoreTests::shapePushBendsContours()
{
    VectorGeometry original = rectangleGeometry();
    DeformationStroke stroke = pushStroke();
    stroke.samples = {{QPointF(2.0, 0.0), QPointF(0.0, 8.0), 1.0}};
    stroke.radius = 14.0;
    ManualDeformation deformation;
    deformation.strokes.push_back(stroke);

    VectorGeometry deformed = original;
    deformation.apply(deformed);
    QCOMPARE(deformed.pieces.at(0).anchor, original.pieces.at(0).anchor);
    QVERIFY(geometrySignature(deformed) != geometrySignature(original));
    const QPointF firstDelta = firstPathElement(deformed.pieces.at(0).path)
        - firstPathElement(original.pieces.at(0).path);
    const QPointF secondDelta = QPointF(
        deformed.pieces.at(0).path.elementAt(1).x - original.pieces.at(0).path.elementAt(1).x,
        deformed.pieces.at(0).path.elementAt(1).y - original.pieces.at(0).path.elementAt(1).y);
    QVERIFY(firstDelta != secondDelta);
}

void CoreTests::inflateMovesPointsOutward()
{
    DeformationStroke stroke = pushStroke();
    stroke.samples = {{QPointF(10.0, 10.0), QPointF(), 1.0}};
    stroke.radius = 30.0;
    stroke.mode = BrushMode::Inflate;
    ManualDeformation inflate;
    inflate.strokes.push_back(stroke);
    VectorGeometry inflated = rectangleGeometry();
    const QRectF originalBounds = inflated.pieces.first().path.boundingRect();
    inflate.apply(inflated);
    QVERIFY(inflated.pieces.first().path.boundingRect().width() > originalBounds.width());
}

void CoreTests::pinchMovesPointsInward()
{
    DeformationStroke stroke = pushStroke();
    stroke.samples = {{QPointF(10.0, 10.0), QPointF(), 1.0}};
    stroke.radius = 30.0;
    ManualDeformation pinch;
    stroke.mode = BrushMode::Pinch;
    pinch.strokes.push_back(stroke);
    VectorGeometry pinched = rectangleGeometry();
    const QRectF originalBounds = pinched.pieces.first().path.boundingRect();
    pinch.apply(pinched);
    QVERIFY(pinched.pieces.first().path.boundingRect().width() < originalBounds.width());
}

void CoreTests::pullMovesPointsTowardCenter()
{
    DeformationStroke stroke = pushStroke();
    stroke.mode = BrushMode::Pull;
    stroke.samples = {{QPointF(30.0, 10.0), QPointF(), 1.0}};
    ManualDeformation pull;
    pull.strokes.push_back(stroke);
    VectorGeometry pulled = rectangleGeometry();
    const qreal originalLeft = pulled.pieces.at(0).path.boundingRect().left();
    pull.apply(pulled);
    QVERIFY(pulled.pieces.at(0).path.boundingRect().left() > originalLeft);
}

void CoreTests::smoothBrushReducesLocalIrregularity()
{
    VectorGeometry geometry;
    GeometryPiece piece;
    piece.path.moveTo(0.0, 0.0);
    piece.path.lineTo(10.0, 0.0);
    piece.path.lineTo(20.0, 12.0);
    piece.path.lineTo(30.0, 0.0);
    piece.path.lineTo(40.0, 0.0);
    piece.path.closeSubpath();
    piece.anchor = QPointF(20.0, 6.0);
    piece.originalAnchor = piece.anchor;
    geometry.pieces.push_back(piece);
    geometry.setReferenceBounds(QRectF(0.0, 0.0, 40.0, 12.0));
    geometry.recomputeBounds();
    const qreal beforeHeight = geometry.bounds.height();

    DeformationStroke stroke;
    stroke.mode = BrushMode::Smooth;
    stroke.target = BrushTarget::Shape;
    stroke.radius = 24.0;
    stroke.strength = 1.0;
    stroke.hardness = 1.0;
    stroke.samples = {{QPointF(20.0, 12.0), QPointF(), 1.0}};
    ManualDeformation deformation;
    deformation.strokes.push_back(stroke);
    deformation.apply(geometry);
    QVERIFY(geometry.bounds.height() < beforeHeight);
}

void CoreTests::deformationStrengthAndToggleAreNondestructive()
{
    const VectorGeometry original = rectangleGeometry();
    ManualDeformation deformation;
    deformation.strokes.push_back(pushStroke());

    VectorGeometry zeroStrength = original;
    deformation.strength = 0.0;
    deformation.apply(zeroStrength);
    QCOMPARE(geometrySignature(zeroStrength), geometrySignature(original));

    VectorGeometry disabled = original;
    deformation.strength = 1.0;
    deformation.enabled = false;
    deformation.apply(disabled);
    QCOMPARE(geometrySignature(disabled), geometrySignature(original));

    deformation.enabled = true;
    VectorGeometry normal = original;
    deformation.apply(normal);
    QVERIFY(geometrySignature(normal) != geometrySignature(original));
    VectorGeometry reapplied = original;
    deformation.apply(reapplied);
    QCOMPARE(geometrySignature(normal), geometrySignature(reapplied));
}

void CoreTests::deformationPreservesMultipleContours()
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    path.addEllipse(QRectF(0.0, 0.0, 60.0, 60.0));
    path.addEllipse(QRectF(20.0, 20.0, 20.0, 20.0));
    const QVector<SampledContour> sampled = ContourSampler::samplePath(path, 0.1);
    QCOMPARE(sampled.size(), 2);
    const QPainterPath rebuilt = ContourSampler::reconstructPath(sampled, path.fillRule(), 0.05);
    QCOMPARE(ContourSampler::samplePath(rebuilt, 0.1).size(), 2);

    VectorGeometry geometry;
    GeometryPiece piece;
    piece.path = path;
    piece.anchor = QPointF(30.0, 30.0);
    piece.originalAnchor = piece.anchor;
    geometry.pieces.push_back(piece);
    geometry.setReferenceBounds(path.boundingRect());
    geometry.recomputeBounds();
    ManualDeformation deformation;
    deformation.strokes.push_back(pushStroke());
    deformation.apply(geometry);
    QCOMPARE(ContourSampler::samplePath(geometry.pieces.first().path, 0.1).size(), 2);
}

void CoreTests::openContourWithSeveralPointsStaysOpen()
{
    QPainterPath path;
    path.moveTo(0.0, 0.0);
    path.lineTo(20.0, 0.0);
    path.lineTo(25.0, 10.0);
    path.lineTo(40.0, 7.0);

    const QVector<SampledContour> sampled = ContourSampler::samplePath(path, 0.1);
    QCOMPARE(sampled.size(), 1);
    QVERIFY(!sampled.first().closed);

    const QPainterPath rebuilt = ContourSampler::reconstructPath(sampled, path.fillRule(), 0.05);
    const QVector<SampledContour> rebuiltContours = ContourSampler::samplePath(rebuilt, 0.1);
    QCOMPARE(rebuiltContours.size(), 1);
    QVERIFY(!rebuiltContours.first().closed);
}

void CoreTests::closedContourStaysClosed()
{
    QPainterPath path;
    path.addRect(QRectF(0.0, 0.0, 40.0, 30.0));

    const QVector<SampledContour> sampled = ContourSampler::samplePath(path, 0.1);
    QCOMPARE(sampled.size(), 1);
    QVERIFY(sampled.first().closed);

    const QPainterPath rebuilt = ContourSampler::reconstructPath(sampled, path.fillRule(), 0.05);
    const QVector<SampledContour> rebuiltContours = ContourSampler::samplePath(rebuilt, 0.1);
    QCOMPARE(rebuiltContours.size(), 1);
    QVERIFY(rebuiltContours.first().closed);
}

void CoreTests::mixedContourClosureSurvivesReconstruction()
{
    QPainterPath path;
    path.addRect(QRectF(0.0, 0.0, 40.0, 30.0));
    path.moveTo(70.0, 0.0);
    path.lineTo(90.0, 12.0);
    path.lineTo(105.0, 4.0);
    path.lineTo(125.0, 18.0);

    const QVector<SampledContour> sampled = ContourSampler::samplePath(path, 0.1);
    QCOMPARE(sampled.size(), 2);
    QVERIFY(sampled.at(0).closed);
    QVERIFY(!sampled.at(1).closed);

    const QPainterPath rebuilt = ContourSampler::reconstructPath(sampled, path.fillRule(), 0.05);
    const QVector<SampledContour> rebuiltContours = ContourSampler::samplePath(rebuilt, 0.1);
    QCOMPARE(rebuiltContours.size(), 2);
    QVERIFY(rebuiltContours.at(0).closed);
    QVERIFY(!rebuiltContours.at(1).closed);
}

void CoreTests::shapeDeformationPreservesOpenContours()
{
    QPainterPath path;
    path.moveTo(0.0, 0.0);
    path.lineTo(20.0, 0.0);
    path.lineTo(25.0, 10.0);
    path.lineTo(40.0, 7.0);

    VectorGeometry geometry;
    GeometryPiece piece;
    piece.path = path;
    piece.anchor = QPointF(20.0, 5.0);
    piece.originalAnchor = piece.anchor;
    geometry.pieces.push_back(piece);
    geometry.setReferenceBounds(path.boundingRect());
    geometry.recomputeBounds();

    ManualDeformation deformation;
    deformation.strokes.push_back(pushStroke(BrushTarget::Shape));
    deformation.apply(geometry);

    const QVector<SampledContour> contours = ContourSampler::samplePath(
        geometry.pieces.first().path, 0.1);
    QCOMPARE(contours.size(), 1);
    QVERIFY(!contours.first().closed);
}

void CoreTests::selectToolIsNotADeformationStroke()
{
    DeformationToolState state;
    QCOMPARE(static_cast<int>(state.tool()), static_cast<int>(EditorTool::Select));
    QVERIFY(!state.acceptsCanvasStroke());
    QVERIFY(!state.brushMode().has_value());
    QVERIFY(!state.targetSelectionEnabled());

    state.setTool(EditorTool::Push);
    QVERIFY(state.acceptsCanvasStroke());
    QVERIFY(state.brushMode().has_value());
}

void CoreTests::smoothToolForcesShapeAndRestoresTarget()
{
    DeformationToolState state;
    state.setTool(EditorTool::Push);
    state.setTarget(BrushTarget::Glyphs);
    state.setTool(EditorTool::Smooth);

    QCOMPARE(static_cast<int>(state.target()), static_cast<int>(BrushTarget::Shape));
    QVERIFY(!state.targetSelectionEnabled());
    QCOMPARE(static_cast<int>(state.brushMode().value()), static_cast<int>(BrushMode::Smooth));

    state.setTool(EditorTool::Push);
    QCOMPARE(static_cast<int>(state.target()), static_cast<int>(BrushTarget::Glyphs));
    QVERIFY(state.targetSelectionEnabled());
}

void CoreTests::smoothGlyphTargetIsNormalized()
{
    ManualDeformation source;
    DeformationStroke stroke = pushStroke(BrushTarget::Glyphs);
    stroke.mode = BrushMode::Smooth;
    source.strokes.push_back(stroke);

    ManualDeformation restored;
    QString error;
    QVERIFY2(ManualDeformation::fromJson(source.toJson(), &restored, &error), qPrintable(error));
    QCOMPARE(static_cast<int>(restored.strokes.first().target), static_cast<int>(BrushTarget::Shape));
}

void CoreTests::cyrillicShapeDeformationProducesGeometry()
{
    const QString family = cyrillicFamily();
    if (family.isEmpty()) {
        QSKIP("No installed font advertises Cyrillic support in this environment.");
    }
    TextObject object = configuredText(QStringLiteral("НЕ СМОТРИ"));
    object.font.family = family;
    object.font.styleName = QFontDatabase::styles(family).value(0);
    TextEngine engine;
    const ShapedText shaped = engine.shape(object);
    VectorGeometry original = GlyphGeometryBuilder::build(shaped, object.typography.fontSize);
    QVERIFY(original.hasVisibleGeometry());
    ManualDeformation deformation;
    DeformationStroke stroke = pushStroke();
    stroke.radius = original.referenceHeight;
    deformation.strokes.push_back(stroke);
    VectorGeometry deformed = original;
    deformation.apply(deformed);
    QVERIFY(deformed.hasVisibleGeometry());
    QVERIFY(geometrySignature(deformed) != geometrySignature(original));
}

void CoreTests::controllerUndoRedoAndMerge()
{
    EditorController controller;
    const QString objectId = controller.createTextObject(QPointF(120.0, 160.0));
    QVERIFY(!objectId.isEmpty());
    const QString initial = controller.activeObject()->sourceText;
    controller.undoStack()->setClean();
    QVERIFY(!controller.isModified());
    const int initialCommandCount = controller.undoStack()->count();
    controller.setText(QStringLiteral("first"));
    controller.setText(QStringLiteral("second"));
    QCOMPARE(controller.undoStack()->count(), initialCommandCount + 1);
    QVERIFY(controller.isModified());
    QCOMPARE(controller.document().objectById(objectId)->sourceText, QStringLiteral("second"));

    controller.undoStack()->undo();
    QCOMPARE(controller.document().objectById(objectId)->sourceText, initial);
    QVERIFY(!controller.isModified());
    controller.undoStack()->redo();
    QCOMPARE(controller.document().objectById(objectId)->sourceText, QStringLiteral("second"));
    QVERIFY(controller.isModified());
}

void CoreTests::controllerEffectCommandsAreGranular()
{
    EditorController controller;
    const QString objectId = controller.createTextObject(QPointF(0.0, 0.0), QStringLiteral("Effects"));
    QVERIFY(!objectId.isEmpty());
    controller.addEffect(QStringLiteral("wave"));
    controller.addEffect(QStringLiteral("stretch"));
    QCOMPARE(controller.document().objectById(objectId)->effects.size(), 2);
    QCOMPARE(controller.document().objectById(objectId)->effects.at(0)->typeId(), QStringLiteral("wave"));
    QCOMPARE(controller.document().objectById(objectId)->effects.at(1)->typeId(), QStringLiteral("stretch"));

    const int beforeParameter = controller.undoStack()->count();
    const double initialAmplitude = effectParameterValue(
        *controller.document().objectById(objectId)->effects.at(0), QStringLiteral("amplitude"));
    controller.setEffectParameter(0, QStringLiteral("amplitude"), 0.2);
    controller.setEffectParameter(0, QStringLiteral("amplitude"), 0.4);
    QCOMPARE(controller.undoStack()->count(), beforeParameter + 1);
    QCOMPARE(effectParameterValue(
                 *controller.document().objectById(objectId)->effects.at(0), QStringLiteral("amplitude")),
             0.4);
    controller.undoStack()->undo();
    QCOMPARE(effectParameterValue(
                 *controller.document().objectById(objectId)->effects.at(0), QStringLiteral("amplitude")),
             initialAmplitude);
    controller.undoStack()->redo();
    QCOMPARE(effectParameterValue(
                 *controller.document().objectById(objectId)->effects.at(0), QStringLiteral("amplitude")),
             0.4);

    controller.moveEffect(0, 1);
    QCOMPARE(controller.document().objectById(objectId)->effects.at(0)->typeId(), QStringLiteral("stretch"));
    controller.undoStack()->undo();
    QCOMPARE(controller.document().objectById(objectId)->effects.at(0)->typeId(), QStringLiteral("wave"));
    controller.removeEffect(0);
    QCOMPARE(controller.document().objectById(objectId)->effects.size(), 1);
    controller.undoStack()->undo();
    QCOMPARE(controller.document().objectById(objectId)->effects.size(), 2);
    QCOMPARE(controller.document().objectById(objectId)->effects.at(0)->typeId(), QStringLiteral("wave"));
}

void CoreTests::controllerDeformationCommandsAreGranular()
{
    EditorController controller;
    const QString objectId = controller.createTextObject(QPointF(0.0, 0.0), QStringLiteral("Deform"));
    QVERIFY(!objectId.isEmpty());
    controller.undoStack()->setClean();
    const DeformationStroke stroke = pushStroke(BrushTarget::Shape);
    controller.setDeformationPreview(stroke);
    QVERIFY(!controller.isModified());
    QVERIFY(controller.document().objectById(objectId)->deformation.strokes.isEmpty());
    controller.clearDeformationPreview();

    const int initialCommandCount = controller.undoStack()->count();
    controller.addDeformationStroke(stroke);
    QCOMPARE(controller.undoStack()->count(), initialCommandCount + 1);
    QCOMPARE(controller.document().objectById(objectId)->deformation.strokes.size(), 1);

    controller.undoStack()->undo();
    QVERIFY(controller.document().objectById(objectId)->deformation.strokes.isEmpty());
    controller.undoStack()->redo();
    QCOMPARE(controller.document().objectById(objectId)->deformation.strokes.size(), 1);

    const int beforeStrength = controller.undoStack()->count();
    controller.setDeformationStrength(0.5);
    controller.setDeformationStrength(0.75);
    QCOMPARE(controller.undoStack()->count(), beforeStrength + 1);
    QCOMPARE(controller.document().objectById(objectId)->deformation.strength, 0.75);

    controller.setDeformationEnabled(false);
    QVERIFY(!controller.document().objectById(objectId)->deformation.enabled);
    controller.undoStack()->undo();
    QVERIFY(controller.document().objectById(objectId)->deformation.enabled);
    controller.undoStack()->undo();
    QCOMPARE(controller.document().objectById(objectId)->deformation.strength, 1.0);

    controller.clearDeformation();
    QVERIFY(controller.document().objectById(objectId)->deformation.strokes.isEmpty());
    controller.undoStack()->undo();
    QCOMPARE(controller.document().objectById(objectId)->deformation.strokes.size(), 1);
}

void CoreTests::controllerCleanStateFollowsUndoStack()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    EditorController controller;
    const QString objectId = controller.createTextObject(QPointF(), QStringLiteral("Clean state"));
    QVERIFY(!objectId.isEmpty());
    const QString projectPath = directory.filePath(QStringLiteral("clean-state.vtproj"));
    QString error;
    QVERIFY2(controller.saveProject(projectPath, &error), qPrintable(error));
    QVERIFY(!controller.isModified());

    controller.setText(QStringLiteral("dirty"));
    QVERIFY(controller.isModified());
    controller.undoStack()->undo();
    QVERIFY(!controller.isModified());
    controller.undoStack()->redo();
    QVERIFY(controller.isModified());
}

void CoreTests::documentHierarchyHasStableIds()
{
    Document document;
    QVERIFY(!document.pages.empty());
    QVERIFY(!document.currentPageId.isEmpty());
    QVERIFY(!document.activeLayerId.isEmpty());
    QVERIFY(document.activeObjectId.isEmpty());
    QVERIFY(!document.hasObjects());
    QVERIFY(document.currentPage());
    QVERIFY(document.activeLayer());
    const QString compatibilityObjectId = document.primaryTextObject().id;
    QVERIFY(compatibilityObjectId != document.activeLayerId);

    Page extraPage;
    Layer extraLayer;
    TextObject extraObject;
    QVERIFY(extraPage.id != document.currentPageId);
    QVERIFY(extraLayer.id != document.activeLayerId);
    QVERIFY(extraObject.id != compatibilityObjectId);
}

void CoreTests::legacyFlatProjectMigratesToPageAndLayer()
{
    Document original;
    original.primaryTextObject().sourceText = QStringLiteral("Legacy");
    QJsonObject root = ProjectSerializer::toJson(original).object();
    root.insert(QStringLiteral("formatVersion"), 3);
    root.remove(QStringLiteral("pages"));
    root.remove(QStringLiteral("currentPageId"));
    root.remove(QStringLiteral("activeLayerId"));
    root.remove(QStringLiteral("activeObjectId"));

    Document migrated;
    QString error;
    QVERIFY2(ProjectSerializer::fromJson(QJsonDocument(root), &migrated, &error), qPrintable(error));
    QCOMPARE(migrated.formatVersion, Document::CurrentFormatVersion);
    QCOMPARE(migrated.pages.size(), size_t(1));
    QCOMPARE(migrated.pages.front()->layers.size(), size_t(1));
    QCOMPARE(migrated.primaryTextObject().sourceText, QStringLiteral("Legacy"));
}

void CoreTests::multilineShapingPreservesLinesAndClusters()
{
    TextObject object = configuredText(QStringLiteral("СТРАХ\nНЕ СМОТРИ"));
    TextEngine engine;
    const ShapedText shaped = engine.shape(object);
    QVERIFY2(shaped.error.isEmpty(), qPrintable(shaped.error));
    QVERIFY(shaped.lineCount >= 2);
    QCOMPARE(shaped.lineBounds.size(), shaped.lineCount);
    QVERIFY(std::any_of(shaped.glyphs.cbegin(), shaped.glyphs.cend(), [](const ShapedGlyph& glyph) {
        return glyph.lineIndex > 0;
    }));
    QVERIFY(std::any_of(shaped.glyphs.cbegin(), shaped.glyphs.cend(), [](const ShapedGlyph& glyph) {
        return glyph.clusterStart >= 0 && glyph.clusterLength > 0;
    }));
    const VectorGeometry geometry = GlyphGeometryBuilder::build(shaped, object.typography.fontSize);
    QVERIFY(geometry.hasVisibleGeometry());
}

void CoreTests::logicalClusterSpansUseWholeLineContext()
{
    // UTF-16 layout: Latin [0], Cyrillic [1], base+combining mark [2,4),
    // surrogate pair [4,6), ligature-capable "fi" [6,8), Latin [8]. The
    // starts are intentionally split and out of logical order across two
    // simulated physical-font/bidi runs.
    const QVector<LogicalClusterSpan> spans = TextEngine::logicalClusterSpans(
        9, {{0, 2, 6, 8}, {4, 1, 2}});
    const QVector<LogicalClusterSpan> expected = {
        {0, 1}, {1, 1}, {2, 2}, {4, 2}, {6, 2}, {8, 1}};
    QCOMPARE(spans.size(), expected.size());
    for (int index = 0; index < expected.size(); ++index) {
        QCOMPARE(spans.at(index).start, expected.at(index).start);
        QCOMPARE(spans.at(index).length, expected.at(index).length);
    }

    // The last glyph in either run must stop at the next logical boundary,
    // never claim the remainder of the line merely because its run ended.
    QCOMPARE(spans.at(1).length, 1);
    QCOMPARE(spans.at(3).length, 2);
    EffectScope range;
    range.kind = EffectScopeKind::TextRange;
    range.start = 4;
    range.end = 6;
    QVERIFY(range.includes(spans.at(3).start, spans.at(3).length));
    QVERIFY(!range.includes(spans.at(2).start, spans.at(2).length));
    QVERIFY(!range.includes(spans.at(4).start, spans.at(4).length));

    const QVector<LogicalClusterSpan> filtered = TextEngine::logicalClusterSpans(
        5, {{4, 0, 0, -1, 5, 99}, {2, 4, 2}});
    const QVector<LogicalClusterSpan> filteredExpected = {{0, 2}, {2, 2}, {4, 1}};
    QCOMPARE(filtered.size(), filteredExpected.size());
    for (int index = 0; index < filteredExpected.size(); ++index) {
        QCOMPARE(filtered.at(index).start, filteredExpected.at(index).start);
        QCOMPARE(filtered.at(index).length, filteredExpected.at(index).length);
    }
    const QVector<LogicalClusterSpan> finalOnly =
        TextEngine::logicalClusterSpans(7, {{3, 3, 3}});
    QCOMPARE(finalOnly.size(), 1);
    QCOMPARE(finalOnly.front().start, 3);
    QCOMPARE(finalOnly.front().length, 4);
    QVERIFY(TextEngine::logicalClusterSpans(0, {{0, 1}}).isEmpty());
    QVERIFY(TextEngine::logicalClusterSpans(-4, {{0}}).isEmpty());
    QVERIFY(TextEngine::logicalClusterSpans(8, {}).isEmpty());

    TextObject emptyLines = configuredText(QStringLiteral("\nA\n\n"));
    TextEngine engine;
    const ShapedText emptyLineShaping = engine.shape(emptyLines);
    QVERIFY2(emptyLineShaping.error.isEmpty(), qPrintable(emptyLineShaping.error));
    QCOMPARE(emptyLineShaping.lineCount, 4);
    QCOMPARE(emptyLineShaping.lineBounds.size(), 4);
    QVERIFY(std::any_of(emptyLineShaping.glyphs.cbegin(), emptyLineShaping.glyphs.cend(),
                        [](const ShapedGlyph& glyph) {
                            return glyph.lineIndex == 1 && glyph.clusterStart == 1;
                        }));
}

void CoreTests::mixedUtf16ShapingUsesGlobalClusterSpans()
{
    const QString source = QStringLiteral("A\u0416e\u0301\U0001F600fi\u05D0\u05D1Z");
    TextObject object = configuredText(source);
    TextEngine engine;
    const ShapedText shaped = engine.shape(object);
    QVERIFY2(shaped.error.isEmpty(), qPrintable(shaped.error));
    QVERIFY(!shaped.glyphs.isEmpty());

    QVector<int> starts;
    for (const ShapedGlyph& glyph : shaped.glyphs) {
        if (glyph.lineIndex == 0 && glyph.clusterStart >= 0) starts.push_back(glyph.clusterStart);
    }
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    QVERIFY(starts.size() >= 5);
    for (const ShapedGlyph& glyph : shaped.glyphs) {
        if (glyph.lineIndex != 0 || glyph.clusterStart < 0) continue;
        const auto position = std::lower_bound(starts.cbegin(), starts.cend(), glyph.clusterStart);
        QVERIFY(position != starts.cend() && *position == glyph.clusterStart);
        const int index = static_cast<int>(position - starts.cbegin());
        const int next = index + 1 < starts.size() ? starts.at(index + 1) : source.size();
        QCOMPARE(glyph.clusterLength, next - glyph.clusterStart);
    }

    const VectorGeometry geometry = GlyphGeometryBuilder::build(
        shaped, object.typography.fontSize);
    for (const GeometryPiece& piece : geometry.pieces) {
        if (piece.sourceGlyphIndex < 0 || piece.sourceGlyphIndex >= shaped.glyphs.size()) continue;
        const ShapedGlyph& glyph = shaped.glyphs.at(piece.sourceGlyphIndex);
        QCOMPARE(piece.sourceClusterStart, glyph.clusterStart);
        QCOMPARE(piece.sourceClusterLength, glyph.clusterLength);
    }
}

void CoreTests::mixedUtf16ClustersSurviveEffectsPersistenceAndExport()
{
    const QString source = QStringLiteral("Ae\u0301\U0001F600fi\u05D0\u05D1Z");
    TextObject object = configuredText(source);
    object.id = QStringLiteral("mixed-utf16-pipeline");
    TextEngine engine;
    const ShapedText shaped = engine.shape(object);
    QVERIFY2(shaped.error.isEmpty(), qPrintable(shaped.error));
    QVERIFY(!shaped.glyphs.isEmpty());

    bool combiningOwnedByBaseCluster = false;
    bool surrogatePairOwnedAsOneCluster = false;
    for (const ShapedGlyph& glyph : shaped.glyphs) {
        if (glyph.clusterStart < 0) continue;
        QVERIFY(glyph.clusterLength > 0);
        QVERIFY(glyph.clusterStart + glyph.clusterLength <= source.size());
        QVERIFY(glyph.clusterStart != 2); // combining mark is not a new cluster
        QVERIFY(glyph.clusterStart != 4); // low surrogate is not a new cluster
        if (glyph.clusterStart <= 1
            && glyph.clusterStart + glyph.clusterLength > 2) {
            combiningOwnedByBaseCluster = true;
        }
        if (glyph.clusterStart <= 3
            && glyph.clusterStart + glyph.clusterLength >= 5) {
            surrogatePairOwnedAsOneCluster = true;
        }
    }
    QVERIFY(combiningOwnedByBaseCluster);
    QVERIFY(surrogatePairOwnedAsOneCluster);

    const VectorGeometry unscoped = GlyphGeometryBuilder::build(
        shaped, object.typography.fontSize);
    QVERIFY(unscoped.hasVisibleGeometry());
    for (const GeometryPiece& piece : unscoped.pieces) {
        if (piece.sourceGlyphIndex < 0 || piece.sourceGlyphIndex >= shaped.glyphs.size()) continue;
        const ShapedGlyph& glyph = shaped.glyphs.at(piece.sourceGlyphIndex);
        QCOMPARE(piece.sourceClusterStart, glyph.clusterStart);
        QCOMPARE(piece.sourceClusterLength, glyph.clusterLength);
    }

    auto stretch = std::make_unique<StretchEffect>();
    stretch->instanceId = QStringLiteral("mixed-utf16-range-effect");
    stretch->horizontal = 1.8;
    stretch->vertical = 0.7;
    stretch->scope = {EffectScopeKind::TextRange, 1, 3};
    object.effects.append(std::move(stretch));

    VectorGeometry scoped = unscoped;
    object.effects.apply(scoped);
    bool targetedPieceChanged = false;
    for (int index = 0; index < scoped.pieces.size(); ++index) {
        const GeometryPiece& before = unscoped.pieces.at(index);
        const GeometryPiece& after = scoped.pieces.at(index);
        const bool targeted = object.effects.at(0)->scope.includes(
            before.sourceClusterStart, before.sourceClusterLength);
        if (targeted) {
            targetedPieceChanged = targetedPieceChanged || before.path != after.path;
        } else {
            QVERIFY(before.path == after.path);
            QCOMPARE(before.anchor, after.anchor);
        }
        QCOMPARE(after.sourceClusterStart, before.sourceClusterStart);
        QCOMPARE(after.sourceClusterLength, before.sourceClusterLength);
    }
    QVERIFY(targetedPieceChanged);

    Document document;
    document.primaryTextObject() = object;
    document.activeObjectId = object.id;
    const SceneGeometry beforeSave = SceneEvaluator::evaluate(*document.currentPage(), 501);
    QCOMPARE(beforeSave.evaluationStatus, EvaluationStatus::Complete);
    const SceneObjectGeometry* beforeObject = beforeSave.objectById(object.id);
    QVERIFY(beforeObject);

    Document restored;
    QString error;
    QVERIFY2(ProjectSerializer::fromJson(ProjectSerializer::toJson(document),
                                         &restored, &error), qPrintable(error));
    const TextObject* restoredObject = restored.objectById(object.id);
    QVERIFY(restoredObject);
    QCOMPARE(restoredObject->sourceText, source);
    QCOMPARE(restoredObject->effects.at(0)->scope.kind, EffectScopeKind::TextRange);
    QCOMPARE(restoredObject->effects.at(0)->scope.start, 1);
    QCOMPARE(restoredObject->effects.at(0)->scope.end, 3);
    const SceneGeometry afterLoad = SceneEvaluator::evaluate(*restored.currentPage(), 501);
    const SceneObjectGeometry* afterObject = afterLoad.objectById(object.id);
    QVERIFY(afterObject);
    QString difference;
    QVERIFY2(test::compareGeometry(test::geometrySignature(beforeObject->geometry),
                                   test::geometrySignature(afterObject->geometry),
                                   &difference), qPrintable(difference));

    VectorExportPayload payload;
    QVERIFY2(ExportPayloadBuilder::build(restored, *restored.currentPage(), afterLoad,
                                         ExportScope::CurrentPage, {}, &payload, &error),
             qPrintable(error));
    QCOMPARE(payload.plainText, source);
    qsizetype recordIndex = 0;
    for (const GeometryPiece& piece : afterObject->geometry.pieces) {
        if (piece.path.isEmpty() || !piece.path.boundingRect().isValid()
            || piece.path.boundingRect().isEmpty()) {
            continue;
        }
        QVERIFY(recordIndex < payload.records.size());
        const VectorExportRecord& record = payload.records.at(recordIndex++);
        QCOMPARE(record.sourceObjectId, object.id);
        QCOMPARE(record.sourceText, source);
        QVERIFY(record.path == piece.path);
    }
    QCOMPARE(recordIndex, payload.records.size());
}

void CoreTests::scopedEffectsOnlyTouchSelectedClusters()
{
    VectorGeometry original = rectangleGeometry();
    original.pieces[0].sourceClusterStart = 0;
    original.pieces[1].sourceClusterStart = 1;
    original.pieces[0].sourceClusterLength = 1;
    original.pieces[1].sourceClusterLength = 1;
    VectorGeometry scoped = original;
    EffectStack stack;
    auto stretch = std::make_unique<StretchEffect>();
    stretch->horizontal = 2.0;
    stretch->vertical = 1.0;
    stretch->scope.kind = EffectScopeKind::TextRange;
    stretch->scope.start = 0;
    stretch->scope.end = 1;
    stack.append(std::move(stretch));
    stack.apply(scoped);
    VectorGeometry originalSecond;
    originalSecond.pieces.push_back(original.pieces.at(1));
    VectorGeometry scopedSecond;
    scopedSecond.pieces.push_back(scoped.pieces.at(1));
    QCOMPARE(geometrySignature(scopedSecond), geometrySignature(originalSecond));
    QVERIFY(geometrySignature(scoped) != geometrySignature(original));
}

void CoreTests::proceduralEffectsAreDeterministicAndAvailable()
{
    const QVector<QPair<QString, QString>> definitions = availableEffectTypes();
    QVERIFY(definitions.size() >= 15);
    const TextObject object = configuredText();
    const VectorGeometry source = baseGeometry(object);
    for (const auto& definition : definitions) {
        std::unique_ptr<Effect> first = createEffect(definition.first);
        std::unique_ptr<Effect> second = createEffect(definition.first);
        QVERIFY(first);
        QVERIFY(second);
        VectorGeometry left = source;
        VectorGeometry right = source;
        first->apply(left, {source.referenceBounds, source.referenceHeight});
        second->apply(right, {source.referenceBounds, source.referenceHeight});
        QCOMPARE(geometrySignature(left), geometrySignature(right));
    }
}

void CoreTests::selectionModelSupportsSingleAndRangeSelection()
{
    SelectionModel selection;
    selection.selectSingle(QStringLiteral("a"));
    QVERIFY(selection.contains(QStringLiteral("a")));
    QCOMPARE(selection.activeObjectId(), QStringLiteral("a"));
    selection.add(QStringLiteral("b"));
    QCOMPARE(selection.selectedObjectIds().size(), 2);
    selection.toggle(QStringLiteral("a"));
    QVERIFY(!selection.contains(QStringLiteral("a")));
    selection.setTextRange(9, 3);
    QVERIFY(selection.hasTextRange());
    QCOMPARE(selection.textRange(), qMakePair(3, 9));
    selection.clearTextRange();
    QVERIFY(!selection.hasTextRange());
}

void CoreTests::controllerSceneCommandsMoveDuplicateAndDeleteObjects()
{
    EditorController controller;
    const QString firstId = controller.createTextObject(QPointF(80.0, 100.0), QStringLiteral("First"));
    QVERIFY(!firstId.isEmpty());
    const QString createdSecondId = controller.createTextObject(QPointF(120.0, 160.0),
                                                                 QStringLiteral("Second"));
    QVERIFY(!createdSecondId.isEmpty());
    const QString secondId = controller.activeObject()->id;
    QCOMPARE(controller.document().objectsOnCurrentPage().size(), 2);
    controller.moveSelectedObjects(QPointF(8.0, 12.0));
    QCOMPARE(controller.activeObject()->transform.position, QPointF(128.0, 172.0));
    controller.undoStack()->undo();
    QCOMPARE(controller.activeObject()->transform.position, QPointF(120.0, 160.0));
    controller.undoStack()->redo();
    QCOMPARE(controller.activeObject()->transform.position, QPointF(128.0, 172.0));
    controller.duplicateSelectedObjects();
    QCOMPARE(controller.document().objectsOnCurrentPage().size(), 3);
    QVERIFY(controller.activeObject()->id != secondId);
    QVERIFY(controller.activeObject()->id != firstId);
    controller.deleteSelectedObjects();
    QCOMPARE(controller.document().objectsOnCurrentPage().size(), 2);
}

void CoreTests::controllerUndoTargetsStableObject()
{
    EditorController controller;
    const QString firstId = controller.createTextObject(QPointF(), QStringLiteral("First"));
    QVERIFY(!firstId.isEmpty());
    controller.undoStack()->setClean();
    controller.setText(QStringLiteral("First edited"));

    const QString secondId = controller.createTextObject(QPointF(), QStringLiteral("Second"));
    QVERIFY(!secondId.isEmpty());
    controller.setText(QStringLiteral("Second edited"));
    controller.selectObject(firstId);

    controller.undoStack()->undo();
    QCOMPARE(controller.document().objectById(firstId)->sourceText, QStringLiteral("First edited"));
    QCOMPARE(controller.document().objectById(secondId)->sourceText, QStringLiteral("Second"));
    controller.undoStack()->redo();
    QCOMPARE(controller.document().objectById(secondId)->sourceText, QStringLiteral("Second edited"));
}

void CoreTests::controllerPageAndLayerCommandsAreUndoable()
{
    EditorController controller;
    QCOMPARE(controller.document().pages.size(), size_t(1));
    controller.addLayer();
    const QString addedLayerId = controller.document().activeLayerId;
    QCOMPARE(controller.document().currentPage()->layers.size(), size_t(2));
    controller.undoStack()->undo();
    QCOMPARE(controller.document().currentPage()->layers.size(), size_t(1));
    QVERIFY(controller.document().activeLayerId != addedLayerId);
    controller.undoStack()->redo();
    QCOMPARE(controller.document().currentPage()->layers.size(), size_t(2));
    QCOMPARE(controller.document().activeLayerId, addedLayerId);
    controller.addPage();
    QCOMPARE(controller.document().pages.size(), size_t(2));
    const QString addedPageId = controller.document().currentPageId;
    controller.undoStack()->undo();
    QCOMPARE(controller.document().pages.size(), size_t(1));
    QVERIFY(controller.document().currentPageId != addedPageId);
    controller.undoStack()->redo();
    QCOMPARE(controller.document().pages.size(), size_t(2));
    controller.removeCurrentPage();
    QCOMPARE(controller.document().pages.size(), size_t(1));
    controller.undoStack()->undo();
    QCOMPARE(controller.document().pages.size(), size_t(2));
    QCOMPARE(controller.document().currentPageId, addedPageId);
    controller.undoStack()->redo();
    QCOMPARE(controller.document().pages.size(), size_t(1));
}

void CoreTests::controllerMoveObjectBetweenLayersIsUndoable()
{
    EditorController controller;
    const QString objectId = controller.createTextObject(QPointF(40.0, 50.0), QStringLiteral("Move me"));
    QVERIFY(!objectId.isEmpty());
    const QString sourceLayerId = controller.document().activeLayerId;
    TextObject* original = controller.document().objectById(objectId);
    QVERIFY(original);
    original->transform.rotation = 12.0;
    original->transform.scale = QPointF(1.4, 0.8);
    auto wave = std::make_unique<WaveEffect>();
    EffectMaskStroke mask;
    mask.points = {QPointF(10.0, 10.0)};
    mask.restore = true;
    wave->maskStrokes.push_back(mask);
    original->effects.append(std::move(wave));
    original->deformation.strokes.push_back(pushStroke());
    const QJsonObject fontBefore = original->font.toJson();
    const QJsonObject typographyBefore = original->typography.toJson(original->fill);
    const QColor fillBefore = original->fill;
    const QJsonObject transformBefore = original->transform.toJson();
    const QJsonArray effectsBefore = original->effects.toJson();
    const QJsonObject deformationBefore = original->deformation.toJson();

    controller.addLayer();
    const QString destinationLayerId = controller.document().activeLayerId;
    QVERIFY(sourceLayerId != destinationLayerId);
    controller.moveObjectToLayer(objectId, destinationLayerId);

    const Layer* source = controller.document().layerById(sourceLayerId);
    const Layer* destination = controller.document().layerById(destinationLayerId);
    QVERIFY(source);
    QVERIFY(destination);
    QVERIFY(!source->objectById(objectId));
    const TextObject* moved = destination->objectById(objectId);
    QVERIFY(moved);
    QCOMPARE(moved->id, objectId);
    QCOMPARE(moved->sourceText, QStringLiteral("Move me"));
    QCOMPARE(moved->font.toJson(), fontBefore);
    QCOMPARE(moved->typography.toJson(moved->fill), typographyBefore);
    QCOMPARE(moved->fill, fillBefore);
    QCOMPARE(moved->transform.toJson(), transformBefore);
    QCOMPARE(moved->effects.toJson(), effectsBefore);
    QCOMPARE(moved->deformation.toJson(), deformationBefore);

    controller.undoStack()->undo();
    QVERIFY(controller.document().layerById(sourceLayerId)->objectById(objectId));
    QVERIFY(!controller.document().layerById(destinationLayerId)->objectById(objectId));
    QCOMPARE(controller.document().activeLayerId, sourceLayerId);
    controller.undoStack()->redo();
    QVERIFY(controller.document().layerById(destinationLayerId)->objectById(objectId));
    QCOMPARE(controller.document().activeLayerId, destinationLayerId);
}

void CoreTests::controllerLockedLayerObjectsAreNotEditable()
{
    EditorController controller;
    const QString objectId = controller.createTextObject(QPointF(), QStringLiteral("Locked"));
    QVERIFY(!objectId.isEmpty());
    TextObject* object = controller.document().objectById(objectId);
    QVERIFY(object);
    object->effects.append(std::make_unique<WaveEffect>());
    const QString effectId = object->effects.at(0)->instanceId;
    const QPointF oldPosition = object->transform.position;
    const QString oldText = object->sourceText;
    controller.setActiveLayerLocked(true);
    controller.setText(QStringLiteral("Should stay locked"));
    controller.moveSelectedObjects(QPointF(20.0, 20.0));
    controller.addDeformationStroke(pushStroke());
    EffectMaskStroke mask;
    mask.points = {QPointF(0.0, 0.0)};
    controller.addEffectMaskStroke(objectId, effectId, mask);

    QCOMPARE(controller.document().objectById(objectId)->sourceText, oldText);
    QCOMPARE(controller.document().objectById(objectId)->transform.position, oldPosition);
    QVERIFY(controller.document().objectById(objectId)->deformation.strokes.isEmpty());
    QVERIFY(controller.document().objectById(objectId)->effects.at(0)->maskStrokes.isEmpty());
    QVERIFY(!controller.activeObject());
}

void CoreTests::controllerMaskUsesObjectLocalScale()
{
    EditorController controller;
    const QString objectId = controller.createTextObject(QPointF(), QStringLiteral("Masked"));
    QVERIFY(!objectId.isEmpty());
    TextObject* object = controller.document().objectById(objectId);
    QVERIFY(object);
    object->transform.position = QPointF(160.0, 120.0);
    object->transform.scale = QPointF(2.0, 2.0);
    object->effects.append(std::make_unique<WaveEffect>());
    const QString effectId = object->effects.at(0)->instanceId;

    EffectMaskStroke stroke;
    stroke.points = {QPointF(180.0, 140.0)};
    stroke.radius = 20.0;
    controller.addEffectMaskStroke(objectId, effectId, stroke);
    QCOMPARE(object->effects.at(0)->maskStrokes.size(), 1);
    QCOMPARE(object->effects.at(0)->maskStrokes.front().radius, 10.0);

    Document restored;
    QString error;
    QVERIFY2(ProjectSerializer::fromJson(ProjectSerializer::toJson(controller.document()),
                                         &restored,
                                         &error),
             qPrintable(error));
    QCOMPARE(restored.objectById(objectId)->effects.at(0)->maskStrokes.front().radius, 10.0);
}

void CoreTests::pageReorderIsUndoableAndSerializable()
{
    EditorController controller;
    controller.addPage();
    controller.addPage();
    QVERIFY(controller.document().pages.size() == 3);
    QStringList originalOrder;
    for (const auto& page : controller.document().pages) {
        originalOrder.push_back(page->id);
    }
    controller.movePage(2, 0);
    QCOMPARE(controller.document().pages.at(0)->id, originalOrder.at(2));
    controller.undoStack()->undo();
    QCOMPARE(controller.document().pages.at(0)->id, originalOrder.at(0));
    controller.undoStack()->redo();
    QCOMPARE(controller.document().pages.at(0)->id, originalOrder.at(2));

    Document restored;
    QString error;
    QVERIFY2(ProjectSerializer::fromJson(ProjectSerializer::toJson(controller.document()),
                                         &restored,
                                         &error),
             qPrintable(error));
    QCOMPARE(restored.pages.size(), controller.document().pages.size());
    for (int index = 0; index < static_cast<int>(restored.pages.size()); ++index) {
        QCOMPARE(restored.pages.at(static_cast<size_t>(index))->id,
                 controller.document().pages.at(static_cast<size_t>(index))->id);
    }
}

void CoreTests::shapingCacheKeyIgnoresFillButTracksLayoutInputs()
{
    TextObject object = configuredText(QStringLiteral("Cache key"));
    const QByteArray original = SceneEvaluator::shapingCacheKey(object);
    object.fill = QColor(Qt::red);
    QCOMPARE(SceneEvaluator::shapingCacheKey(object), original);
    object.sourceText += QStringLiteral(" changed");
    QVERIFY(SceneEvaluator::shapingCacheKey(object) != original);
    object.sourceText = QStringLiteral("Cache key");
    object.typography.trackingEm += 0.01;
    QVERIFY(SceneEvaluator::shapingCacheKey(object) != original);
    object.typography.trackingEm = 0.025;
    object.typography.lineSpacing += 0.1;
    QVERIFY(SceneEvaluator::shapingCacheKey(object) != original);
    object.typography.lineSpacing = 1.0;
    object.font.weight += 100;
    QVERIFY(SceneEvaluator::shapingCacheKey(object) != original);
}

void CoreTests::shortcutManagerDetectsConflictsAndPersists()
{
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("VectorTypographyTests"));
    QCoreApplication::setApplicationName(QStringLiteral("ShortcutManager"));

    const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString firstId = QStringLiteral("test.open.%1").arg(suffix);
    const QString secondId = QStringLiteral("test.close.%1").arg(suffix);
    const QKeySequence firstSequence(QStringLiteral("Ctrl+Shift+9"));
    const QKeySequence secondDefault(QStringLiteral("Ctrl+Shift+0"));
    const QKeySequence secondCustom(QStringLiteral("Ctrl+Alt+9"));

    QAction firstAction;
    QAction secondAction;
    ShortcutManager manager;
    manager.registerAction(firstId, &firstAction, firstSequence);
    manager.registerAction(secondId, &secondAction, secondDefault);

    QString error;
    QVERIFY(!manager.setShortcut(secondId, firstSequence, &error));
    QVERIFY(error.contains(firstId));
    QVERIFY(manager.setShortcut(secondId, secondCustom, &error));
    QCOMPARE(secondAction.shortcut(), secondCustom);

    QAction restoredAction;
    ShortcutManager restored;
    restored.registerAction(secondId, &restoredAction, secondDefault);
    QCOMPARE(restored.shortcut(secondId), secondCustom);
    restored.resetToDefaults();
    QCOMPARE(restored.shortcut(secondId), secondDefault);

}

void CoreTests::asyncEvaluationPublishesLatestGeneration()
{
    EditorController controller;
    const QString objectId = controller.createTextObject(QPointF(0.0, 0.0), QStringLiteral("initial"));
    QVERIFY(!objectId.isEmpty());
    controller.setText(QStringLiteral("stale generation"));
    controller.setText(QStringLiteral("latest generation"));

    QTRY_VERIFY_WITH_TIMEOUT(controller.sceneGeometry().objectById(objectId)
                                 && controller.sceneGeometry().objectById(objectId)->sourceText
                                        == QStringLiteral("latest generation"),
                             5000);
}

void CoreTests::objectFrameRoundTripsPointsAndVectors()
{
    ObjectTransform transform;
    transform.position = QPointF(240.0, -75.0);
    transform.rotation = 31.0;
    transform.scale = QPointF(2.0, -0.75);
    const ObjectFrame frame = ObjectFrame::fromTransform(transform,
                                                          QRectF(-20.0, 10.0, 120.0, 60.0));
    const QPointF localPoint(12.5, 44.0);
    const QPointF localVector(8.0, -3.0);
    const QPointF pagePoint = frame.localPointToPage(localPoint);
    const QPointF pageVector = frame.localVectorToPage(localVector);
    const QPointF pointRoundTrip = frame.pagePointToLocal(pagePoint);
    const QPointF vectorRoundTrip = frame.pageVectorToLocal(pageVector);
    QVERIFY(QLineF(pointRoundTrip, localPoint).length() < 1.0e-8);
    QVERIFY(QLineF(vectorRoundTrip, localVector).length() < 1.0e-8);
    QVERIFY(QLineF(frame.pageVectorToLocal(frame.localPointToPage(localVector)), localVector).length() > 1.0);
    QCOMPARE(frame.orientedPageQuad().size(), 4);
    QVERIFY(frame.pageAabb().contains(pagePoint));
}

void CoreTests::deformationBrushModesProduceDistinctGeometry()
{
    QSet<QByteArray> signatures;
    for (const BrushMode mode : {BrushMode::Push, BrushMode::Pull, BrushMode::Inflate,
                                 BrushMode::Pinch, BrushMode::Smooth}) {
        VectorGeometry geometry = rectangleGeometry();
        DeformationStroke stroke;
        stroke.mode = mode;
        stroke.target = BrushTarget::Shape;
        stroke.radius = 120.0;
        stroke.strength = 1.0;
        stroke.hardness = 0.5;
        stroke.samples = {{QPointF(30.0, 10.0), QPointF(), 1.0},
                          {QPointF(40.0, 10.0), QPointF(10.0, 0.0), 1.0}};
        ManualDeformation deformation;
        deformation.strokes.push_back(stroke);
        deformation.apply(geometry);
        signatures.insert(geometrySignature(geometry));
    }
    QCOMPARE(signatures.size(), 5);
}

void CoreTests::decorationsFollowTrackedLineExtents()
{
    TextObject base = configuredText(QStringLiteral("ABCD"));
    base.typography.trackingEm = 0.0;
    TextEngine engine;
    const ShapedText untracked = engine.shape(base);
    QVERIFY2(untracked.error.isEmpty(), qPrintable(untracked.error));
    const int glyphCount = std::count_if(untracked.glyphs.cbegin(), untracked.glyphs.cend(),
                                         [](const ShapedGlyph& glyph) { return glyph.lineIndex == 0; });
    QVERIFY(glyphCount > 1);

    for (const qreal tracking : {0.05, -0.05, 0.0}) {
        TextObject object = base;
        object.typography.trackingEm = tracking;
        engine.clearCache();
        const ShapedText shaped = engine.shape(object);
        QVERIFY2(shaped.error.isEmpty(), qPrintable(shaped.error));
        const qreal expectedWidth = qMax<qreal>(0.0, untracked.lineBounds.at(0).width()
            + tracking * shaped.resolvedEmSize * (glyphCount - 1));
        QVERIFY(shaped.lineBounds.at(0).isValid());
        QVERIFY(std::abs(shaped.lineBounds.at(0).width() - expectedWidth) < 0.01);

        const VectorGeometry geometry = GlyphGeometryBuilder::build(shaped,
                                                                      object.typography.fontSize,
                                                                      true, true);
        QVector<qreal> decorationWidths;
        for (const GeometryPiece& piece : geometry.pieces) {
            if (piece.sourceGlyphIndex == -1 && piece.sourceLineIndex == 0) {
                decorationWidths.push_back(piece.path.boundingRect().width());
            }
        }
        QCOMPARE(decorationWidths.size(), 2);
        for (const qreal width : decorationWidths) {
            QVERIFY(std::abs(width - shaped.lineBounds.at(0).width()) < 0.01);
        }
    }

    TextObject multiline = base;
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
    const QString emoji = QString::fromUtf8("A😀BC");
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
