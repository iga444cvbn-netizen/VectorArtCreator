#include "tests/support/invariant_checker.h"
#include "tests/support/semantic_equality.h"
#include "tests/support/semantic_geometry.h"
#include "tests/support/state_fingerprint.h"
#include "tests/support/workload_builder.h"

#include "core/effects/effect_registry.h"
#include "core/scene/object_frame.h"
#include "core/scene/scene_evaluator.h"
#include "core/serialization/project_serializer.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineF>
#include <QElapsedTimer>
#include <QTest>

#include <limits>
#include <memory>
#include <utility>

using namespace vt;

namespace {

VectorGeometry semanticGeometryFixture()
{
    VectorGeometry geometry;
    geometry.referenceHeight = 24.0;
    for (int index = 0; index < 3; ++index) {
        GeometryPiece piece;
        piece.path.addRect(QRectF(index * 30.0, 0.0, 20.0, 24.0));
        piece.sourceGlyphIndex = 10 + index;
        piece.sourceClusterStart = index * 2;
        piece.sourceClusterLength = 2;
        piece.sourceLineIndex = index % 2;
        piece.anchor = QPointF(index * 30.0 + 10.0, 12.0);
        piece.originalAnchor = piece.anchor;
        piece.opacityMultiplier = 1.0 - index * 0.2;
        piece.generationDepth = index;
        piece.generatorEffectId = QStringLiteral("generator-%1").arg(index);
        geometry.pieces.push_back(piece);
    }
    geometry.setReferenceBounds(QRectF(0.0, 0.0, 80.0, 24.0));
    geometry.recomputeBounds();
    return geometry;
}

TextObject semanticTextFixture()
{
    TextObject object;
    object.id = QStringLiteral("text-semantic-contract");
    object.sourceText = QString::fromUtf8("Привет\nSemantic");
    object.font.family = QStringLiteral("Contract Family");
    object.font.styleName = QStringLiteral("Contract Style");
    object.font.weight = 650;
    object.font.italic = true;
    object.font.underline = true;
    object.font.strikeOut = true;
    object.font.fingerprint = QStringLiteral("sha256:font-contract");
    object.font.embeddedResourceId = QStringLiteral("font-resource-contract");
    object.font.embeddingPermission = QStringLiteral("preview-print");
    object.typography.fontSize = 93.5;
    object.typography.trackingEm = -0.075;
    object.typography.lineSpacing = 1.42;
    object.fill = QColor(12, 34, 56, 178);
    object.effectStackStrength = 1.75;

    std::unique_ptr<Effect> effect = EffectRegistry::instance().create(QStringLiteral("wave"));
    Q_ASSERT(effect);
    effect->instanceId = QStringLiteral("effect-semantic-contract");
    effect->enabled = false;
    effect->masterStrength = 1.6;
    effect->scope = {EffectScopeKind::TextRange, 1, 6};
    EffectMaskStroke mask;
    mask.points = {QPointF(2.0, 3.0), QPointF(8.0, 13.0)};
    mask.radius = 17.0;
    mask.opacity = 0.73;
    mask.hardness = 0.29;
    mask.restore = true;
    effect->maskStrokes = {mask};
    effect->maskInverted = true;
    if (!effect->setParameter(QStringLiteral("amplitude"), 0.81)
        || !effect->setParameter(QStringLiteral("frequency"), 3.7)
        || !effect->setParameter(QStringLiteral("phase"), -0.23)) {
        qFatal("semantic effect fixture could not set Wave parameters");
    }
    object.effects.append(std::move(effect));

    object.deformation.enabled = false;
    object.deformation.strength = 1.33;
    DeformationStroke stroke;
    stroke.mode = BrushMode::Pinch;
    stroke.target = BrushTarget::Glyphs;
    stroke.radius = 51.0;
    stroke.strength = 0.44;
    stroke.hardness = 0.91;
    stroke.coordinateSpace = DeformationCoordinateSpace::ObjectLocal;
    stroke.samples = {{QPointF(4.0, 5.0), QPointF(1.0, -2.0), 0.61},
                      {QPointF(8.0, 9.0), QPointF(-3.0, 4.0), 0.87}};
    object.deformation.strokes = {stroke};

    object.transform.position = QPointF(137.0, -42.0);
    object.transform.rotation = 37.5;
    object.transform.scale = QPointF(-0.65, 1.27);
    object.transform.pivotLocal = QPointF(14.0, 19.0);
    object.transform.hasPivot = true;
    object.visible = false;
    object.futureData = {{QStringLiteral("futureFlag"), true},
                         {QStringLiteral("futureArray"), QJsonArray{1, 2, 3}}};
    return object;
}

Document semanticDocumentFixture()
{
    Document document;
    document.title = QStringLiteral("Semantic inventory");
    document.metadata = {{QStringLiteral("custom"), QStringLiteral("metadata")}};
    document.resources = {{QStringLiteral("font-resource-contract"),
                           QJsonObject{{QStringLiteral("kind"), QStringLiteral("font")}}}};
    Page* page = document.currentPage();
    Q_ASSERT(page && !page->layers.empty());
    page->id = QStringLiteral("page-semantic-contract");
    page->name = QStringLiteral("Contract Page");
    page->size = QSizeF(1440.5, 900.25);
    page->background = QColor(201, 202, 203, 204);
    Layer* layer = page->layers.front().get();
    layer->id = QStringLiteral("layer-semantic-contract");
    layer->name = QStringLiteral("Contract Layer");
    layer->visible = false;
    layer->locked = true;
    layer->objects.clear();
    layer->objects.push_back(std::make_unique<TextObject>(semanticTextFixture()));
    document.currentPageId = page->id;
    document.activeLayerId = layer->id;
    document.activeObjectId = layer->objects.front()->id;
    return document;
}

SceneObjectGeometry semanticSceneFixture()
{
    SceneObjectGeometry object;
    object.objectId = QStringLiteral("scene-object");
    object.pageId = QStringLiteral("scene-page");
    object.layerId = QStringLiteral("scene-layer");
    object.sourceText = QStringLiteral("Scene");
    object.transform.position = QPointF(40.0, 70.0);
    object.transform.rotation = 31.0;
    object.transform.scale = QPointF(-0.8, 1.2);
    object.transform.pivotLocal = QPointF(13.0, 7.0);
    object.transform.hasPivot = true;
    object.geometry = semanticGeometryFixture();
    object.frame = ObjectFrame::fromTransform(object.transform,
                                               object.geometry.referenceBounds,
                                               object.geometry.bounds);
    object.fill = QColor(10, 20, 30, 160);
    object.visible = true;
    object.locked = true;
    return object;
}

} // namespace

