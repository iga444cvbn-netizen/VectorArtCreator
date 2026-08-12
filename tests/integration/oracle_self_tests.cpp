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
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
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

QJsonObject budgetObject(int sourceUnits = 1,
                         int effectCount = 0,
                         int maskStrokesPerEffect = 0,
                         int maskPointsPerStroke = 0,
                         int deformationStrokes = 0,
                         int samplesPerDeformationStroke = 0)
{
    QJsonArray effects;
    for (int effectIndex = 0; effectIndex < effectCount; ++effectIndex) {
        QJsonArray mask;
        for (int strokeIndex = 0; strokeIndex < maskStrokesPerEffect; ++strokeIndex) {
            QJsonArray points;
            for (int pointIndex = 0; pointIndex < maskPointsPerStroke; ++pointIndex) {
                points.append(QJsonObject{{QStringLiteral("x"), pointIndex},
                                          {QStringLiteral("y"), strokeIndex}});
            }
            mask.append(QJsonObject{{QStringLiteral("points"), points}});
        }
        effects.append(QJsonObject{{QStringLiteral("mask"), mask}});
    }
    QJsonArray strokes;
    for (int strokeIndex = 0; strokeIndex < deformationStrokes; ++strokeIndex) {
        QJsonArray samples;
        for (int sampleIndex = 0; sampleIndex < samplesPerDeformationStroke; ++sampleIndex) {
            samples.append(QJsonObject{});
        }
        strokes.append(QJsonObject{{QStringLiteral("samples"), samples}});
    }
    return {{QStringLiteral("sourceText"), QString(sourceUnits, QLatin1Char('x'))},
            {QStringLiteral("effects"), effects},
            {QStringLiteral("deformation"), QJsonObject{{QStringLiteral("strokes"), strokes}}}};
}

QJsonDocument budgetProject(int pageCount,
                            int layersPerPage,
                            int objectsPerLayer,
                            const QJsonObject& object = budgetObject())
{
    QJsonArray pages;
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        QJsonArray layers;
        for (int layerIndex = 0; layerIndex < layersPerPage; ++layerIndex) {
            QJsonArray objects;
            for (int objectIndex = 0; objectIndex < objectsPerLayer; ++objectIndex) {
                objects.append(object);
            }
            layers.append(QJsonObject{{QStringLiteral("objects"), objects}});
        }
        pages.append(QJsonObject{{QStringLiteral("layers"), layers}});
    }
    return QJsonDocument(QJsonObject{
        {QStringLiteral("formatVersion"), Document::CurrentFormatVersion},
        {QStringLiteral("pages"), pages},
    });
}

QJsonDocument budgetProjectWithObjects(const QJsonArray& objects)
{
    return QJsonDocument(QJsonObject{
        {QStringLiteral("formatVersion"), Document::CurrentFormatVersion},
        {QStringLiteral("pages"), QJsonArray{QJsonObject{
            {QStringLiteral("layers"), QJsonArray{QJsonObject{
                {QStringLiteral("objects"), objects},
            }}},
        }}},
    });
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
    void historicalIdentityMigrationIsDeterministic();
    void serializedResourceBudgetsHaveExactBoundaries();
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

    QJsonObject duplicateLayer = validRoot;
    pages = duplicateLayer.value(QStringLiteral("pages")).toArray();
    page = pages.at(0).toObject();
    layers = page.value(QStringLiteral("layers")).toArray();
    layers.append(layers.at(0));
    page.insert(QStringLiteral("layers"), layers);
    pages.replace(0, page);
    duplicateLayer.insert(QStringLiteral("pages"), pages);
    QVERIFY(expectRejected(duplicateLayer, QStringLiteral("pages[0].layers[1].id")));

    QJsonObject duplicateObject = validRoot;
    pages = duplicateObject.value(QStringLiteral("pages")).toArray();
    page = pages.at(0).toObject();
    layers = page.value(QStringLiteral("layers")).toArray();
    layer = layers.at(0).toObject();
    objects = layer.value(QStringLiteral("objects")).toArray();
    objects.append(objects.at(0));
    layer.insert(QStringLiteral("objects"), objects);
    layers.replace(0, layer);
    page.insert(QStringLiteral("layers"), layers);
    pages.replace(0, page);
    duplicateObject.insert(QStringLiteral("pages"), pages);
    QVERIFY(expectRejected(duplicateObject,
                           QStringLiteral("pages[0].layers[0].objects[1].id")));

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

    // Saving is the other persistence boundary. A synthetically corrupted live
    // value must be rejected before QSaveFile can replace a previously good file.
    Document malformedSave = semanticDocumentFixture();
    malformedSave.pages.push_back(std::make_unique<Page>(*malformedSave.pages.front()));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString savePath = directory.filePath(QStringLiteral("identity-contract.vta"));
    const QByteArray sentinel("previous-good-project");
    {
        QFile file(savePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(sentinel), qint64(sentinel.size()));
    }
    QString saveError;
    QVERIFY(!ProjectSerializer::saveToFile(malformedSave, savePath, &saveError));
    QVERIFY(saveError.contains(QStringLiteral("missing or duplicate page ID")));
    QFile unchanged(savePath);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), sentinel);
}

