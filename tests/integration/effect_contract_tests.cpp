#include "tests/support/geometry_assertions.h"
#include "tests/support/semantic_equality.h"
#include "tests/support/semantic_geometry.h"
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

void configureRepresentative(Effect* effect)
{
    QVERIFY(effect);
    const QString typeId = effect->typeId();
    for (const EffectParameter& parameter : effect->parameterDefinitions()) {
        double value = parameter.value;
        if (parameter.id == QStringLiteral("seed")) value = 424242.0;
        else if (parameter.id == QStringLiteral("copyCount")) value = 2.0;
        else if (parameter.id == QStringLiteral("horizontal")) value = 1.6;
        else if (parameter.id == QStringLiteral("vertical")) value = 0.7;
        else if (parameter.id == QStringLiteral("amplitude")) value = 0.55;
        else if (parameter.id == QStringLiteral("phase")) value = 0.21;
        else if (parameter.id == QStringLiteral("frequency")) value = 1.7;
        else if (parameter.id == QStringLiteral("amount")
                 || parameter.id == QStringLiteral("strength")) value = 0.65;
        else if (parameter.id == QStringLiteral("offsetX")) value = 0.22;
        else if (parameter.id == QStringLiteral("offsetY")) value = 0.14;
        else if (parameter.id == QStringLiteral("rotationStep")) value = 11.0;
        else if (parameter.id == QStringLiteral("scaleStep")) value = 0.88;
        else if (parameter.id == QStringLiteral("opacityDecay")) value = 0.62;
        else if (parameter.minimum < 0.0 && parameter.maximum > 0.0) value = parameter.maximum * 0.6;
        else value = parameter.minimum + (parameter.maximum - parameter.minimum) * 0.63;
        QVERIFY2(effect->setParameter(parameter.id, value), qPrintable(typeId + QLatin1Char(':') + parameter.id));
    }
}

VectorGeometry applySingle(const Effect& effect, qreal stackStrength = 1.0)
{
    EffectStack stack;
    stack.append(effect.clone());
    VectorGeometry geometry = contractGeometry();
    stack.apply(geometry, stackStrength);
    return geometry;
}

bool samePiece(const VectorGeometry& left, int leftIndex,
               const VectorGeometry& right, int rightIndex)
{
    return test::geometrySignature(left).pieces.at(leftIndex)
        == test::geometrySignature(right).pieces.at(rightIndex);
}

} // namespace

class EffectContractTests final : public QObject {
    Q_OBJECT

private slots:
    void everyRegisteredEffectHasAnExplicitContract();
    void registeredEffectContract_data();
    void registeredEffectContract();
    void maskAndTextRangeRespectDescriptorClaims_data();
    void maskAndTextRangeRespectDescriptorClaims();
    void waveAndStretchHaveDirectionalMagnitude();
    void deterministicSeedsAndGeneratorMetadata();
    void effectOrderIsSemanticallyNonCommutative();
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
    QCOMPARE(effect->generatesGeometry(), descriptor->domain == EffectDomain::Generator);
    QVERIFY(!descriptor->displayName.isEmpty());
    QVERIFY(!descriptor->shortDescription.isEmpty());
    QVERIFY(descriptor->recommendedMasterStrengthRange.first == 0.0);
    QVERIFY(descriptor->recommendedMasterStrengthRange.second >= 1.0);

    const QVector<EffectParameter> definitions = effect->parameterDefinitions();
    QSet<QString> parameterIds;
    for (const EffectParameter& parameter : definitions) {
        QVERIFY(!parameter.id.isEmpty());
        QVERIFY(!parameterIds.contains(parameter.id));
        parameterIds.insert(parameter.id);
        QVERIFY(parameter.minimum <= parameter.value && parameter.value <= parameter.maximum);
        QVERIFY(parameter.step > 0.0);
        for (double boundary : {parameter.minimum, parameter.maximum}) {
            auto boundaryEffect = descriptor->factory();
            QVERIFY(boundaryEffect->setParameter(parameter.id, boundary));
            VectorGeometry boundaryResult = applySingle(*boundaryEffect);
            QString boundaryError;
            QVERIFY2(test::hasFiniteGeometry(boundaryResult, &boundaryError), qPrintable(boundaryError));
            QVERIFY(boundaryResult.pieces.size() <= 4096);
        }
    }
    for (const QString& basicParameterId : descriptor->basicParameterIds) {
        QVERIFY2(parameterIds.contains(basicParameterId),
                 qPrintable(QStringLiteral("Descriptor %1 advertises unknown basic parameter %2")
                                .arg(typeId, basicParameterId)));
    }