class OracleSelfTests final : public QObject {
    Q_OBJECT

private slots:
    void geometrySignatureRejectsMeaningfulDifferences();
    void sceneSignatureIncludesFrameAndPivot();
    void semanticCopyContractCoversPersistentInventory();
    void semanticFingerprintExcludesOnlyDeclaredTransientState();
    void invariantCheckerRejectsSyntheticCorruption();
    void currentSchemaLoaderRejectsIdentityCorruption();
    void frameRoundTripIsStableForStaticSnapshots();
    void workloadBuildersAreDeterministicAndInspectable();
};

void OracleSelfTests::geometrySignatureRejectsMeaningfulDifferences()
{
    const VectorGeometry source = semanticGeometryFixture();
    const test::GeometrySignature expected = test::geometrySignature(source);
    QString difference;
    QVERIFY2(test::compareGeometry(expected, test::geometrySignature(source), &difference),
             qPrintable(difference));
    QCOMPARE(test::geometryDigest(expected), test::geometryDigest(test::geometrySignature(source)));

    VectorGeometry pointChanged = source;
    const auto element = pointChanged.pieces[0].path.elementAt(1);
    pointChanged.pieces[0].path.setElementPositionAt(1, element.x, element.y + 0.125);
    QVERIFY(!test::compareGeometry(expected, test::geometrySignature(pointChanged), &difference));
    QVERIFY2(difference.contains(QStringLiteral("piece[0].path[1].position")), qPrintable(difference));

    VectorGeometry opacityChanged = source;
    opacityChanged.pieces[1].opacityMultiplier = 0.42;
    QVERIFY(!test::compareGeometry(expected, test::geometrySignature(opacityChanged), &difference));
    QVERIFY2(difference.contains(QStringLiteral("piece[1].opacity")), qPrintable(difference));

    VectorGeometry reordered = source;
    std::swap(reordered.pieces[0], reordered.pieces[1]);
    QVERIFY(!test::compareGeometry(expected, test::geometrySignature(reordered), &difference));
    QVERIFY2(difference.contains(QStringLiteral("piece[0]")), qPrintable(difference));
}