void OracleSelfTests::historicalIdentityMigrationIsDeterministic()
{
    QJsonObject legacyObject = ProjectSerializer::textObjectToJson(semanticTextFixture());
    legacyObject.insert(QStringLiteral("id"), QStringLiteral("ambiguous-legacy-id"));
    QJsonArray legacyEffects = legacyObject.value(QStringLiteral("effects")).toArray();
    QJsonObject legacyEffect = legacyEffects.at(0).toObject();
    legacyEffect.remove(QStringLiteral("id"));
    legacyEffects.replace(0, legacyEffect);
    legacyObject.insert(QStringLiteral("effects"), legacyEffects);
    const QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("vectorTypographyProject")},
        {QStringLiteral("formatVersion"), 3},
        {QStringLiteral("metadata"), QJsonObject{
            {QStringLiteral("createdAt"), QStringLiteral("2024-01-01T00:00:00.000Z")},
            {QStringLiteral("modifiedAt"), QStringLiteral("2024-01-01T00:00:00.000Z")},
        }},
        {QStringLiteral("objects"), QJsonArray{legacyObject, legacyObject}},
    };

    Document first;
    Document second;
    QString error;
    QVERIFY2(ProjectSerializer::fromJson(QJsonDocument(root), &first, &error), qPrintable(error));
    error.clear();
    QVERIFY2(ProjectSerializer::fromJson(QJsonDocument(root), &second, &error), qPrintable(error));
    QVERIFY2(test::checkInvariants(first).ok(), qPrintable(test::checkInvariants(first).summary()));
    QCOMPARE(first.currentPageId, QStringLiteral("migrated-v3-page-0"));
    QCOMPARE(first.activeLayerId, QStringLiteral("migrated-v3-page-0-layer-0"));
    QCOMPARE(first.pages.front()->layers.front()->objects.size(), size_t(2));

    QSet<QString> identities;
    for (int objectIndex = 0; objectIndex < 2; ++objectIndex) {
        const TextObject* left = first.pages.front()->layers.front()->objects.at(objectIndex).get();
        const TextObject* right = second.pages.front()->layers.front()->objects.at(objectIndex).get();
        QVERIFY(left && right);
        QCOMPARE(left->id, right->id);
        QCOMPARE(left->id,
                 QStringLiteral("migrated-v3-page-0-layer-0-object-%1").arg(objectIndex));
        QVERIFY(!identities.contains(left->id));
        identities.insert(left->id);
        QCOMPARE(left->effects.at(0)->instanceId, right->effects.at(0)->instanceId);
        QCOMPARE(left->effects.at(0)->instanceId,
                 QStringLiteral("migrated-v3-page-0-layer-0-object-%1-effect-0")
                     .arg(objectIndex));
        QVERIFY(!identities.contains(left->effects.at(0)->instanceId));
        identities.insert(left->effects.at(0)->instanceId);
    }

    // Versions that introduced the authoritative hierarchy never rename
    // collisions silently, including historical v4/v5 files.
    for (int version : {4, 5, Document::CurrentFormatVersion}) {
        QJsonObject hierarchical = ProjectSerializer::toJson(semanticDocumentFixture()).object();
        hierarchical.insert(QStringLiteral("formatVersion"), version);
        QJsonArray pages = hierarchical.value(QStringLiteral("pages")).toArray();
        pages.append(pages.at(0));
        hierarchical.insert(QStringLiteral("pages"), pages);
        const QString before = test::semanticFingerprint(first);
        error.clear();
        QVERIFY(!ProjectSerializer::fromJson(QJsonDocument(hierarchical), &first, &error));
        QVERIFY(error.contains(QStringLiteral("missing or duplicate page ID")));
        QCOMPARE(test::semanticFingerprint(first), before);
    }
}

