#include "tests/support/geometry_assertions.h"
#include "tests/support/test_fonts.h"

#include "core/effects/effect_registry.h"
#include "core/effects/effect_stack.h"
#include "core/geometry/vector_geometry.h"
#include "core/presets/preset_catalog.h"
#include "core/presets/preset_manager.h"
#include "core/text/text_engine.h"

#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QGuiApplication>

using namespace vt;

namespace {

VectorGeometry contractGeometry()
{
    VectorGeometry geometry;
    for (int i = 0; i < 3; ++i) {
        GeometryPiece piece;
        piece.path.addRect(QRectF(i * 30.0, 0.0, 20.0, 24.0));
        piece.anchor = QPointF(i * 30.0 + 10.0, 12.0);
        piece.originalAnchor = piece.anchor;
        piece.sourceGlyphIndex = i;
        piece.sourceClusterStart = i;
        geometry.pieces.push_back(piece);
    }
    geometry.setReferenceBounds(QRectF(0, 0, 80, 24));
    geometry.recomputeBounds();
    return geometry;
}

const QSet<QString>& contractTypes()
{
    static const QSet<QString> types = {
        QStringLiteral("wave"), QStringLiteral("glyphJitter"), QStringLiteral("stretch"),
        QStringLiteral("bounce"), QStringLiteral("staircase"), QStringLiteral("randomOffset"),
        QStringLiteral("randomRotation"), QStringLiteral("randomScale"), QStringLiteral("horizontalSpread"),
        QStringLiteral("verticalSpread"), QStringLiteral("arc"), QStringLiteral("zigzag"),
        QStringLiteral("sineRotation"), QStringLiteral("crescendo"), QStringLiteral("shrink"),
        QStringLiteral("skew"), QStringLiteral("compression"), QStringLiteral("expandCenter"),
        QStringLiteral("squeezeCenter"), QStringLiteral("baselineDrift"), QStringLiteral("alternatingTilt"),
        QStringLiteral("bend"), QStringLiteral("sag"), QStringLiteral("waveWarp"), QStringLiteral("bulge"),
        QStringLiteral("pinch"), QStringLiteral("noiseWarp"), QStringLiteral("melt"), QStringLiteral("smear"),
        QStringLiteral("echo"), QStringLiteral("ghost"), QStringLiteral("afterimage")};
    return types;
}

} // namespace

class EffectContractTests final : public QObject {
    Q_OBJECT

private slots:
    void everyRegisteredEffectHasAnExplicitContract();
    void registeredEffectContract_data();
    void registeredEffectContract();
    void builtInPresetContract_data();
    void builtInPresetContract();
};

void EffectContractTests::everyRegisteredEffectHasAnExplicitContract()
{
    const auto& registry = EffectRegistry::instance();
    QString error;
    QVERIFY2(registry.validate(&error), qPrintable(error));
    for (const EffectDescriptor& descriptor : registry.descriptors()) {
        QVERIFY2(contractTypes().contains(descriptor.typeId),
                 qPrintable(QStringLiteral("New public effect needs a Phase 4T contract: %1").arg(descriptor.typeId)));
    }
}

void EffectContractTests::registeredEffectContract_data()
{
    QTest::addColumn<QString>("typeId");
    for (const EffectDescriptor& descriptor : EffectRegistry::instance().descriptors()) {
        QTest::newRow(descriptor.typeId.toUtf8().constData()) << descriptor.typeId;
    }
}

void EffectContractTests::registeredEffectContract()
{
    QFETCH(QString, typeId);
    const EffectDescriptor* descriptor = EffectRegistry::instance().descriptor(typeId);
    QVERIFY(descriptor);
    auto effect = EffectRegistry::instance().create(typeId);
    QVERIFY(effect);
    QCOMPARE(effect->typeId(), typeId);
    QCOMPARE(effect->domain(), descriptor->domain);
    for (const EffectParameter& parameter : effect->parameterDefinitions()) {
        QVERIFY(parameter.minimum <= parameter.value && parameter.value <= parameter.maximum);
        QVERIFY(effect->setParameter(parameter.id, parameter.minimum));
        QVERIFY(effect->setParameter(parameter.id, parameter.maximum));
    }
    EffectStack stack;
    stack.append(effect->clone());
    QString error;
    EffectStack restored = EffectStack::fromJson(stack.toJson(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(restored.size(), 1);
    VectorGeometry result = contractGeometry();
    restored.apply(result, 1.0);
    QVERIFY2(test::hasFiniteGeometry(result, &error), qPrintable(error));
    QVERIFY(result.pieces.size() <= 4096);
    VectorGeometry neutral = contractGeometry();
    restored.apply(neutral, 0.0);
    QVERIFY2(test::hasFiniteGeometry(neutral, &error), qPrintable(error));
}

void EffectContractTests::builtInPresetContract_data()
{
    QTest::addColumn<QString>("id");
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    PresetManager manager(temporary.path());
    PresetCatalog catalog(manager);
    QString diagnostics;
    const auto entries = catalog.entries(&diagnostics);
    QVERIFY2(diagnostics.isEmpty(), qPrintable(diagnostics));
    for (const PresetCatalogEntry& entry : entries) {
        if (entry.builtIn) QTest::newRow(entry.preset.id.toUtf8().constData()) << entry.preset.id;
    }
}

void EffectContractTests::builtInPresetContract()
{
    QFETCH(QString, id);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    PresetManager manager(temporary.path());
    PresetCatalog catalog(manager);
    PresetCatalogEntry entry;
    QString error;
    QVERIFY2(catalog.presetById(id, &entry, &error), qPrintable(error));
    QVERIFY(entry.builtIn);
    for (int i = 0; i < entry.preset.effects.size(); ++i) {
        const Effect* effect = entry.preset.effects.at(i);
        QVERIFY(effect);
        QVERIFY(EffectRegistry::instance().descriptor(effect->typeId()));
    }
    VectorGeometry geometry = contractGeometry();
    entry.preset.effects.apply(geometry, 1.0);
    QVERIFY2(test::hasFiniteGeometry(geometry, &error), qPrintable(error));
}

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    EffectContractTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "effect_contract_tests.moc"