void OracleSelfTests::sceneSignatureIncludesFrameAndPivot()
{
    const SceneObjectGeometry source = semanticSceneFixture();
    const test::SceneObjectSignature expected = test::sceneObjectSignature(source);
    QString difference;
    QVERIFY2(test::compareSceneObject(expected, test::sceneObjectSignature(source), &difference),
             qPrintable(difference));

    SceneObjectGeometry changed = source;
    changed.transform.pivotLocal += QPointF(0.5, 0.0);
    changed.frame = ObjectFrame::fromTransform(changed.transform,
                                               changed.geometry.referenceBounds,
                                               changed.geometry.bounds);
    QVERIFY(!test::compareSceneObject(expected, test::sceneObjectSignature(changed), &difference));
    QVERIFY2(difference.contains(QStringLiteral("transform.pivotLocal")), qPrintable(difference));
}

void OracleSelfTests::semanticCopyContractCoversPersistentInventory()
{
    const TextObject original = semanticTextFixture();
    QString difference;
    const TextObject constructed(original);
    QVERIFY2(test::compareTextObjects(original, constructed, &difference), qPrintable(difference));

    TextObject assigned;
    assigned = original;
    QVERIFY2(test::compareTextObjects(original, assigned, &difference), qPrintable(difference));

    const Document document = semanticDocumentFixture();
    const Page& originalPage = *document.pages.front();
    const Page constructedPage(originalPage);
    QVERIFY2(test::comparePages(originalPage, constructedPage, &difference), qPrintable(difference));
    Page assignedPage;
    assignedPage = originalPage;
    QVERIFY2(test::comparePages(originalPage, assignedPage, &difference), qPrintable(difference));

    Document restored;
    QVERIFY2(ProjectSerializer::fromJson(ProjectSerializer::toJson(document), &restored, &difference),
             qPrintable(difference));
    QVERIFY2(test::semanticallyEqual(document, restored, &difference), qPrintable(difference));
    QVERIFY2(test::compareTextObjects(*document.pages.front()->layers.front()->objects.front(),
                                      *restored.pages.front()->layers.front()->objects.front(),
                                      &difference), qPrintable(difference));
}

void OracleSelfTests::semanticFingerprintExcludesOnlyDeclaredTransientState()
{
    const Document original = semanticDocumentFixture();
    Document transientChanged(original);
    transientChanged.createdAt = transientChanged.createdAt.addDays(-2);
    transientChanged.modifiedAt = transientChanged.modifiedAt.addSecs(7);
    transientChanged.activeObjectId.clear();
    QCOMPARE(test::semanticFingerprint(original), test::semanticFingerprint(transientChanged));

    Document persistentChanged(original);
    persistentChanged.pages.front()->layers.front()->objects.front()->effectStackStrength = 0.25;
    QVERIFY(test::semanticFingerprint(original) != test::semanticFingerprint(persistentChanged));
}

void OracleSelfTests::invariantCheckerRejectsSyntheticCorruption()
{
    Document valid = semanticDocumentFixture();
    QVERIFY2(test::checkInvariants(valid).ok(), qPrintable(test::checkInvariants(valid).summary()));

    Document duplicate(valid);
    duplicate.pages.front()->layers.front()->objects.push_back(
        std::make_unique<TextObject>(*duplicate.pages.front()->layers.front()->objects.front()));
    const auto duplicateReport = test::checkInvariants(duplicate);
    QVERIFY(!duplicateReport.ok());
    QVERIFY2(duplicateReport.summary().contains(QStringLiteral("duplicate or invalid object id")),
             qPrintable(duplicateReport.summary()));

    Document nonLocal(valid);
    auto secondPage = std::make_unique<Page>();
    secondPage->id = QStringLiteral("page-other");
    secondPage->layers.front()->id = QStringLiteral("layer-other");
    nonLocal.activeLayerId = secondPage->layers.front()->id;
    nonLocal.pages.push_back(std::move(secondPage));
    const auto localityReport = test::checkInvariants(nonLocal);
    QVERIFY(!localityReport.ok());
    QVERIFY2(localityReport.summary().contains(QStringLiteral("active layer is not on current page")),
             qPrintable(localityReport.summary()));

    Document notFinite(valid);
    notFinite.pages.front()->layers.front()->objects.front()->transform.rotation =
        std::numeric_limits<qreal>::quiet_NaN();
    const auto numericReport = test::checkInvariants(notFinite);
    QVERIFY(!numericReport.ok());
    QVERIFY2(numericReport.summary().contains(QStringLiteral("invalid transform")),
             qPrintable(numericReport.summary()));

    Document unsupportedMask(valid);
    std::unique_ptr<Effect> echo = EffectRegistry::instance().create(QStringLiteral("echo"));
    QVERIFY(echo);
    echo->instanceId = QStringLiteral("unsupported-mask-effect");
    EffectMaskStroke mask;
    mask.points = {QPointF(10.0, 10.0)};
    echo->maskStrokes = {mask};
    unsupportedMask.pages.front()->layers.front()->objects.front()->effects.append(std::move(echo));
    const auto capabilityReport = test::checkInvariants(unsupportedMask);
    QVERIFY(!capabilityReport.ok());
    QVERIFY2(capabilityReport.summary().contains(QStringLiteral("unsupported effect mask")),
             qPrintable(capabilityReport.summary()));
}

