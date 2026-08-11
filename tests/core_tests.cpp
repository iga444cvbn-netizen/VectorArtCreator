#include "core/document/document.h"
#include "core/deformation/contour_sampler.h"
#include "core/deformation/manual_deformation.h"
#include "core/effects/glyph_jitter_effect.h"
#include "core/effects/effect_registry.h"
#include "core/effects/text_range_rebaser.h"
#include "core/effects/procedural_effect.h"
#include "core/effects/stretch_effect.h"
#include "core/effects/wave_effect.h"
#include "core/export/svg_exporter.h"
#include "core/export/export_payload_builder.h"
#include "core/presets/preset.h"
#include "core/presets/preset_manager.h"
#include "core/presets/preset_catalog.h"
#include "core/serialization/project_serializer.h"
#include "core/scene/scene_evaluator.h"
#include "core/scene/object_frame.h"
#include "core/text/text_engine.h"
#include "core/undo/document_commands.h"
#include "ui/deformation_tool_state.h"
#include "ui/editor_controller.h"
#include "ui/selection_model.h"
#include "ui/shortcut_manager.h"

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
#include <QPainterPath>
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

} // namespace

class CoreTests final : public QObject {
    Q_OBJECT

private slots:
    void projectSerializationRoundTrip();
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
    QVERIFY(svg.contains("fill-opacity=\"0.4\""));
    QVERIFY(svg.contains("fill-rule=\"nonzero\""));
    QVERIFY(svg.contains("viewBox=\"0 0 40 40\""));
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
