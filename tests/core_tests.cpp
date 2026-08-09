#include "core/document/document.h"
#include "core/effects/glyph_jitter_effect.h"
#include "core/effects/stretch_effect.h"
#include "core/effects/wave_effect.h"
#include "core/export/svg_exporter.h"
#include "core/presets/preset.h"
#include "core/presets/preset_manager.h"
#include "core/serialization/project_serializer.h"
#include "core/text/text_engine.h"
#include "ui/editor_controller.h"

#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
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

} // namespace

class CoreTests final : public QObject {
    Q_OBJECT

private slots:
    void projectSerializationRoundTrip();
    void projectV1TrackingMigrates();
    void presetSerializationRoundTrip();
    void unicodePresetStorageIsCollisionSafe();
    void deterministicJitter();
    void effectOrderingIsDeterministic();
    void textReplacementPreservesEffects();
    void presetApplicationClonesEffects();
    void svgExportContainsPaths();
    void cyrillicTextProducesGeometry();
    void glyphFallbackIsReportedWhenAvailable();
    void missingFontStatesAreDistinguished();
    void trackingScalesWithFontSize();
    void trackingUsesTrueEmDistance();
    void controllerUndoRedoAndMerge();
    void controllerEffectCommandsAreGranular();
    void controllerCleanStateFollowsUndoStack();
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
        QStringLiteral("Бездна"),
        QStringLiteral("Паника"),
        QStringLiteral("Туман"),
        QStringLiteral("Шёпот"),
    };
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
    QVERIFY2(manager.loadPreset(QStringLiteral("Бездна"), &loadedByName, &error), qPrintable(error));
    QCOMPARE(loadedByName.name, QStringLiteral("Бездна"));
    QVERIFY2(manager.deletePreset(QStringLiteral("Паника"), &error), qPrintable(error));
    QVERIFY2(manager.deletePresetById(loadedByName.id, &error), qPrintable(error));
    QVERIFY(!manager.listPresetNames().contains(QStringLiteral("Паника")));
    QVERIFY(!manager.listPresetNames().contains(QStringLiteral("Бездна")));
    QVERIFY(manager.listPresetNames().contains(QStringLiteral("Туман")));
    QVERIFY(manager.listPresetNames().contains(QStringLiteral("Шёпот")));
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

void CoreTests::textReplacementPreservesEffects()
{
    Document document;
    document.primaryTextObject().effects.append(std::make_unique<WaveEffect>());
    document.primaryTextObject().effects.append(std::make_unique<GlyphJitterEffect>());
    const QJsonArray before = document.primaryTextObject().effects.toJson();
    document.primaryTextObject().sourceText = QStringLiteral("Другой текст");
    QCOMPARE(document.primaryTextObject().effects.toJson(), before);
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

void CoreTests::cyrillicTextProducesGeometry()
{
    const QString family = cyrillicFamily();
    if (family.isEmpty()) {
        QSKIP("No installed font advertises Cyrillic support in this environment.");
    }

    TextObject object = configuredText(QStringLiteral("Привет мир"));
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
    const qreal ratio = largeShape.logicalBounds.width() / smallShape.logicalBounds.width();
    QVERIFY2(std::abs(ratio - 2.0) < 0.15, qPrintable(QStringLiteral("tracking ratio was %1").arg(ratio)));
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
    const qreal narrowDelta = engine.shape(narrow).logicalBounds.width()
        - engine.shape(narrowUntracked).logicalBounds.width();
    const qreal wideDelta = engine.shape(wide).logicalBounds.width()
        - engine.shape(wideUntracked).logicalBounds.width();

    const qreal expected = 3.0 * narrow.typography.fontSize * narrow.typography.trackingEm;
    QVERIFY2(std::abs(narrowDelta - expected) < 0.01,
             qPrintable(QStringLiteral("narrow tracking delta was %1").arg(narrowDelta)));
    QVERIFY2(std::abs(wideDelta - expected) < 0.01,
             qPrintable(QStringLiteral("wide tracking delta was %1").arg(wideDelta)));
    QVERIFY(std::abs(narrowDelta - wideDelta) < 0.01);
}

void CoreTests::controllerUndoRedoAndMerge()
{
    EditorController controller;
    const QString initial = controller.document().primaryTextObject().sourceText;
    QVERIFY(!controller.isModified());
    controller.setText(QStringLiteral("first"));
    controller.setText(QStringLiteral("second"));
    QCOMPARE(controller.undoStack()->count(), 1);
    QVERIFY(controller.isModified());
    QCOMPARE(controller.document().primaryTextObject().sourceText, QStringLiteral("second"));

    controller.undoStack()->undo();
    QCOMPARE(controller.document().primaryTextObject().sourceText, initial);
    QVERIFY(!controller.isModified());
    controller.undoStack()->redo();
    QCOMPARE(controller.document().primaryTextObject().sourceText, QStringLiteral("second"));
    QVERIFY(controller.isModified());
}

void CoreTests::controllerEffectCommandsAreGranular()
{
    EditorController controller;
    controller.addEffect(QStringLiteral("wave"));
    controller.addEffect(QStringLiteral("stretch"));
    QCOMPARE(controller.document().primaryTextObject().effects.size(), 2);
    QCOMPARE(controller.document().primaryTextObject().effects.at(0)->typeId(), QStringLiteral("wave"));
    QCOMPARE(controller.document().primaryTextObject().effects.at(1)->typeId(), QStringLiteral("stretch"));

    const int beforeParameter = controller.undoStack()->count();
    const double initialAmplitude = effectParameterValue(
        *controller.document().primaryTextObject().effects.at(0), QStringLiteral("amplitude"));
    controller.setEffectParameter(0, QStringLiteral("amplitude"), 0.2);
    controller.setEffectParameter(0, QStringLiteral("amplitude"), 0.4);
    QCOMPARE(controller.undoStack()->count(), beforeParameter + 1);
    QCOMPARE(effectParameterValue(
                 *controller.document().primaryTextObject().effects.at(0), QStringLiteral("amplitude")),
             0.4);
    controller.undoStack()->undo();
    QCOMPARE(effectParameterValue(
                 *controller.document().primaryTextObject().effects.at(0), QStringLiteral("amplitude")),
             initialAmplitude);
    controller.undoStack()->redo();
    QCOMPARE(effectParameterValue(
                 *controller.document().primaryTextObject().effects.at(0), QStringLiteral("amplitude")),
             0.4);

    controller.moveEffect(0, 1);
    QCOMPARE(controller.document().primaryTextObject().effects.at(0)->typeId(), QStringLiteral("stretch"));
    controller.undoStack()->undo();
    QCOMPARE(controller.document().primaryTextObject().effects.at(0)->typeId(), QStringLiteral("wave"));
    controller.removeEffect(0);
    QCOMPARE(controller.document().primaryTextObject().effects.size(), 1);
    controller.undoStack()->undo();
    QCOMPARE(controller.document().primaryTextObject().effects.size(), 2);
    QCOMPARE(controller.document().primaryTextObject().effects.at(0)->typeId(), QStringLiteral("wave"));
}

void CoreTests::controllerCleanStateFollowsUndoStack()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    EditorController controller;
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

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    CoreTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "core_tests.moc"