void OracleSelfTests::currentSchemaLoaderRejectsIdentityCorruption()
{
    const QJsonObject validRoot = ProjectSerializer::toJson(semanticDocumentFixture()).object();
    Document loaded;
    QString error;
    QVERIFY2(ProjectSerializer::fromJson(QJsonDocument(validRoot), &loaded, &error), qPrintable(error));

    auto expectRejected = [&loaded](const QJsonObject& root, const QString& expectedMessage) {
        const QString before = test::semanticFingerprint(loaded);
        QString loadError;
        const bool accepted = ProjectSerializer::fromJson(QJsonDocument(root), &loaded, &loadError);
        if (accepted || !loadError.contains(expectedMessage)
            || test::semanticFingerprint(loaded) != before) {
            qWarning().noquote() << "accepted=" << accepted << "error=" << loadError
                                 << "expected fragment=" << expectedMessage;
            return false;
        }
        return true;
    };

    QJsonObject missingObjectId = validRoot;
    QJsonArray pages = missingObjectId.value(QStringLiteral("pages")).toArray();
    QJsonObject page = pages.at(0).toObject();
    QJsonArray layers = page.value(QStringLiteral("layers")).toArray();
    QJsonObject layer = layers.at(0).toObject();
    QJsonArray objects = layer.value(QStringLiteral("objects")).toArray();
    QJsonObject object = objects.at(0).toObject();
    object.remove(QStringLiteral("id"));
    objects.replace(0, object);
    layer.insert(QStringLiteral("objects"), objects);
    layers.replace(0, layer);
    page.insert(QStringLiteral("layers"), layers);
    pages.replace(0, page);
    missingObjectId.insert(QStringLiteral("pages"), pages);
    QVERIFY(expectRejected(missingObjectId, QStringLiteral("missing or duplicate object ID")));

    QJsonObject duplicatePage = validRoot;
    pages = duplicatePage.value(QStringLiteral("pages")).toArray();
    pages.append(pages.at(0));
    duplicatePage.insert(QStringLiteral("pages"), pages);
    QVERIFY(expectRejected(duplicatePage, QStringLiteral("missing or duplicate page ID")));

    QJsonObject duplicateEffect = validRoot;
    pages = duplicateEffect.value(QStringLiteral("pages")).toArray();
    page = pages.at(0).toObject();
    layers = page.value(QStringLiteral("layers")).toArray();
    layer = layers.at(0).toObject();
    objects = layer.value(QStringLiteral("objects")).toArray();
    object = objects.at(0).toObject();
    object.insert(QStringLiteral("id"), QStringLiteral("text-second"));
    objects.append(object);
    layer.insert(QStringLiteral("objects"), objects);
    layers.replace(0, layer);
    page.insert(QStringLiteral("layers"), layers);
    pages.replace(0, page);
    duplicateEffect.insert(QStringLiteral("pages"), pages);
    QVERIFY(expectRejected(duplicateEffect, QStringLiteral("missing or duplicate effect ID")));

    QJsonObject nonLocalActiveLayer = validRoot;
    pages = nonLocalActiveLayer.value(QStringLiteral("pages")).toArray();
    QJsonObject otherPage = pages.at(0).toObject();
    otherPage.insert(QStringLiteral("id"), QStringLiteral("page-other"));
    QJsonArray otherLayers = otherPage.value(QStringLiteral("layers")).toArray();
    QJsonObject otherLayer = otherLayers.at(0).toObject();
    otherLayer.insert(QStringLiteral("id"), QStringLiteral("layer-other"));
    QJsonArray otherObjects = otherLayer.value(QStringLiteral("objects")).toArray();
    QJsonObject otherObject = otherObjects.at(0).toObject();
    otherObject.insert(QStringLiteral("id"), QStringLiteral("text-other"));
    QJsonArray otherEffects = otherObject.value(QStringLiteral("effects")).toArray();
    QJsonObject otherEffect = otherEffects.at(0).toObject();
    otherEffect.insert(QStringLiteral("id"), QStringLiteral("effect-other"));
    otherEffects.replace(0, otherEffect);
    otherObject.insert(QStringLiteral("effects"), otherEffects);
    otherObjects.replace(0, otherObject);
    otherLayer.insert(QStringLiteral("objects"), otherObjects);
    otherLayers.replace(0, otherLayer);
    otherPage.insert(QStringLiteral("layers"), otherLayers);
    pages.append(otherPage);
    nonLocalActiveLayer.insert(QStringLiteral("pages"), pages);
    nonLocalActiveLayer.insert(QStringLiteral("activeLayerId"), QStringLiteral("layer-other"));
    QVERIFY(expectRejected(nonLocalActiveLayer, QStringLiteral("active layer is not on its current page")));

    QJsonObject nonLocalActiveObject = nonLocalActiveLayer;
    nonLocalActiveObject.insert(QStringLiteral("activeLayerId"), QStringLiteral("layer-semantic-contract"));
    nonLocalActiveObject.insert(QStringLiteral("activeObjectId"), QStringLiteral("text-other"));
    QVERIFY(expectRejected(nonLocalActiveObject, QStringLiteral("active object is not on its current page")));
}