    configureRepresentative(effect.get());
    effect->instanceId = QStringLiteral("contract-") + typeId;
    effect->masterStrength = 1.35;
    EffectStack stack;
    stack.append(effect->clone());
    QString error;
    EffectStack restored = EffectStack::fromJson(stack.toJson(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(restored.size(), 1);
    QVERIFY2(test::compareEffects(*effect, *restored.at(0), &error), qPrintable(error));

    const VectorGeometry base = contractGeometry();
    VectorGeometry neutral = contractGeometry();
    restored.apply(neutral, 0.0);
    QVERIFY2(test::compareGeometry(test::geometrySignature(base), test::geometrySignature(neutral), &error),
             qPrintable(QStringLiteral("%1 violates exact stack-strength-zero identity:\n%2").arg(typeId, error)));

    auto disabledEffect = effect->clone();
    disabledEffect->enabled = false;
    VectorGeometry disabled = applySingle(*disabledEffect, descriptor->recommendedMasterStrengthRange.second);
    QVERIFY2(test::compareGeometry(test::geometrySignature(base), test::geometrySignature(disabled), &error),
             qPrintable(QStringLiteral("%1 violates disabled identity:\n%2").arg(typeId, error)));

    VectorGeometry result = contractGeometry();
    restored.apply(result, 1.0);
    QVERIFY2(test::hasFiniteGeometry(result, &error), qPrintable(error));
    QVERIFY(result.pieces.size() <= 4096);
    QVERIFY2(!test::compareGeometry(test::geometrySignature(base), test::geometrySignature(result), &error),
             qPrintable(QStringLiteral("Representative %1 effect is a semantic no-op").arg(typeId)));
    if (descriptor->preservesPieceCount) QCOMPARE(result.pieces.size(), base.pieces.size());
    if (descriptor->preservesPathTopology) {
        for (int index = 0; index < base.pieces.size(); ++index) {
            QCOMPARE(result.pieces.at(index).path.elementCount(), base.pieces.at(index).path.elementCount());
        }
    }

    if (descriptor->deterministic) {
        VectorGeometry replay = contractGeometry();
        restored.apply(replay, 1.0);
        QVERIFY2(test::compareGeometry(test::geometrySignature(result), test::geometrySignature(replay), &error),
                 qPrintable(QStringLiteral("%1 deterministic replay mismatch:\n%2").arg(typeId, error)));
    }

    VectorGeometry intermediate = contractGeometry();
    restored.apply(intermediate, 0.5);
    VectorGeometry maximum = contractGeometry();
    restored.apply(maximum, descriptor->recommendedMasterStrengthRange.second);
    QVERIFY(!test::compareGeometry(test::geometrySignature(base), test::geometrySignature(intermediate)));
    QVERIFY(!test::compareGeometry(test::geometrySignature(intermediate), test::geometrySignature(maximum)));
}

void EffectContractTests::maskAndTextRangeRespectDescriptorClaims_data()
{
    QTest::addColumn<QString>("typeId");
    for (const EffectDescriptor& descriptor : EffectRegistry::instance().descriptors()) {
        QTest::newRow(descriptor.typeId.toUtf8().constData()) << descriptor.typeId;
    }
}

void EffectContractTests::maskAndTextRangeRespectDescriptorClaims()
{
    QFETCH(QString, typeId);
    const EffectDescriptor* descriptor = EffectRegistry::instance().descriptor(typeId);
    QVERIFY(descriptor);
    auto effect = descriptor->factory();
    configureRepresentative(effect.get());
    effect->instanceId = QStringLiteral("target-") + typeId;

    const VectorGeometry base = contractGeometry();
    if (descriptor->supportsTextRange) {
        effect->scope = {EffectScopeKind::TextRange, 0, 2};
        const VectorGeometry ranged = applySingle(*effect);
        if (effect->generatesGeometry()) {
            QVERIFY(ranged.pieces.size() > base.pieces.size());
            for (int index = base.pieces.size(); index < ranged.pieces.size(); ++index) {
                QCOMPARE(ranged.pieces.at(index).sourceGlyphIndex, base.pieces.at(0).sourceGlyphIndex);
            }
        } else {
            QVERIFY(!samePiece(base, 0, ranged, 0));
            QVERIFY(samePiece(base, 1, ranged, 1));
            QVERIFY(samePiece(base, 2, ranged, 2));
        }
    }

    if (descriptor->supportsMask) {
        effect->scope = {};
        EffectMaskStroke mask;
        mask.points = {base.pieces.at(0).path.boundingRect().center()};
        mask.radius = 8.0;
        mask.opacity = 1.0;
        mask.hardness = 1.0;
        effect->maskStrokes = {mask};
        const VectorGeometry masked = applySingle(*effect);
        QCOMPARE(masked.pieces.size(), base.pieces.size());
        QVERIFY2(samePiece(base, 0, masked, 0), qPrintable(typeId + QStringLiteral(" ignored its mask")));
        QVERIFY(!samePiece(base, 1, masked, 1) || !samePiece(base, 2, masked, 2));
    }
}

void EffectContractTests::waveAndStretchHaveDirectionalMagnitude()
{
    auto wave = EffectRegistry::instance().create(QStringLiteral("wave"));
    QVERIFY(wave->setParameter(QStringLiteral("amplitude"), 0.5));
    QVERIFY(wave->setParameter(QStringLiteral("frequency"), 1.0));
    QVERIFY(wave->setParameter(QStringLiteral("phase"), 0.25));
    const VectorGeometry base = contractGeometry();
    const VectorGeometry halfWave = applySingle(*wave, 0.5);
    const VectorGeometry fullWave = applySingle(*wave, 1.0);
    bool observed = false;
    for (int index = 0; index < base.pieces.size(); ++index) {
        QCOMPARE(fullWave.pieces.at(index).anchor.x(), base.pieces.at(index).anchor.x());
        const qreal halfDelta = halfWave.pieces.at(index).anchor.y() - base.pieces.at(index).anchor.y();
        const qreal fullDelta = fullWave.pieces.at(index).anchor.y() - base.pieces.at(index).anchor.y();
        if (qAbs(fullDelta) > 1.0e-6) {
            observed = true;
            QVERIFY(qAbs(fullDelta - halfDelta * 2.0) < 1.0e-8);
        }
    }
    QVERIFY(observed);

    auto stretch = EffectRegistry::instance().create(QStringLiteral("stretch"));
    QVERIFY(stretch->setParameter(QStringLiteral("horizontal"), 1.5));
    QVERIFY(stretch->setParameter(QStringLiteral("vertical"), 0.7));
    const VectorGeometry stretched = applySingle(*stretch, 1.0);
    const VectorGeometry amplified = applySingle(*stretch, 2.0);
    QVERIFY(stretched.bounds.width() > base.bounds.width());
    QVERIFY(stretched.bounds.height() < base.bounds.height());
    QVERIFY(amplified.bounds.width() > stretched.bounds.width());
    QVERIFY(amplified.bounds.height() < stretched.bounds.height());
}

void EffectContractTests::deterministicSeedsAndGeneratorMetadata()
{
    for (const QString& typeId : {QStringLiteral("glyphJitter"), QStringLiteral("noiseWarp")}) {
        auto first = EffectRegistry::instance().create(typeId);
        configureRepresentative(first.get());
        QVERIFY(first->setParameter(QStringLiteral("seed"), 111.0));
        auto second = first->clone();
        const VectorGeometry a = applySingle(*first);
        const VectorGeometry b = applySingle(*second);
        QVERIFY(test::compareGeometry(test::geometrySignature(a), test::geometrySignature(b)));
        QVERIFY(second->setParameter(QStringLiteral("seed"), 222.0));
        const VectorGeometry c = applySingle(*second);
        QVERIFY(!test::compareGeometry(test::geometrySignature(a), test::geometrySignature(c)));
    }

    const VectorGeometry base = contractGeometry();
    for (const QString& typeId : {QStringLiteral("echo"), QStringLiteral("ghost"),
                                  QStringLiteral("afterimage")}) {
        auto generator = EffectRegistry::instance().create(typeId);
        configureRepresentative(generator.get());
        generator->instanceId = QStringLiteral("generator-") + typeId;
        const VectorGeometry generated = applySingle(*generator);
        QCOMPARE(generated.pieces.size(), base.pieces.size() * 3);
        for (int index = 0; index < base.pieces.size(); ++index) {
            QVERIFY(samePiece(base, index, generated, index));
        }
        for (int index = base.pieces.size(); index < generated.pieces.size(); ++index) {
            const GeometryPiece& piece = generated.pieces.at(index);
            QCOMPARE(piece.generationDepth, 1);
            QCOMPARE(piece.generatorEffectId, generator->instanceId);
            QVERIFY(piece.opacityMultiplier > 0.0 && piece.opacityMultiplier <= 1.0);
            QVERIFY(piece.sourceGlyphIndex >= 0);
            QVERIFY(piece.sourceClusterStart >= 0);
        }
    }
}

void EffectContractTests::effectOrderIsSemanticallyNonCommutative()
{
    auto applyOrder = [](const QString& firstType, const QString& secondType) {
        EffectStack stack;
        auto first = EffectRegistry::instance().create(firstType);
        auto second = EffectRegistry::instance().create(secondType);
        configureRepresentative(first.get());
        configureRepresentative(second.get());
        first->instanceId = QStringLiteral("ordered-") + firstType;
        second->instanceId = QStringLiteral("ordered-") + secondType;
        stack.append(std::move(first));
        stack.append(std::move(second));
        VectorGeometry result = contractGeometry();
        stack.apply(result, 1.0);
        return result;
    };

    const VectorGeometry stretchThenBend = applyOrder(QStringLiteral("stretch"), QStringLiteral("bend"));
    const VectorGeometry bendThenStretch = applyOrder(QStringLiteral("bend"), QStringLiteral("stretch"));
    QVERIFY(!test::compareGeometry(test::geometrySignature(stretchThenBend),
                                   test::geometrySignature(bendThenStretch)));

    const VectorGeometry meltThenEcho = applyOrder(QStringLiteral("melt"), QStringLiteral("echo"));
    const VectorGeometry echoThenMelt = applyOrder(QStringLiteral("echo"), QStringLiteral("melt"));
    QVERIFY(!test::compareGeometry(test::geometrySignature(meltThenEcho),
                                   test::geometrySignature(echoThenMelt)));
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
