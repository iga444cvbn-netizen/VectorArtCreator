#include "core/document/document.h"
#include "core/effects/glyph_jitter_effect.h"
#include "core/effects/stretch_effect.h"
#include "core/effects/wave_effect.h"
#include "core/export/svg_exporter.h"
#include "core/presets/preset.h"
#include "core/serialization/project_serializer.h"
#include "core/text/text_engine.h"
#include "ui/editor_controller.h"

#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>

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
    object.typography.tracking = 2.0;
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

} // namespace

class CoreTests final : public QObject {
    Q_OBJECT

private slots:
    void projectSerializationRoundTrip();
    void presetSerializationRoundTrip();
    void deterministicJitter();
    void effectOrderingIsDeterministic();
    void textReplacementPreservesEffects();
    void presetApplicationClonesEffects();
    void svgExportContainsPaths();
    void cyrillicTextProducesGeometry();
    void controllerUndoRedoAndMerge();
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

    QString error;
    Document restored;
    QVERIFY(ProjectSerializer::fromJson(ProjectSerializer::toJson(original), &restored, &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(restored.title, original.title);
    QCOMPARE(restored.primaryTextObject().sourceText, object.sourceText);
    QVERIFY(restored.primaryTextObject().font == object.font);
    QCOMPARE(restored.primaryTextObject().typography.fontSize, object.typography.fontSize);
    QCOMPARE(restored.primaryTextObject().typography.tracking, object.typography.tracking);
    QCOMPARE(restored.primaryTextObject().fill, object.fill);
    QCOMPARE(restored.primaryTextObject().effects.size(), 2);
    QCOMPARE(restored.primaryTextObject().effects.at(0)->typeId(), QStringLiteral("wave"));
    QCOMPARE(restored.primaryTextObject().effects.at(1)->enabled, false);
    const auto* restoredJitter = dynamic_cast<const GlyphJitterEffect*>(restored.primaryTextObject().effects.at(1));
    QVERIFY(restoredJitter);
    QCOMPARE(restoredJitter->seed, 987654321U);
}

void CoreTests::presetSerializationRoundTrip()
{
    Preset original;
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
    QCOMPARE(restored.name, original.name);
    QCOMPARE(restored.effects.size(), 2);
    const auto* restoredStretch = dynamic_cast<const StretchEffect*>(restored.effects.at(1));
    QVERIFY(restoredStretch);
    QCOMPARE(restoredStretch->horizontal, 1.8);
    QCOMPARE(restoredStretch->vertical, 0.75);
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
    TextEngine engine;
    const ShapedText shaped = engine.shape(object);
    QVERIFY2(shaped.error.isEmpty(), qPrintable(shaped.error));
    const VectorGeometry geometry = GlyphGeometryBuilder::build(shaped, object.typography.fontSize);
    QVERIFY(geometry.hasVisibleGeometry());
    QVERIFY(!geometry.combinedPath().isEmpty());
}

void CoreTests::controllerUndoRedoAndMerge()
{
    EditorController controller;
    const QString initial = controller.document().primaryTextObject().sourceText;
    controller.setText(QStringLiteral("first"));
    controller.setText(QStringLiteral("second"));
    QCOMPARE(controller.undoStack()->count(), 1);
    QCOMPARE(controller.document().primaryTextObject().sourceText, QStringLiteral("second"));

    controller.undoStack()->undo();
    QCOMPARE(controller.document().primaryTextObject().sourceText, initial);
    controller.undoStack()->redo();
    QCOMPARE(controller.document().primaryTextObject().sourceText, QStringLiteral("second"));
}

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    CoreTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "core_tests.moc"