void OracleSelfTests::frameRoundTripIsStableForStaticSnapshots()
{
    const SceneObjectGeometry object = semanticSceneFixture();
    const QVector<QPointF> localPoints = {QPointF(0.0, 0.0), QPointF(13.0, 7.0),
                                          QPointF(80.0, 24.0), QPointF(-12.5, 41.25)};
    for (const QPointF& local : localPoints) {
        const QPointF page = object.frame.localPointToPage(local);
        const QPointF restored = object.frame.pagePointToLocal(page);
        QVERIFY2(QLineF(local, restored).length() < 1.0e-8,
                 qPrintable(QStringLiteral("frame round-trip drifted: local=(%1,%2), restored=(%3,%4)")
                                .arg(local.x()).arg(local.y()).arg(restored.x()).arg(restored.y())));
    }
}

void OracleSelfTests::workloadBuildersAreDeterministicAndInspectable()
{
    const QVector<test::WorkloadParameters> matrix = {
        {1, 0, 0, 0},
        {10, 4, 8, 2},
        {100, 2, 3, 1},
    };
    for (const test::WorkloadParameters& parameters : matrix) {
        const Document first = test::buildDeterministicWorkload(parameters);
        const Document replay = test::buildDeterministicWorkload(parameters);
        QCOMPARE(test::semanticFingerprint(first), test::semanticFingerprint(replay));
        const test::WorkloadSummary summary = test::inspectWorkload(first);
        QCOMPARE(summary.objects, parameters.objectCount);
        QCOMPARE(summary.maskStrokes,
                 parameters.maskSegmentsPerObject > 0 ? parameters.objectCount : 0);
        QCOMPARE(summary.maskPoints,
                 parameters.maskSegmentsPerObject > 0
                     ? parameters.objectCount * (parameters.maskSegmentsPerObject + 1) : 0);
        QCOMPARE(summary.deformationSamples,
                 parameters.objectCount * parameters.deformationSamplesPerObject);
        QCOMPARE(summary.requestedGeneratorCopies,
                 parameters.objectCount * parameters.generatorCopies);
        QVERIFY2(test::checkInvariants(first).ok(), qPrintable(test::checkInvariants(first).summary()));

        QElapsedTimer timer;
        timer.start();
        const SceneGeometry scene = SceneEvaluator::evaluate(*first.currentPage());
        const qint64 elapsed = timer.elapsed();
        QCOMPARE(scene.objects.size(), parameters.objectCount);
        qInfo().noquote() << QStringLiteral(
            "workload objects=%1 maskSegments/object=%2 deformationSamples/object=%3 generatorCopies=%4 elapsedMs=%5")
            .arg(parameters.objectCount).arg(parameters.maskSegmentsPerObject)
            .arg(parameters.deformationSamplesPerObject).arg(parameters.generatorCopies)
            .arg(elapsed);
    }
}

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    OracleSelfTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "oracle_self_tests.moc"