void OracleSelfTests::serializedResourceBudgetsHaveExactBoundaries()
{
    QString error;
    const ProjectResourceLimits production = ProjectSerializer::resourceLimits();
    QVERIFY(ProjectSerializer::validateProjectInputSize(
        production.maximumProjectInputBytes, &error));
    QVERIFY(!ProjectSerializer::validateProjectInputSize(
        production.maximumProjectInputBytes + 1, &error));
    QVERIFY(error.contains(QStringLiteral("maximum")));
    error.clear();
    QVERIFY(ProjectSerializer::validateClipboardInputSize(
        production.maximumClipboardInputBytes, &error));
    QVERIFY(!ProjectSerializer::validateClipboardInputSize(
        production.maximumClipboardInputBytes + 1, &error));

    ProjectResourceLimits limits;
    limits.maximumPages = 100;
    limits.maximumLayersPerPage = 100;
    limits.maximumLayers = 100;
    limits.maximumObjectsPerLayer = 100;
    limits.maximumObjects = 100;
    limits.maximumEffectsPerObject = 100;
    limits.maximumEffects = 100;
    limits.maximumSourceUtf16PerObject = 100;
    limits.maximumSourceUtf16 = 100;
    limits.maximumMaskStrokesPerEffect = 100;
    limits.maximumMaskStrokes = 100;
    limits.maximumMaskPointsPerStroke = 100;
    limits.maximumMaskPoints = 100;
    limits.maximumDeformationStrokesPerObject = 100;
    limits.maximumDeformationStrokes = 100;
    limits.maximumDeformationSamplesPerStroke = 100;
    limits.maximumDeformationSamples = 100;
    limits.maximumEstimatedWork = 1'000'000;

    auto accepted = [](const QJsonDocument& document,
                       const ProjectResourceLimits& candidate,
                       const QString& diagnostic = QString()) {
        QString budgetError;
        const bool result = ProjectSerializer::validateResourceBudget(
            document, candidate, &budgetError);
        if (!result && !diagnostic.isEmpty() && !budgetError.contains(diagnostic)) {
            qWarning().noquote() << "unexpected budget diagnostic:" << budgetError
                                 << "expected:" << diagnostic;
        }
        if (diagnostic.isEmpty()) return result;
        return !result && budgetError.contains(diagnostic);
    };

    ProjectResourceLimits candidate = limits;
    candidate.maximumPages = 2;
    QVERIFY(accepted(budgetProject(2, 0, 0), candidate));
    QVERIFY(accepted(budgetProject(3, 0, 0), candidate, QStringLiteral("pages")));

    candidate = limits;
    candidate.maximumLayersPerPage = 2;
    QVERIFY(accepted(budgetProject(1, 2, 0), candidate));
    QVERIFY(accepted(budgetProject(1, 3, 0), candidate, QStringLiteral("layers per page")));

    candidate = limits;
    candidate.maximumLayers = 2;
    QVERIFY(accepted(budgetProject(2, 1, 0), candidate));
    QVERIFY(accepted(budgetProject(3, 1, 0), candidate, QStringLiteral("aggregate layers")));

    candidate = limits;
    candidate.maximumObjectsPerLayer = 2;
    QVERIFY(accepted(budgetProject(1, 1, 2), candidate));
    QVERIFY(accepted(budgetProject(1, 1, 3), candidate, QStringLiteral("objects per layer")));

    candidate = limits;
    candidate.maximumObjects = 2;
    QVERIFY(accepted(budgetProject(1, 2, 1), candidate));
    QVERIFY(accepted(budgetProject(1, 3, 1), candidate, QStringLiteral("aggregate objects")));

    candidate = limits;
    candidate.maximumEffectsPerObject = 2;
    QVERIFY(accepted(budgetProject(1, 1, 1, budgetObject(1, 2)), candidate));
    QVERIFY(accepted(budgetProject(1, 1, 1, budgetObject(1, 3)), candidate,
                     QStringLiteral("effects per object")));

    candidate = limits;
    candidate.maximumEffects = 2;
    QVERIFY(accepted(budgetProjectWithObjects(
        QJsonArray{budgetObject(1, 1), budgetObject(1, 1)}), candidate));
    QVERIFY(accepted(budgetProjectWithObjects(
        QJsonArray{budgetObject(1, 1), budgetObject(1, 2)}), candidate,
                     QStringLiteral("aggregate effects")));

    candidate = limits;
    candidate.maximumSourceUtf16PerObject = 2;
    QVERIFY(accepted(budgetProject(1, 1, 1, budgetObject(2)), candidate));
    QVERIFY(accepted(budgetProject(1, 1, 1, budgetObject(3)), candidate,
                     QStringLiteral("UTF-16 code units")));

    candidate = limits;
    candidate.maximumSourceUtf16 = 4;
    QVERIFY(accepted(budgetProjectWithObjects(
        QJsonArray{budgetObject(2), budgetObject(2)}), candidate));
    QVERIFY(accepted(budgetProjectWithObjects(
        QJsonArray{budgetObject(2), budgetObject(3)}), candidate,
                     QStringLiteral("aggregate UTF-16")));

    candidate = limits;
    candidate.maximumMaskStrokesPerEffect = 2;
    QVERIFY(accepted(budgetProject(1, 1, 1, budgetObject(1, 1, 2)), candidate));
    QVERIFY(accepted(budgetProject(1, 1, 1, budgetObject(1, 1, 3)), candidate,
                     QStringLiteral("mask strokes per effect")));

    candidate = limits;
    candidate.maximumMaskStrokes = 2;
    QVERIFY(accepted(budgetProjectWithObjects(
        QJsonArray{budgetObject(1, 1, 1), budgetObject(1, 1, 1)}), candidate));
    QVERIFY(accepted(budgetProjectWithObjects(
        QJsonArray{budgetObject(1, 1, 1), budgetObject(1, 1, 2)}), candidate,
                     QStringLiteral("aggregate mask strokes")));

    candidate = limits;
    candidate.maximumMaskPointsPerStroke = 2;
    QVERIFY(accepted(budgetProject(1, 1, 1, budgetObject(1, 1, 1, 2)), candidate));
    QVERIFY(accepted(budgetProject(1, 1, 1, budgetObject(1, 1, 1, 3)), candidate,
                     QStringLiteral("mask points per stroke")));

    candidate = limits;
    candidate.maximumMaskPoints = 4;
    QVERIFY(accepted(budgetProjectWithObjects(
        QJsonArray{budgetObject(1, 1, 1, 2), budgetObject(1, 1, 1, 2)}), candidate));
    QVERIFY(accepted(budgetProjectWithObjects(
        QJsonArray{budgetObject(1, 1, 1, 2), budgetObject(1, 1, 1, 3)}), candidate,
                     QStringLiteral("aggregate mask points")));

    candidate = limits;
    candidate.maximumDeformationStrokesPerObject = 2;
    QVERIFY(accepted(budgetProject(1, 1, 1, budgetObject(1, 0, 0, 0, 2, 1)), candidate));
    QVERIFY(accepted(budgetProject(1, 1, 1, budgetObject(1, 0, 0, 0, 3, 1)), candidate,
                     QStringLiteral("deformation strokes per object")));

    candidate = limits;
    candidate.maximumDeformationStrokes = 2;
    QVERIFY(accepted(budgetProjectWithObjects(QJsonArray{
        budgetObject(1, 0, 0, 0, 1, 1), budgetObject(1, 0, 0, 0, 1, 1)}), candidate));
    QVERIFY(accepted(budgetProjectWithObjects(QJsonArray{
        budgetObject(1, 0, 0, 0, 1, 1), budgetObject(1, 0, 0, 0, 2, 1)}), candidate,
                     QStringLiteral("aggregate deformation strokes")));

    candidate = limits;
    candidate.maximumDeformationSamplesPerStroke = 2;
    QVERIFY(accepted(budgetProject(1, 1, 1, budgetObject(1, 0, 0, 0, 1, 2)), candidate));
    QVERIFY(accepted(budgetProject(1, 1, 1, budgetObject(1, 0, 0, 0, 1, 3)), candidate,
                     QStringLiteral("deformation samples per stroke")));

    candidate = limits;
    candidate.maximumDeformationSamples = 4;
    QVERIFY(accepted(budgetProjectWithObjects(QJsonArray{
        budgetObject(1, 0, 0, 0, 1, 2), budgetObject(1, 0, 0, 0, 1, 2)}), candidate));
    QVERIFY(accepted(budgetProjectWithObjects(QJsonArray{
        budgetObject(1, 0, 0, 0, 1, 2), budgetObject(1, 0, 0, 0, 1, 3)}), candidate,
                     QStringLiteral("aggregate deformation samples")));

    candidate = limits;
    candidate.maximumEstimatedWork = 8;
    QVERIFY(accepted(budgetProject(1, 1, 1, budgetObject(1, 1, 1, 6)), candidate));
    QVERIFY(accepted(budgetProject(1, 1, 1, budgetObject(1, 1, 1, 7)), candidate,
                     QStringLiteral("estimated geometry work")));

    // Production loader gate: every child remains under its local limit, but
    // the composed text x mask workload is one aggregate operation too large.
    QJsonObject adversarial = ProjectSerializer::toJson(semanticDocumentFixture()).object();
    QJsonArray pages = adversarial.value(QStringLiteral("pages")).toArray();
    QJsonObject page = pages.at(0).toObject();
    QJsonArray layers = page.value(QStringLiteral("layers")).toArray();
    QJsonObject layer = layers.at(0).toObject();
    QJsonArray objects = layer.value(QStringLiteral("objects")).toArray();
    QJsonObject object = objects.at(0).toObject();
    object.insert(QStringLiteral("sourceText"), QString(2000, QLatin1Char('x')));
    QJsonArray effects = object.value(QStringLiteral("effects")).toArray();
    QJsonObject effect = effects.at(0).toObject();
    QJsonArray mask;
    for (int strokeIndex = 0; strokeIndex < 2; ++strokeIndex) {
        QJsonArray points;
        for (int pointIndex = 0; pointIndex < 2000; ++pointIndex) {
            points.append(QJsonObject{{QStringLiteral("x"), pointIndex},
                                      {QStringLiteral("y"), strokeIndex}});
        }
        mask.append(QJsonObject{{QStringLiteral("points"), points}});
    }
    effect.insert(QStringLiteral("mask"), mask);
    effects.replace(0, effect);
    object.insert(QStringLiteral("effects"), effects);
    objects.replace(0, object);
    layer.insert(QStringLiteral("objects"), objects);
    layers.replace(0, layer);
    page.insert(QStringLiteral("layers"), layers);
    pages.replace(0, page);
    adversarial.insert(QStringLiteral("pages"), pages);
    Document destination = semanticDocumentFixture();
    const QString before = test::semanticFingerprint(destination);
    error.clear();
    QVERIFY(!ProjectSerializer::fromJson(QJsonDocument(adversarial), &destination, &error));
    QVERIFY(error.contains(QStringLiteral("estimated geometry work")));
    QCOMPARE(test::semanticFingerprint(destination), before);

    QJsonObject transientObject = budgetObject(1, 0, 0, 0, 1, 1);
    QJsonObject deformation = transientObject.value(QStringLiteral("deformation")).toObject();
    QJsonArray transientStrokes = deformation.value(QStringLiteral("strokes")).toArray();
    QJsonObject transientStroke = transientStrokes.at(0).toObject();
    transientStroke.insert(QStringLiteral("coordinateSpace"), QStringLiteral("pageInput"));
    transientStrokes.replace(0, transientStroke);
    deformation.insert(QStringLiteral("strokes"), transientStrokes);
    transientObject.insert(QStringLiteral("deformation"), deformation);
    error.clear();
    QVERIFY(!ProjectSerializer::validateResourceBudget(
        QJsonDocument(QJsonArray{transientObject}), production, &error));
    QVERIFY(error.contains(QStringLiteral("cannot be persisted")));
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
