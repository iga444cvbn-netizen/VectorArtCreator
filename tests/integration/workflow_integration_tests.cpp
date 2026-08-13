#include "tests/support/async_evaluation_gate.h"
#include "tests/support/invariant_checker.h"
#include "tests/support/semantic_equality.h"
#include "tests/support/semantic_geometry.h"
#include "tests/support/state_fingerprint.h"

#include "core/effects/effect_registry.h"
#include "core/export/export_payload_builder.h"
#include "core/export/svg_exporter.h"
#include "core/scene/scene_evaluator.h"
#include "core/serialization/project_serializer.h"
#include "ui/editor_controller.h"

#include <QCoreApplication>
#include <QClipboard>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QLineF>
#include <QMimeData>
#include <QRandomGenerator>
#include <QSet>
#include <QSemaphore>
#include <QThreadPool>
#include <QTest>
#include <QTemporaryDir>
#include <QtConcurrentRun>

#include <atomic>
#include <cmath>

using namespace vt;

class WorkflowIntegrationTests final : public QObject {
    Q_OBJECT

private slots:
    void semanticSaveLoadAndUndoRedoEquivalence();
    void latestAsyncTextGenerationWins();
    void latestAsyncSemanticSnapshotWins();
    void mixedRapidMutationsPublishOnlyFinalSemanticScene();
    void staleFrameCannotAuthorizeSpatialMutation();
    void transientPreviewNeverBecomesDocumentOrFrameAuthority();
    void cooperativeWorkBudgetAndCancellationAreDeterministic();
    void cancelledSvgNeverCommitsPartialOutput();
    void pageSwitchRejectsLatePreviousPage();
    void deletedObjectCannotBeRepublished();
    void controllerDestructionWithQueuedEvaluationIsSafe();
    void duplicatePageFreshensEntireIdentityHierarchy();
    void mergeableCommandReturningToStartRestoresClean_data();
    void mergeableCommandReturningToStartRestoresClean();
    void realFileLifecyclePreservesComplexSemantics();
    void exportPlainTextPreservesEqualObjectMultiplicity();
    void unsupportedEffectMaskIsRefusedWithoutMutation_data();
    void unsupportedEffectMaskIsRefusedWithoutMutation();
    void duplicatePasteAndPresetFreshenEffectIdentities();
    void effectReorderDeleteUndoRestoresSemanticOrder();
    void legacyV1V2V3MigrationSurvivesSaveReloadAndUndoRedo();
    void malformedOrOversizedClipboardPasteIsTransactional();
    void pathTypographySeededWorkflow();
    void seededValidWorkflows_data();
    void seededValidWorkflows();
};

void WorkflowIntegrationTests::semanticSaveLoadAndUndoRedoEquivalence()
{
    EditorController controller;
    const QString id = controller.createTextObject(QPointF(120, 80), QStringLiteral("Alpha"));
    controller.setFontItalic(true);
    controller.setTracking(-0.03);
    controller.addEffect(QStringLiteral("wave"));
    controller.moveSelectedObjects(QPointF(40, 20));
    const QString after = test::semanticFingerprint(controller.document());
    controller.undoStack()->undo(); controller.undoStack()->undo(); controller.undoStack()->undo(); controller.undoStack()->undo();
    const QString before = test::semanticFingerprint(controller.document());
    QVERIFY(before != after);
    controller.undoStack()->redo(); controller.undoStack()->redo(); controller.undoStack()->redo(); controller.undoStack()->redo();
    QCOMPARE(test::semanticFingerprint(controller.document()), after);
    Document loaded;
    QString error;
    QVERIFY2(ProjectSerializer::fromJson(ProjectSerializer::toJson(controller.document()), &loaded, &error), qPrintable(error));
    QVERIFY2(test::semanticallyEqual(controller.document(), loaded, &error), qPrintable(error));
    QVERIFY(controller.document().objectById(id));
}

void WorkflowIntegrationTests::latestAsyncTextGenerationWins()
{
    EditorController controller;
    const QString id = controller.createTextObject(QPointF(30, 30), QStringLiteral("first"));
    controller.setText(QStringLiteral("second"));
    controller.setText(QStringLiteral("third"));
    QTRY_COMPARE_WITH_TIMEOUT(controller.document().objectById(id)->sourceText, QStringLiteral("third"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.sceneGeometry().objectById(id) != nullptr, 5000);
    QCOMPARE(controller.sceneGeometry().objectById(id)->sourceText, QStringLiteral("third"));
}

void WorkflowIntegrationTests::latestAsyncSemanticSnapshotWins()
{
    EditorController controller;
    const QString id = controller.createTextObject(QPointF(30, 30), QStringLiteral("initial"));
    controller.addEffect(QStringLiteral("wave"));
    DeformationStroke stroke;
    stroke.mode = BrushMode::Push;
    stroke.radius = 80.0;
    stroke.strength = 0.8;
    stroke.samples = {{QPointF(20.0, 0.0), QPointF(0.0, 14.0), 1.0}};
    controller.addDeformationStroke(stroke);
    QTRY_VERIFY_WITH_TIMEOUT(controller.sceneGeometry().objectById(id) != nullptr, 5000);

    test::AsyncEvaluationGate gate;
    QVERIFY2(gate.waitUntilHolding(), "Could not acquire deterministic async evaluation barrier");

    ObjectTransform transformA = controller.document().objectById(id)->transform;
    transformA.position = QPointF(90.0, 50.0);
    transformA.rotation = -17.0;
    transformA.scale = QPointF(0.75, 1.1);
    transformA.pivotLocal = QPointF(10.0, 12.0);
    transformA.hasPivot = true;
    controller.setText(QStringLiteral("semantic A"));
    controller.setEffectParameter(0, QStringLiteral("amplitude"), 0.15);
    controller.setEffectStackStrength(0.35);
    controller.setDeformationStrength(0.4);
    controller.setObjectTransform(id, transformA);

    ObjectTransform transformB = transformA;
    transformB.position = QPointF(271.0, 183.0);
    transformB.rotation = 43.0;
    transformB.scale = QPointF(-1.2, 0.63);
    transformB.pivotLocal = QPointF(31.0, -9.0);
    controller.setText(QString::fromUtf8("семантика B"));
    controller.setEffectParameter(0, QStringLiteral("amplitude"), 0.82);
    controller.setEffectStackStrength(1.7);
    controller.setDeformationStrength(1.35);
    controller.setObjectTransform(id, transformB);

    const SceneGeometry expectedScene = SceneEvaluator::evaluate(
        *controller.document().currentPage(), controller.spatialRevision());
    const SceneObjectGeometry* expectedObject = expectedScene.objectById(id);
    QVERIFY(expectedObject);
    const test::SceneObjectSignature expected = test::sceneObjectSignature(*expectedObject);

    gate.release();
    QTRY_VERIFY_WITH_TIMEOUT(controller.sceneGeometry().objectById(id) != nullptr, 8000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.sceneGeometry().objectById(id)->sourceText,
                              QString::fromUtf8("семантика B"), 8000);
    QString difference;
    QVERIFY2(test::compareSceneObject(expected,
                                      test::sceneObjectSignature(*controller.sceneGeometry().objectById(id)),
                                      &difference),
             qPrintable(QStringLiteral("Latest source text won but its semantic scene did not:\n%1")
                            .arg(difference)));
    const auto report = test::checkInvariants(controller.document(), &controller.sceneGeometry(),
                                               controller.selectedObjectIds(),
                                               controller.selectionModel()->activeObjectId());
    QVERIFY2(report.ok(), qPrintable(report.summary()));
}

void WorkflowIntegrationTests::mixedRapidMutationsPublishOnlyFinalSemanticScene()
{
    EditorController controller;
    const QString pageA = controller.document().currentPageId;
    const QString objectA = controller.createTextObject(
        QPointF(30.0, 40.0), QStringLiteral("page A initial"));
    QTRY_VERIFY_WITH_TIMEOUT(controller.sceneGeometry().objectById(objectA) != nullptr, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.sceneGeometry().spatialRevision,
                              controller.spatialRevision(), 5000);

    test::AsyncEvaluationGate gate;
    QVERIFY2(gate.waitUntilHolding(), "Could not hold the deterministic latest-wins queue");

    controller.setText(QStringLiteral("page A superseded text"));
    controller.setFontSize(118.0);
    controller.addEffect(QStringLiteral("wave"));
    controller.setEffectParameter(0, QStringLiteral("amplitude"), 0.72);
    ObjectTransform transformA = controller.document().objectById(objectA)->transform;
    transformA.position = QPointF(250.0, 160.0);
    transformA.rotation = -28.0;
    transformA.scale = QPointF(-0.8, 1.25);
    controller.setObjectTransform(objectA, transformA);
    DeformationStroke deformationA;
    deformationA.coordinateSpace = DeformationCoordinateSpace::ObjectLocal;
    deformationA.mode = BrushMode::Pull;
    deformationA.target = BrushTarget::Shape;
    deformationA.radius = 80.0;
    deformationA.samples = {{QPointF(40.0, 20.0), QPointF(3.0, -5.0), 1.0}};
    controller.addDeformationStroke(objectA, deformationA);

    controller.addPage();
    const QString pageB = controller.document().currentPageId;
    QVERIFY(pageB != pageA);
    const QString finalObject = controller.createTextObject(
        QPointF(140.0, 120.0), QStringLiteral("page B intermediate"));
    controller.setText(QStringLiteral("page B FINAL \U0001F600"));
    controller.setFontSize(91.0);
    controller.setTracking(-0.045);
    controller.addEffect(QStringLiteral("wave"));
    controller.setEffectParameter(0, QStringLiteral("amplitude"), 0.63);
    controller.setEffectStackStrength(1.42);
    const QString finalEffectId =
        controller.document().objectById(finalObject)->effects.at(0)->instanceId;
    EffectMaskStroke finalMask;
    finalMask.points = {QPointF(160.0, 135.0), QPointF(260.0, 155.0)};
    finalMask.radius = 36.0;
    finalMask.opacity = 0.68;
    finalMask.hardness = 0.42;
    controller.addEffectMaskStroke(finalObject, finalEffectId, finalMask,
                                   controller.spatialRevision());
    ObjectTransform transformB = controller.document().objectById(finalObject)->transform;
    transformB.position = QPointF(390.0, 245.0);
    transformB.rotation = 37.0;
    transformB.scale = QPointF(1.18, 0.74);
    transformB.pivotLocal = QPointF(20.0, 12.0);
    transformB.hasPivot = true;
    controller.setObjectTransform(finalObject, transformB);
    DeformationStroke finalDeformation;
    finalDeformation.coordinateSpace = DeformationCoordinateSpace::ObjectLocal;
    finalDeformation.mode = BrushMode::Push;
    finalDeformation.target = BrushTarget::Shape;
    finalDeformation.radius = 65.0;
    finalDeformation.samples = {
        {QPointF(30.0, 12.0), QPointF(), 1.0},
        {QPointF(48.0, 18.0), QPointF(9.0, 6.0), 0.9},
    };
    controller.addDeformationStroke(finalObject, finalDeformation);

    const QString deletedObject = controller.createTextObject(
        QPointF(700.0, 500.0), QStringLiteral("must never publish"));
    controller.addEffect(QStringLiteral("echo"));
    controller.deleteObject(deletedObject);
    QVERIFY(!controller.document().objectById(deletedObject));

    controller.switchPage(pageA);
    controller.selectObject(objectA);
    controller.setText(QStringLiteral("page A final but non-current"));
    controller.switchPage(pageB);
    controller.selectObject(finalObject);
    controller.setLineSpacing(1.31);
    controller.setFontItalic(true);

    const quint64 finalRevision = controller.spatialRevision();
    const SceneGeometry expectedScene = SceneEvaluator::evaluate(
        *controller.document().currentPage(), finalRevision);
    QCOMPARE(expectedScene.evaluationStatus, EvaluationStatus::Complete);
    const SceneObjectGeometry* expectedObject = expectedScene.objectById(finalObject);
    QVERIFY(expectedObject);
    const test::SceneObjectSignature expected = test::sceneObjectSignature(*expectedObject);

    gate.release();
    QTRY_COMPARE_WITH_TIMEOUT(controller.sceneGeometry().spatialRevision,
                              finalRevision, 10000);
    QCOMPARE(controller.sceneGeometry().evaluationStatus, EvaluationStatus::Complete);
    QVERIFY(!controller.sceneGeometry().containsTransientPreview);
    QCOMPARE(controller.sceneGeometry().pageId, pageB);
    QCOMPARE(controller.sceneGeometry().objects.size(), 1);
    QVERIFY(!controller.sceneGeometry().objectById(objectA));
    QVERIFY(!controller.sceneGeometry().objectById(deletedObject));
    const SceneObjectGeometry* published = controller.sceneGeometry().objectById(finalObject);
    QVERIFY(published);
    QString difference;
    QVERIFY2(test::compareSceneObject(expected, test::sceneObjectSignature(*published),
                                      &difference),
             qPrintable(QStringLiteral("mixed latest-wins mismatch: %1").arg(difference)));
    const auto report = test::checkInvariants(
        controller.document(), &controller.sceneGeometry(), controller.selectedObjectIds(),
        controller.selectionModel()->activeObjectId());
    QVERIFY2(report.ok(), qPrintable(report.summary()));
}

void WorkflowIntegrationTests::staleFrameCannotAuthorizeSpatialMutation()
{
    EditorController controller;
    const QString id = controller.createTextObject(
        QPointF(80.0, 60.0), QStringLiteral("revision mapped"));
    controller.addEffect(QStringLiteral("wave"));
    QTRY_VERIFY_WITH_TIMEOUT(controller.sceneGeometry().objectById(id) != nullptr, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.sceneGeometry().spatialRevision,
                              controller.spatialRevision(), 5000);
    const SceneObjectGeometry* publishedN = controller.sceneGeometry().objectById(id);
    QVERIFY(publishedN);
    const ObjectFrame frameN = publishedN->frame;
    const quint64 revisionN = controller.sceneGeometry().spatialRevision;
    QCOMPARE(frameN.spatialRevision, revisionN);
    QCOMPARE(revisionN, controller.spatialRevision());

    test::AsyncEvaluationGate gate;
    QVERIFY2(gate.waitUntilHolding(), "Could not hold publication at revision N");

    ObjectTransform transform = controller.document().objectById(id)->transform;
    transform.position = QPointF(310.0, 175.0);
    transform.rotation = 31.0;
    transform.scale = QPointF(-0.72, 1.45);
    transform.pivotLocal = frameN.pivotLocal;
    transform.hasPivot = true;
    controller.setObjectTransform(id, transform);
    QVERIFY(controller.spatialRevision() > revisionN);
    QCOMPARE(controller.sceneGeometry().spatialRevision, revisionN);

    const QString effectId = controller.document().objectById(id)->effects.at(0)->instanceId;
    const QPointF intendedMaskPoint(424.0, 289.0);
    EffectMaskStroke pageMask;
    pageMask.points = {intendedMaskPoint};
    pageMask.radius = 22.0;
    pageMask.opacity = 0.7;
    pageMask.hardness = 0.3;
    controller.addEffectMaskStroke(id, effectId, pageMask, revisionN);
    const EffectMaskStroke& storedMask =
        controller.document().objectById(id)->effects.at(0)->maskStrokes.back();
    const ObjectFrame maskFrame = SceneEvaluator::evaluateObjectFrame(
        *controller.document().objectById(id), controller.spatialRevision());
    QVERIFY(QLineF(maskFrame.localPointToPage(storedMask.points.front()),
                   intendedMaskPoint).length() < 1.0e-6);
    QVERIFY2(QLineF(frameN.localPointToPage(storedMask.points.front()),
                    intendedMaskPoint).length() > 10.0,
             "obsolete revision N frame authorized the stored mask mutation");

    const QPointF intendedPageStart(445.0, 318.0);
    const QPointF intendedPageEnd(471.0, 337.0);
    DeformationStroke pageInput;
    pageInput.coordinateSpace = DeformationCoordinateSpace::PageInput;
    pageInput.mode = BrushMode::Push;
    pageInput.target = BrushTarget::Shape;
    pageInput.radius = 28.0;
    pageInput.samples = {
        {intendedPageStart, QPointF(), 1.0},
        {intendedPageEnd, intendedPageEnd - intendedPageStart, 1.0},
    };
    controller.addDeformationStroke(id, pageInput, revisionN);

    const TextObject* storedObject = controller.document().objectById(id);
    QVERIFY(storedObject);
    QVERIFY(!storedObject->deformation.strokes.isEmpty());
    const DeformationStroke& stored = storedObject->deformation.strokes.back();
    QCOMPARE(stored.coordinateSpace, DeformationCoordinateSpace::ObjectLocal);
    const ObjectFrame currentFrame = SceneEvaluator::evaluateObjectFrame(
        *storedObject, controller.spatialRevision());
    QVERIFY(QLineF(currentFrame.localPointToPage(stored.samples.at(0).position),
                   intendedPageStart).length() < 1.0e-6);
    QVERIFY(QLineF(currentFrame.localPointToPage(stored.samples.at(1).position),
                   intendedPageEnd).length() < 1.0e-6);
    QVERIFY2(QLineF(frameN.localPointToPage(stored.samples.at(0).position),
                    intendedPageStart).length() > 10.0,
             "obsolete revision N frame authorized the stored local mutation");

    const ObjectTransform beforeRejectedGesture = storedObject->transform;
    const QString beforeRejectedFingerprint = test::semanticFingerprint(controller.document());
    const int beforeRejectedCount = controller.undoStack()->count();
    const int beforeRejectedIndex = controller.undoStack()->index();
    ObjectTransform staleTransform = beforeRejectedGesture;
    staleTransform.rotation += 45.0;
    controller.setObjectTransform(id, staleTransform, revisionN);
    QCOMPARE(controller.document().objectById(id)->transform.toJson(),
             beforeRejectedGesture.toJson());
    QCOMPARE(test::semanticFingerprint(controller.document()), beforeRejectedFingerprint);
    QCOMPARE(controller.undoStack()->count(), beforeRejectedCount);
    QCOMPARE(controller.undoStack()->index(), beforeRejectedIndex);

    gate.release();
    QTRY_COMPARE_WITH_TIMEOUT(controller.sceneGeometry().spatialRevision,
                              controller.spatialRevision(), 8000);
    QCOMPARE(controller.sceneGeometry().objectById(id)->frame.spatialRevision,
             controller.spatialRevision());
}

void WorkflowIntegrationTests::transientPreviewNeverBecomesDocumentOrFrameAuthority()
{
    EditorController controller;
    const QString id = controller.createTextObject(
        QPointF(90.0, 70.0), QStringLiteral("preview is not authority"));
    controller.addEffect(QStringLiteral("wave"));
    QTRY_VERIFY_WITH_TIMEOUT(controller.sceneGeometry().objectById(id) != nullptr, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.sceneGeometry().spatialRevision,
                              controller.spatialRevision(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.sceneGeometry().containsTransientPreview, 5000);

    const QString persistedBefore = test::semanticFingerprint(controller.document());
    const test::GeometrySignature committedGeometry = test::geometrySignature(
        controller.sceneGeometry().objectById(id)->geometry);
    const ObjectFrame committedFrame = controller.sceneGeometry().objectById(id)->frame;

    DeformationStroke preview;
    preview.coordinateSpace = DeformationCoordinateSpace::ObjectLocal;
    preview.mode = BrushMode::Push;
    preview.target = BrushTarget::Shape;
    preview.radius = qMax<qreal>(80.0, committedFrame.currentLocalBounds.width());
    const QPointF localCenter = committedFrame.currentLocalBounds.center();
    preview.samples = {
        {localCenter - QPointF(8.0, 0.0), QPointF(), 1.0},
        {localCenter + QPointF(8.0, 0.0), QPointF(24.0, 13.0), 1.0},
    };
    controller.setDeformationPreview(id, preview, controller.spatialRevision());
    QTRY_VERIFY_WITH_TIMEOUT(controller.sceneGeometry().containsTransientPreview, 8000);
    const SceneObjectGeometry* previewObject = controller.sceneGeometry().objectById(id);
    QVERIFY(previewObject);
    QString geometryDifference;
    QVERIFY2(!test::compareGeometry(
                 committedGeometry, test::geometrySignature(previewObject->geometry),
                 &geometryDifference),
             "the preview fixture did not actually alter evaluated geometry");

    QCOMPARE(test::semanticFingerprint(controller.document()), persistedBefore);
    const QByteArray serialized = ProjectSerializer::toJson(controller.document())
                                      .toJson(QJsonDocument::Compact);
    QVERIFY(!serialized.contains("pageInput"));

    // A semantic command must discard the preview before publishing its newer
    // revision. This also proves the preview cannot leak into worker caches or
    // keep future frames marked transient.
    controller.setTracking(0.03125);
    QVERIFY(test::semanticFingerprint(controller.document()) != persistedBefore);
    QTRY_COMPARE_WITH_TIMEOUT(controller.sceneGeometry().spatialRevision,
                              controller.spatialRevision(), 8000);
    QVERIFY(!controller.sceneGeometry().containsTransientPreview);
    QCOMPARE(controller.document().objectById(id)->deformation.strokes.size(), 0);

    const SceneGeometry synchronous = SceneEvaluator::evaluate(
        *controller.document().currentPage(), controller.spatialRevision());
    const SceneObjectGeometry* expected = synchronous.objectById(id);
    const SceneObjectGeometry* actual = controller.sceneGeometry().objectById(id);
    QVERIFY(expected && actual);
    QString difference;
    QVERIFY2(test::compareSceneObject(test::sceneObjectSignature(*expected),
                                      test::sceneObjectSignature(*actual), &difference),
             qPrintable(difference));
}

void WorkflowIntegrationTests::cooperativeWorkBudgetAndCancellationAreDeterministic()
{
    Document document;
    TextObject& object = document.primaryTextObject();
    object.sourceText = QString(200, QLatin1Char('W'));
    object.font.family = QFontDatabase::families().value(0);
    object.font.styleName = QFontDatabase::styles(object.font.family).value(0);
    object.typography.fontSize = 72.0;

    std::unique_ptr<Effect> wave = EffectRegistry::instance().create(QStringLiteral("wave"));
    QVERIFY(wave);
    EffectMaskStroke mask;
    mask.radius = 60.0;
    for (int index = 0; index < 64; ++index) {
        mask.points.push_back(QPointF(index * 5.0, std::sin(index * 0.2) * 20.0));
    }
    wave->maskStrokes = {mask};
    object.effects.append(std::move(wave));

    DeformationStroke deformation;
    deformation.coordinateSpace = DeformationCoordinateSpace::ObjectLocal;
    deformation.mode = BrushMode::Push;
    deformation.target = BrushTarget::Shape;
    deformation.radius = 80.0;
    for (int index = 0; index < 64; ++index) {
        deformation.samples.push_back(
            {QPointF(index * 4.0, 0.0), QPointF(0.0, 2.0), 1.0});
    }
    object.deformation.strokes = {deformation};

    QSemaphore reachedCheckpoint;
    QSemaphore continueWork;
    std::atomic_bool paused = false;
    const WorkControl work = WorkControl::withBudget(1'000'000);
    constexpr qint64 cancellationUnit = 13'000;
    work.setCheckpointCallback([&](qint64 consumed) {
        if (consumed >= cancellationUnit && !paused.exchange(true)) {
            reachedCheckpoint.release();
            continueWork.acquire();
        }
    });
    const Page snapshot = *document.currentPage();
    QFuture<SceneGeometry> future = QtConcurrent::run([snapshot, work] {
        return SceneEvaluator::evaluate(snapshot, 73, work);
    });
    if (!reachedCheckpoint.tryAcquire(1, 5000)) {
        work.cancel();
        continueWork.release();
        future.waitForFinished();
        QFAIL("Evaluation did not reach the deterministic work-unit checkpoint");
    }
    work.cancel();
    continueWork.release();
    const SceneGeometry cancelled = future.result();
    QCOMPARE(cancelled.evaluationStatus, EvaluationStatus::Cancelled);
    QVERIFY(cancelled.objects.isEmpty());
    QVERIFY(work.unitsConsumed() >= cancellationUnit);
    QVERIFY(work.unitsConsumed() < cancellationUnit + 12'000);

    const WorkControl bounded = WorkControl::withBudget(3);
    const SceneGeometry exceeded = SceneEvaluator::evaluate(snapshot, 74, bounded);
    QCOMPARE(exceeded.evaluationStatus, EvaluationStatus::BudgetExceeded);
    QVERIFY(exceeded.objects.isEmpty());
    QCOMPARE(bounded.unitsConsumed(), qint64(3));
}

void WorkflowIntegrationTests::cancelledSvgNeverCommitsPartialOutput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("atomic-cancel.svg"));
    const QByteArray sentinel("previous-good-output");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(sentinel), qint64(sentinel.size()));
    }

    VectorExportPayload payload;
    payload.bounds = QRectF(0.0, 0.0, 100.0, 40.0);
    VectorExportRecord record;
    record.path.addRect(payload.bounds);
    record.fill = Qt::black;
    payload.records = {record};

    const WorkControl work = WorkControl::withBudget(1000);
    work.setCheckpointCallback([work](qint64 consumed) {
        if (consumed == 1) work.cancel();
    });
    SvgExporter exporter;
    QString error;
    QVERIFY(!exporter.exportPayload(payload, path, &error, work));
    QCOMPARE(work.status(), WorkControlStatus::Cancelled);
    QVERIFY(error.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
    QFile unchanged(path);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), sentinel);
}

void WorkflowIntegrationTests::pageSwitchRejectsLatePreviousPage()
{
    EditorController controller;
    const QString pageA = controller.document().currentPageId;
    const QString objectA = controller.createTextObject(QPointF(20, 20), QStringLiteral("Page A"));
    QTRY_VERIFY_WITH_TIMEOUT(controller.sceneGeometry().objectById(objectA) != nullptr, 5000);
    controller.addPage();
    const QString pageB = controller.document().currentPageId;
    QVERIFY(pageA != pageB);
    const QString objectB = controller.createTextObject(QPointF(320, 180), QStringLiteral("Page B authoritative"));
    QTRY_VERIFY_WITH_TIMEOUT(controller.sceneGeometry().objectById(objectB) != nullptr, 5000);
    controller.switchPage(pageA);
    QTRY_COMPARE_WITH_TIMEOUT(controller.sceneGeometry().pageId, pageA, 5000);

    test::AsyncEvaluationGate gate;
    QVERIFY(gate.waitUntilHolding());
    controller.setText(QStringLiteral("late Page A result"));
    controller.switchPage(pageB);
    const SceneGeometry expected = SceneEvaluator::evaluate(
        *controller.document().currentPage(), controller.spatialRevision());
    gate.release();

    QTRY_COMPARE_WITH_TIMEOUT(controller.sceneGeometry().pageId, pageB, 8000);
    QVERIFY(controller.sceneGeometry().objectById(objectA) == nullptr);
    const SceneObjectGeometry* publishedB = controller.sceneGeometry().objectById(objectB);
    QVERIFY(publishedB);
    QString difference;
    QVERIFY2(test::compareSceneObject(
                 test::sceneObjectSignature(*expected.objectById(objectB)),
                 test::sceneObjectSignature(*publishedB), &difference), qPrintable(difference));
}

void WorkflowIntegrationTests::deletedObjectCannotBeRepublished()
{
    EditorController controller;
    const QString id = controller.createTextObject(QPointF(40, 40), QStringLiteral("delete me"));
    QTRY_VERIFY_WITH_TIMEOUT(controller.sceneGeometry().objectById(id) != nullptr, 5000);
    test::AsyncEvaluationGate gate;
    QVERIFY(gate.waitUntilHolding());
    controller.setText(QStringLiteral("stale worker snapshot"));
    controller.deleteObject(id);
    QVERIFY(controller.document().objectById(id) == nullptr);
    gate.release();
    QTRY_VERIFY_WITH_TIMEOUT(controller.sceneGeometry().objectById(id) == nullptr, 8000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.sceneGeometry().pageId,
                              controller.document().currentPageId, 8000);
}

void WorkflowIntegrationTests::controllerDestructionWithQueuedEvaluationIsSafe()
{
    auto controller = std::make_unique<EditorController>();
    const QString id = controller->createTextObject(QPointF(60, 60), QStringLiteral("lifetime"));
    QTRY_VERIFY_WITH_TIMEOUT(controller->sceneGeometry().objectById(id) != nullptr, 5000);
    test::AsyncEvaluationGate gate;
    QVERIFY(gate.waitUntilHolding());
    controller->setText(QStringLiteral("queued during destruction"));
    controller.reset();
    gate.release();
    QVERIFY2(QThreadPool::globalInstance()->waitForDone(8000),
             "Queued evaluation did not finish after controller destruction");
}

void WorkflowIntegrationTests::duplicatePageFreshensEntireIdentityHierarchy()
{
    EditorController controller;
    const QString originalObjectId = controller.createTextObject(QPointF(50, 60), QStringLiteral("duplicate semantics"));
    controller.setPathLayoutEnabled(true);
    controller.addEffect(QStringLiteral("wave"));
    controller.addEffect(QStringLiteral("echo"));
    controller.setEffectStackStrength(1.6);
    const Page original(*controller.document().currentPage());
    const QString originalPageId = original.id;
    QSet<QString> originalIds;
    originalIds.insert(original.id);
    for (const auto& layer : original.layers) {
        originalIds.insert(layer->id);
        for (const auto& object : layer->objects) {
            originalIds.insert(object->id);
            if (object->path.has_value()) {
                originalIds.insert(object->path->id);
                for (const PathNode& node : object->path->nodes) {
                    originalIds.insert(node.id);
                }
            }
            for (int index = 0; index < object->effects.size(); ++index) {
                originalIds.insert(object->effects.at(index)->instanceId);
            }
        }
    }

    controller.duplicateCurrentPage();
    const Page* duplicated = controller.document().currentPage();
    QVERIFY(duplicated);
    QVERIFY(duplicated->id != originalPageId);
    QStringList duplicatedIds;
    duplicatedIds << duplicated->id;
    for (const auto& layer : duplicated->layers) {
        duplicatedIds << layer->id;
        for (const auto& object : layer->objects) {
            duplicatedIds << object->id;
            if (object->path.has_value()) {
                duplicatedIds << object->path->id;
                for (const PathNode& node : object->path->nodes) {
                    duplicatedIds << node.id;
                }
            }
            for (int index = 0; index < object->effects.size(); ++index) {
                duplicatedIds << object->effects.at(index)->instanceId;
            }
        }
    }
    QSet<QString> uniqueDuplicatedIds;
    for (const QString& id : duplicatedIds) uniqueDuplicatedIds.insert(id);
    QCOMPARE(uniqueDuplicatedIds.size(), duplicatedIds.size());
    for (const QString& id : duplicatedIds) QVERIFY2(!originalIds.contains(id), qPrintable(id));
    QVERIFY2(test::checkInvariants(controller.document()).ok(),
             qPrintable(test::checkInvariants(controller.document()).summary()));

    Page normalized(*duplicated);
    normalized.id = original.id;
    normalized.name = original.name;
    for (size_t layerIndex = 0; layerIndex < normalized.layers.size(); ++layerIndex) {
        Layer& normalizedLayer = *normalized.layers[layerIndex];
        const Layer& originalLayer = *original.layers[layerIndex];
        normalizedLayer.id = originalLayer.id;
        for (size_t objectIndex = 0; objectIndex < normalizedLayer.objects.size(); ++objectIndex) {
            TextObject& normalizedObject = *normalizedLayer.objects[objectIndex];
            const TextObject& originalObject = *originalLayer.objects[objectIndex];
            normalizedObject.id = originalObject.id;
            if (normalizedObject.path.has_value() && originalObject.path.has_value()) {
                normalizedObject.path->id = originalObject.path->id;
                for (int nodeIndex = 0;
                     nodeIndex < normalizedObject.path->nodes.size(); ++nodeIndex) {
                    normalizedObject.path->nodes[nodeIndex].id =
                        originalObject.path->nodes.at(nodeIndex).id;
                }
                normalizedObject.pathLayout.pathId = normalizedObject.path->id;
            }
            for (int effectIndex = 0; effectIndex < normalizedObject.effects.size(); ++effectIndex) {
                normalizedObject.effects.at(effectIndex)->instanceId =
                    originalObject.effects.at(effectIndex)->instanceId;
            }
        }
    }
    QString difference;
    QVERIFY2(test::comparePages(original, normalized, &difference), qPrintable(difference));

    const QString duplicatePageId = duplicated->id;
    controller.undoStack()->undo();
    QVERIFY(controller.document().pageById(duplicatePageId) == nullptr);
    QVERIFY(controller.document().objectById(originalObjectId));
    controller.undoStack()->redo();
    QCOMPARE(controller.document().currentPageId, duplicatePageId);
    QVERIFY2(test::checkInvariants(controller.document()).ok(),
             qPrintable(test::checkInvariants(controller.document()).summary()));
}

void WorkflowIntegrationTests::mergeableCommandReturningToStartRestoresClean_data()
{
    QTest::addColumn<QString>("family");
    for (const QString& family : {QStringLiteral("text"), QStringLiteral("fontSize"),
                                  QStringLiteral("tracking"), QStringLiteral("lineSpacing"),
                                  QStringLiteral("effectParameter"), QStringLiteral("effectMaster"),
                                  QStringLiteral("deformationStrength"), QStringLiteral("move")}) {
        QTest::newRow(family.toUtf8().constData()) << family;
    }
}

void WorkflowIntegrationTests::mergeableCommandReturningToStartRestoresClean()
{
    QFETCH(QString, family);
    EditorController controller;
    const QString objectId =
        controller.createTextObject(QPointF(100, 80), QStringLiteral("clean origin"));
    QVERIFY(!objectId.isEmpty());
    QCOMPARE(controller.document().activeObjectId, objectId);
    QVERIFY(controller.document().objectById(objectId));
    controller.addEffect(QStringLiteral("wave"));
    controller.undoStack()->setClean();
    const QString saved = test::semanticFingerprint(controller.document());
    const int savedIndex = controller.undoStack()->index();
    const int savedCount = controller.undoStack()->count();

    if (family == QStringLiteral("text")) {
        controller.setText(QStringLiteral("away"));
        controller.setText(QStringLiteral("clean origin"));
    } else if (family == QStringLiteral("fontSize")) {
        controller.setFontSize(101.0); controller.setFontSize(72.0);
    } else if (family == QStringLiteral("tracking")) {
        controller.setTracking(0.18); controller.setTracking(0.0);
    } else if (family == QStringLiteral("lineSpacing")) {
        controller.setLineSpacing(1.8); controller.setLineSpacing(1.0);
    } else if (family == QStringLiteral("effectParameter")) {
        const double initial = controller.activeObject()->effects.at(0)->parameterDefinitions().at(0).value;
        controller.setEffectParameter(0, QStringLiteral("amplitude"), 0.8);
        controller.setEffectParameter(0, QStringLiteral("amplitude"), initial);
    } else if (family == QStringLiteral("effectMaster")) {
        controller.setEffectMasterStrength(0, 1.8); controller.setEffectMasterStrength(0, 1.0);
    } else if (family == QStringLiteral("deformationStrength")) {
        controller.setDeformationStrength(1.7); controller.setDeformationStrength(1.0);
    } else if (family == QStringLiteral("move")) {
        controller.moveSelectedObjects(QPointF(20, -11));
        controller.moveSelectedObjects(QPointF(-20, 11));
    }

    QCOMPARE(test::semanticFingerprint(controller.document()), saved);
    QVERIFY2(controller.undoStack()->isClean(), qPrintable(family + QStringLiteral(" left the clean index dirty")));
    QVERIFY(!controller.isModified());
    QCOMPARE(controller.undoStack()->index(), savedIndex);
    QCOMPARE(controller.undoStack()->count(), savedCount);

    controller.setFillColor(QColor(90, 80, 70));
    QVERIFY(controller.undoStack()->canUndo());
    controller.undoStack()->undo();
    QCOMPARE(test::semanticFingerprint(controller.document()), saved);
    QVERIFY(controller.undoStack()->isClean());
}

void WorkflowIntegrationTests::realFileLifecyclePreservesComplexSemantics()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString projectPath = temporary.filePath(QStringLiteral("complex.vtype"));
    const QString malformedPath = temporary.filePath(QStringLiteral("malformed.vtype"));
    const QString migrationPath = temporary.filePath(QStringLiteral("migration-v5.vtype"));

    EditorController controller;
    const QString firstPageId = controller.document().currentPageId;
    const QString firstObjectId = controller.createTextObject(
        QPointF(113.0, 79.0), QString::fromUtf8("Привет\nsemantic lifecycle"));
    controller.setTracking(-0.06);
    controller.setLineSpacing(1.35);
    controller.addEffect(QStringLiteral("wave"));
    controller.setEffectParameter(0, QStringLiteral("amplitude"), 0.67);
    controller.setEffectScope(0, {EffectScopeKind::TextRange, 0, 6});
    Effect* effect = controller.activeObject()->effects.at(0);
    QVERIFY(effect);
    EffectMaskStroke mask;
    mask.points = {QPointF(1.0, 2.0), QPointF(30.0, 18.0)};
    mask.radius = 22.0;
    mask.opacity = 0.74;
    controller.addEffectMaskStroke(firstObjectId, effect->instanceId, mask);
    controller.setEffectStackStrength(1.65);
    DeformationStroke deformation;
    deformation.mode = BrushMode::Pull;
    deformation.target = BrushTarget::Shape;
    deformation.radius = 44.0;
    deformation.strength = 0.58;
    deformation.samples = {{QPointF(17.0, 12.0), QPointF(-4.0, 9.0), 0.83}};
    controller.addDeformationStroke(firstObjectId, deformation);
    ObjectTransform transformed = controller.activeObject()->transform;
    transformed.position = QPointF(238.0, 146.0);
    transformed.rotation = 32.0;
    transformed.scale = QPointF(-0.82, 1.24);
    transformed.pivotLocal = QPointF(28.0, 19.0);
    transformed.hasPivot = true;
    controller.setObjectTransform(firstObjectId, transformed);
    controller.addPage();
    const QString secondObjectId = controller.createTextObject(
        QPointF(420.0, 210.0), QStringLiteral("Second page"));
    controller.addEffect(QStringLiteral("echo"));
    QVERIFY(!secondObjectId.isEmpty());

    QString error;
    QVERIFY2(controller.saveProject(projectPath, &error), qPrintable(error));
    QVERIFY(!controller.isModified());
    const QString saved = test::semanticFingerprint(controller.document());

    controller.newDocument();
    QVERIFY(test::semanticFingerprint(controller.document()) != saved);
    QVERIFY2(controller.openProject(projectPath, &error), qPrintable(error));
    QCOMPARE(test::semanticFingerprint(controller.document()), saved);
    QVERIFY(!controller.isModified());
    QVERIFY(controller.document().pageById(firstPageId));
    const TextObject* restoredFirst = controller.document().objectById(firstObjectId);
    QVERIFY(restoredFirst);
    QCOMPARE(restoredFirst->effectStackStrength, 1.65);
    QCOMPARE(restoredFirst->effects.at(0)->scope.kind, EffectScopeKind::TextRange);
    QCOMPARE(restoredFirst->effects.at(0)->maskStrokes.size(), 1);
    QCOMPARE(restoredFirst->deformation.strokes.size(), 1);
    QCOMPARE(restoredFirst->transform.rotation, 32.0);

    QFile malformed(malformedPath);
    QVERIFY(malformed.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(malformed.write("{ not valid json"), qint64(16));
    malformed.close();
    const QString beforeFailedOpen = test::semanticFingerprint(controller.document());
    error.clear();
    QVERIFY(!controller.openProject(malformedPath, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(test::semanticFingerprint(controller.document()), beforeFailedOpen);

    QJsonObject migrationRoot = ProjectSerializer::toJson(controller.document()).object();
    migrationRoot.insert(QStringLiteral("formatVersion"), 5);
    QJsonArray migrationPages = migrationRoot.value(QStringLiteral("pages")).toArray();
    for (int pageIndex = 0; pageIndex < migrationPages.size(); ++pageIndex) {
        QJsonObject page = migrationPages.at(pageIndex).toObject();
        QJsonArray layers = page.value(QStringLiteral("layers")).toArray();
        for (int layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
            QJsonObject layer = layers.at(layerIndex).toObject();
            QJsonArray objects = layer.value(QStringLiteral("objects")).toArray();
            for (int objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
                QJsonObject object = objects.at(objectIndex).toObject();
                object.remove(QStringLiteral("effectStackStrength"));
                objects.replace(objectIndex, object);
            }
            layer.insert(QStringLiteral("objects"), objects);
            layers.replace(layerIndex, layer);
        }
        page.insert(QStringLiteral("layers"), layers);
        migrationPages.replace(pageIndex, page);
    }
    migrationRoot.insert(QStringLiteral("pages"), migrationPages);
    QFile migration(migrationPath);
    QVERIFY(migration.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray migrationBytes = QJsonDocument(migrationRoot).toJson(QJsonDocument::Indented);
    QCOMPARE(migration.write(migrationBytes), qint64(migrationBytes.size()));
    migration.close();
    error.clear();
    QVERIFY2(controller.openProject(migrationPath, &error), qPrintable(error));
    QCOMPARE(controller.document().objectById(firstObjectId)->effectStackStrength, 1.0);
}

void WorkflowIntegrationTests::exportPlainTextPreservesEqualObjectMultiplicity()
{
    Document document;
    Page* page = document.currentPage();
    QVERIFY(page && !page->layers.empty());
    Layer* layer = page->layers.front().get();
    layer->objects.clear();
    auto first = std::make_unique<TextObject>();
    first->id = QStringLiteral("equal-value-object-a");
    first->sourceText = QStringLiteral("Hello");
    first->transform.position = QPointF(20.0, 20.0);
    auto second = std::make_unique<TextObject>();
    second->id = QStringLiteral("equal-value-object-b");
    second->sourceText = QStringLiteral("Hello");
    second->transform.position = QPointF(220.0, 20.0);
    layer->objects.push_back(std::move(first));
    layer->objects.push_back(std::move(second));
    document.activeObjectId = QStringLiteral("equal-value-object-a");

    const SceneGeometry scene = SceneEvaluator::evaluate(*page);
    VectorExportPayload payload;
    QString error;
    QVERIFY2(ExportPayloadBuilder::build(document, *page, scene, ExportScope::CurrentPage,
                                         {}, &payload, &error), qPrintable(error));
    QCOMPARE(payload.plainText, QStringLiteral("Hello\nHello"));
    QVERIFY(!payload.records.isEmpty());
    QStringList objectOrder;
    for (const VectorExportRecord& record : payload.records) {
        if (objectOrder.isEmpty() || objectOrder.back() != record.sourceObjectId) {
            objectOrder << record.sourceObjectId;
        }
        QCOMPARE(record.sourceText, QStringLiteral("Hello"));
    }
    QCOMPARE(objectOrder, QStringList({QStringLiteral("equal-value-object-a"),
                                       QStringLiteral("equal-value-object-b")}));
}

void WorkflowIntegrationTests::unsupportedEffectMaskIsRefusedWithoutMutation_data()
{
    QTest::addColumn<QString>("unsupportedTypeId");
    for (const EffectDescriptor& descriptor : EffectRegistry::instance().descriptors()) {
        if (!descriptor.supportsMask) {
            QTest::newRow(descriptor.typeId.toUtf8().constData()) << descriptor.typeId;
        }
    }
}

void WorkflowIntegrationTests::unsupportedEffectMaskIsRefusedWithoutMutation()
{
    QFETCH(QString, unsupportedTypeId);
    EditorController controller;
    const QString objectId = controller.createTextObject(QPointF(80, 80), QStringLiteral("mask contract"));
    controller.addEffect(unsupportedTypeId);
    Effect* unsupportedEffect = controller.document().objectById(objectId)->effects.at(0);
    QVERIFY(unsupportedEffect);
    QCOMPARE(unsupportedEffect->typeId(), unsupportedTypeId);
    const EffectDescriptor* descriptor = EffectRegistry::instance().descriptor(unsupportedTypeId);
    QVERIFY(descriptor && !descriptor->supportsMask);
    EffectMaskStroke stroke;
    stroke.points = {QPointF(90, 90), QPointF(120, 100)};
    stroke.radius = 18.0;
    controller.addEffectMaskStroke(objectId, unsupportedEffect->instanceId, stroke);
    QVERIFY(unsupportedEffect->maskStrokes.isEmpty());
    const QJsonArray serializedEffects = ProjectSerializer::textObjectToJson(
        *controller.document().objectById(objectId)).value(QStringLiteral("effects")).toArray();
    QCOMPARE(serializedEffects.size(), 1);
    QVERIFY(serializedEffects.at(0).toObject().value(QStringLiteral("mask")).toArray().isEmpty());

    controller.addEffect(QStringLiteral("wave"));
    Effect* wave = controller.document().objectById(objectId)->effects.at(1);
    QVERIFY(wave);
    const EffectDescriptor* waveDescriptor = EffectRegistry::instance().descriptor(wave->typeId());
    QVERIFY(waveDescriptor && waveDescriptor->supportsMask);
    controller.addEffectMaskStroke(objectId, wave->instanceId, stroke);
    QCOMPARE(wave->maskStrokes.size(), 1);
}

void WorkflowIntegrationTests::duplicatePasteAndPresetFreshenEffectIdentities()
{
    EditorController controller;
    const QString originalId = controller.createTextObject(QPointF(100, 100), QStringLiteral("clone matrix"));
    controller.setPathLayoutEnabled(true);
    controller.addEffect(QStringLiteral("wave"));
    controller.addEffect(QStringLiteral("echo"));
    const TextObject originalSnapshot(*controller.document().objectById(originalId));
    QVERIFY(originalSnapshot.path.has_value());

    controller.duplicateSelectedObjects();
    const auto afterDuplicate = controller.document().objectsOnCurrentPage();
    QCOMPARE(afterDuplicate.size(), 2);
    TextObject* duplicate = afterDuplicate.at(0)->id == originalId
        ? afterDuplicate.at(1) : afterDuplicate.at(0);
    QVERIFY(duplicate->id != originalId);
    QVERIFY(duplicate->path.has_value());
    QVERIFY(duplicate->path->id != originalSnapshot.path->id);
    QCOMPARE(duplicate->pathLayout.pathId, duplicate->path->id);
    QVERIFY(duplicate->path->nodes.front().id != originalSnapshot.path->nodes.front().id);
    for (int index = 0; index < duplicate->effects.size(); ++index) {
        QVERIFY(duplicate->effects.at(index)->instanceId
                != originalSnapshot.effects.at(index)->instanceId);
        QCOMPARE(duplicate->effects.at(index)->parametersToJson(),
                 originalSnapshot.effects.at(index)->parametersToJson());
    }
    const QString duplicateId = duplicate->id;
    controller.undoStack()->undo();
    QVERIFY(controller.document().objectById(duplicateId) == nullptr);
    controller.undoStack()->redo();
    QVERIFY(controller.document().objectById(duplicateId));
    QTRY_VERIFY_WITH_TIMEOUT(controller.sceneGeometry().objectById(originalId)
                                 && controller.sceneGeometry().objectById(duplicateId), 5000);

    controller.selectObject(originalId);
    QCOMPARE(controller.selectionModel()->activeObjectId(), originalId);
    controller.copySelectedObjects();
    controller.pasteObjects();
    const auto afterPaste = controller.document().objectsOnCurrentPage();
    QCOMPARE(afterPaste.size(), 3);
    QSet<QString> objectIds;
    QSet<QString> effectIds;
    QSet<QString> pathIds;
    QSet<QString> pathNodeIds;
    for (const TextObject* object : afterPaste) {
        objectIds.insert(object->id);
        QVERIFY(object->path.has_value());
        pathIds.insert(object->path->id);
        for (const PathNode& node : object->path->nodes) {
            pathNodeIds.insert(node.id);
        }
        for (int index = 0; index < object->effects.size(); ++index) {
            effectIds.insert(object->effects.at(index)->instanceId);
        }
    }
    QCOMPARE(objectIds.size(), 3);
    QCOMPARE(effectIds.size(), 6);
    QCOMPARE(pathIds.size(), 3);
    QCOMPARE(pathNodeIds.size(), 3 * originalSnapshot.path->nodes.size());
    QString error;
    controller.selectObject(originalId);
    QVERIFY2(controller.applyPresetById(QStringLiteral("builtin.whisper.v1"), &error), qPrintable(error));
    controller.selectObject(duplicateId);
    QVERIFY2(controller.applyPresetById(QStringLiteral("builtin.whisper.v1"), &error), qPrintable(error));
    QSet<QString> presetIds;
    for (const QString& id : {originalId, duplicateId}) {
        const TextObject* object = controller.document().objectById(id);
        QVERIFY(object);
        QCOMPARE(object->effects.size(), 3);
        for (int index = 0; index < object->effects.size(); ++index) {
            presetIds.insert(object->effects.at(index)->instanceId);
        }
    }
    QCOMPARE(presetIds.size(), 6);
    QVERIFY2(test::checkInvariants(controller.document()).ok(),
             qPrintable(test::checkInvariants(controller.document()).summary()));
}

void WorkflowIntegrationTests::effectReorderDeleteUndoRestoresSemanticOrder()
{
    EditorController controller;
    const QString objectId = controller.createTextObject(QPointF(100, 100), QStringLiteral("order contract"));
    controller.addEffect(QStringLiteral("wave"));
    controller.addEffect(QStringLiteral("bend"));
    controller.addEffect(QStringLiteral("echo"));
    TextObject* object = controller.document().objectById(objectId);
    QVERIFY(object);
    const QJsonArray originalStack = object->effects.toJson();
    const test::GeometrySignature originalGeometry = test::geometrySignature(
        SceneEvaluator::evaluate(*controller.document().currentPage()).objectById(objectId)->geometry);

    controller.moveEffect(0, 2);
    QVERIFY(object->effects.toJson() != originalStack);
    controller.undoStack()->undo();
    QCOMPARE(object->effects.toJson(), originalStack);
    QString difference;
    QVERIFY2(test::compareGeometry(
                 originalGeometry,
                 test::geometrySignature(
                     SceneEvaluator::evaluate(*controller.document().currentPage()).objectById(objectId)->geometry),
                 &difference), qPrintable(difference));

    controller.removeEffect(1);
    QCOMPARE(object->effects.size(), 2);
    controller.undoStack()->undo();
    QCOMPARE(object->effects.toJson(), originalStack);
    controller.undoStack()->redo();
    QCOMPARE(object->effects.size(), 2);
    controller.undoStack()->undo();
    QCOMPARE(object->effects.toJson(), originalStack);
}

void WorkflowIntegrationTests::legacyV1V2V3MigrationSurvivesSaveReloadAndUndoRedo()
{
    Document legacy;
    Layer* layer = legacy.activeLayer();
    QVERIFY(layer);
    TextObject& first = legacy.primaryTextObject();
    first.sourceText = QStringLiteral("legacy first");
    first.effects.append(EffectRegistry::instance().create(QStringLiteral("wave")));
    auto second = std::make_unique<TextObject>();
    second->sourceText = QStringLiteral("legacy second");
    second->effects.append(EffectRegistry::instance().create(QStringLiteral("echo")));
    layer->objects.push_back(std::move(second));

    const QJsonObject serializedCurrent = ProjectSerializer::toJson(legacy).object();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (int version : {1, 2, 3}) {
        QJsonObject root = serializedCurrent;
        root.insert(QStringLiteral("formatVersion"), version);
        root.remove(QStringLiteral("pages"));
        root.remove(QStringLiteral("currentPageId"));
        root.remove(QStringLiteral("activeLayerId"));
        root.remove(QStringLiteral("activeObjectId"));

        const QString legacyPath = directory.filePath(
            QStringLiteral("legacy-v%1.vtype").arg(version));
        QFile file(legacyPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();

        EditorController controller;
        QString error;
        QVERIFY2(controller.openProject(legacyPath, &error), qPrintable(error));
        const auto objects = controller.document().objectsOnCurrentPage();
        QCOMPARE(objects.size(), 2);
        QCOMPARE(objects.at(0)->sourceText, QStringLiteral("legacy first"));
        QCOMPARE(objects.at(1)->sourceText, QStringLiteral("legacy second"));
        QCOMPARE(objects.at(0)->id,
                 QStringLiteral("migrated-v%1-page-0-layer-0-object-0").arg(version));
        QCOMPARE(objects.at(1)->id,
                 QStringLiteral("migrated-v%1-page-0-layer-0-object-1").arg(version));
        QCOMPARE(objects.at(0)->effects.at(0)->instanceId,
                 QStringLiteral("migrated-v%1-page-0-layer-0-object-0-effect-0")
                     .arg(version));
        QCOMPARE(objects.at(1)->effects.at(0)->instanceId,
                 QStringLiteral("migrated-v%1-page-0-layer-0-object-1-effect-0")
                     .arg(version));
        QVERIFY2(test::checkInvariants(controller.document()).ok(),
                 qPrintable(test::checkInvariants(controller.document()).summary()));

        const QString migratedFingerprint = test::semanticFingerprint(controller.document());
        const QString currentPath = directory.filePath(
            QStringLiteral("current-from-v%1.vtype").arg(version));
        error.clear();
        QVERIFY2(controller.saveProject(currentPath, &error), qPrintable(error));
        controller.newDocument();
        error.clear();
        QVERIFY2(controller.openProject(currentPath, &error), qPrintable(error));
        QCOMPARE(test::semanticFingerprint(controller.document()), migratedFingerprint);
        QCOMPARE(controller.document().formatVersion, Document::CurrentFormatVersion);

        controller.selectObject(controller.document().objectsOnCurrentPage().at(1)->id);
        const QString beforeEdit = test::semanticFingerprint(controller.document());
        controller.setText(QStringLiteral("legacy second edited"));
        const QString afterEdit = test::semanticFingerprint(controller.document());
        QVERIFY(afterEdit != beforeEdit);
        controller.undoStack()->undo();
        QCOMPARE(test::semanticFingerprint(controller.document()), beforeEdit);
        controller.undoStack()->redo();
        QCOMPARE(test::semanticFingerprint(controller.document()), afterEdit);
    }
}

void WorkflowIntegrationTests::malformedOrOversizedClipboardPasteIsTransactional()
{
    EditorController controller;
    const QString existingId = controller.createTextObject(
        QPointF(40.0, 40.0), QStringLiteral("clipboard sentinel"));
    QVERIFY(!existingId.isEmpty());
    controller.undoStack()->setClean();
    const QString before = test::semanticFingerprint(controller.document());
    const int beforeCount = controller.undoStack()->count();
    const int beforeIndex = controller.undoStack()->index();

    auto installEditorClipboard = [](QByteArray bytes) {
        auto* mimeData = new QMimeData();
        mimeData->setData(QStringLiteral("application/x-vector-typography-objects"),
                          std::move(bytes));
        QGuiApplication::clipboard()->setMimeData(mimeData);
    };
    auto assertUnchanged = [&controller, &before, beforeCount, beforeIndex] {
        QCOMPARE(test::semanticFingerprint(controller.document()), before);
        QCOMPARE(controller.document().objectsOnCurrentPage().size(), 1);
        QCOMPARE(controller.undoStack()->count(), beforeCount);
        QCOMPARE(controller.undoStack()->index(), beforeIndex);
        QVERIFY(controller.undoStack()->isClean());
    };

    const QJsonObject valid = ProjectSerializer::textObjectToJson(
        *controller.document().objectById(existingId));
    QJsonObject invalidLater = valid;
    invalidLater.insert(QStringLiteral("type"), QStringLiteral("unsupported-later-entry"));
    installEditorClipboard(QJsonDocument(QJsonArray{valid, invalidLater})
                               .toJson(QJsonDocument::Compact));
    controller.pasteObjects();
    assertUnchanged();

    const qint64 oversizedBytes =
        ProjectSerializer::resourceLimits().maximumClipboardInputBytes + 1;
    installEditorClipboard(QByteArray(static_cast<qsizetype>(oversizedBytes), 'x'));
    controller.pasteObjects();
    assertUnchanged();
}

void WorkflowIntegrationTests::pathTypographySeededWorkflow()
{
    EditorController controller;
    const QString objectId = controller.createTextObject(
        QPointF(120.0, 90.0), QStringLiteral("seeded path workflow"));
    QVERIFY(!objectId.isEmpty());
    controller.setPathLayoutEnabled(true);
    QVERIFY(controller.document().objectById(objectId)->path.has_value());

    QRandomGenerator random(20260813u);
    const auto context = [&controller](int step) {
        return QStringLiteral("path step=%1 object=%2")
            .arg(step)
            .arg(controller.document().activeObjectId);
    };

    for (int step = 0; step < 48; ++step) {
        TextObject* object = controller.document().objectById(objectId);
        QVERIFY(object && object->path.has_value());
        switch (random.bounded(6)) {
        case 0:
            controller.setPathLayoutEnabled(!object->pathLayout.enabled);
            break;
        case 1:
            controller.setPathStartOffset(-90.0 + random.generateDouble() * 180.0);
            controller.setPathBaselineOffset(-30.0 + random.generateDouble() * 60.0);
            controller.setPathFlip(step % 2 == 0);
            break;
        case 2:
            controller.reversePath();
            break;
        case 3:
            controller.setPathClosed(!object->path->closed);
            break;
        case 4: {
            PathGeometry candidate = *object->path;
            candidate.nodes.front().anchor += QPointF(
                -5.0 + random.generateDouble() * 10.0,
                -5.0 + random.generateDouble() * 10.0);
            controller.setPathGeometry(objectId, candidate, controller.spatialRevision());
            break;
        }
        case 5:
            controller.removePathLayout();
            controller.setPathLayoutEnabled(true);
            break;
        default:
            Q_UNREACHABLE();
        }

        QCoreApplication::processEvents();
        const auto report = test::checkInvariants(controller.document(), nullptr,
                                                   controller.selectedObjectIds(),
                                                   controller.selectionModel()->activeObjectId());
        QVERIFY2(report.ok(), qPrintable(context(step) + QLatin1Char('\n') + report.summary()));

        if (step % 8 == 7 && controller.undoStack()->canUndo()) {
            const QString beforeUndo = test::semanticFingerprint(controller.document());
            controller.undoStack()->undo();
            const auto undoReport = test::checkInvariants(
                controller.document(), nullptr, controller.selectedObjectIds(),
                controller.selectionModel()->activeObjectId());
            QVERIFY2(undoReport.ok(), qPrintable(context(step) + QLatin1Char('\n')
                                                   + undoReport.summary()));
            controller.undoStack()->redo();
            QVERIFY2(test::semanticFingerprint(controller.document()) == beforeUndo,
                     qPrintable(context(step)
                                + QStringLiteral("\npath undo/redo did not restore the document")));
        }
    }
}

void WorkflowIntegrationTests::seededValidWorkflows_data()
{
    QTest::addColumn<quint32>("seed");
    bool explicitSeed = false;
    const quint32 requestedSeed = qEnvironmentVariableIntValue("VT_WORKFLOW_SEED", &explicitSeed);
    if (explicitSeed) {
        QTest::newRow(QByteArray::number(requestedSeed).constData()) << requestedSeed;
        return;
    }
    for (quint32 seed : {1u, 7u, 42u, 99u, 1337u, 65537u, 314159u,
                         8675309u, 12648430u, 20260811u}) {
        QTest::newRow(QByteArray::number(seed).constData()) << seed;
    }
}

void WorkflowIntegrationTests::seededValidWorkflows()
{
    QFETCH(quint32, seed);
    EditorController controller;
    QRandomGenerator random(seed);
    QStringList history;
    bool explicitSteps = false;
    const int requestedSteps = qEnvironmentVariableIntValue("VT_WORKFLOW_STEPS", &explicitSteps);
    const int steps = explicitSteps ? qBound(1, requestedSteps, 2000) : 200;
    QTemporaryDir exportDirectory;
    QVERIFY(exportDirectory.isValid());
    const QStringList effectTypes = {
        QStringLiteral("wave"), QStringLiteral("glyphJitter"), QStringLiteral("stretch"),
        QStringLiteral("bend"), QStringLiteral("noiseWarp"), QStringLiteral("melt"),
        QStringLiteral("echo"), QStringLiteral("ghost")};

    auto currentObjects = [&controller] {
        return controller.document().objectsOnCurrentPage();
    };
    auto ensureSelection = [&controller, &currentObjects] {
        if (!controller.selectedObjectIds().isEmpty()) return;
        const auto objects = currentObjects();
        if (!objects.isEmpty()) controller.selectObject(objects.front()->id);
    };
    auto context = [&controller, seed, &history](int step) {
        const TextObject* active = controller.activeObject();
        return QStringLiteral("seed=%1 action=%2 page=%3 layer=%4 active=%5 selection=[%6] effect=%7 tool=%8\nhistory=%9")
            .arg(seed).arg(step).arg(controller.document().currentPageId,
                                    controller.document().activeLayerId,
                                    active ? active->id : QStringLiteral("<none>"),
                                    controller.selectedObjectIds().join(QLatin1Char(',')),
                                    controller.selectedEffectId())
            .arg(static_cast<int>(controller.tool()))
            .arg(history.join(QStringLiteral(" -> ")));
    };

    for (int step = 0; step < steps; ++step) {
        // Keep the long-lived seeded workflow stream stable. Path-specific
        // fuzzing lives in its own deterministic path workflow below so that
        // adding a new action cannot reshuffle unrelated page/layer coverage.
        const int action = random.bounded(31);
        if (action == 0 || currentObjects().isEmpty()) {
            const QString id = controller.createTextObject(QPointF(random.bounded(500), random.bounded(300)),
                                                           QStringLiteral("Seed %1 step %2").arg(seed).arg(step));
            history << QStringLiteral("Create(%1)").arg(id);
        } else {
            ensureSelection();
            TextObject* active = controller.activeObject();
            if (action == 1) {
                controller.setText(step % 3 == 0
                                       ? QString::fromUtf8("Кириллица %1\nстрока").arg(step)
                                       : QStringLiteral("edit %1 seed %2").arg(step).arg(seed));
                history << "Text";
            } else if (action == 2) {
                controller.setFontSize(10.0 + random.bounded(180)); history << "FontSize";
            } else if (action == 3) {
                controller.setTracking(-0.2 + random.generateDouble() * 0.5); history << "Tracking";
            } else if (action == 4) {
                controller.setLineSpacing(0.6 + random.generateDouble() * 1.8); history << "LineSpacing";
            } else if (action == 5) {
                controller.setFontWeight(step % 2 ? QFont::Bold : QFont::Normal);
                controller.setFontItalic(step % 3 == 0);
                controller.setFontUnderline(step % 4 == 0);
                controller.setFontStrikeOut(step % 5 == 0);
                history << "Traits";
            } else if (action == 6) {
                controller.moveSelectedObjects(QPointF(random.bounded(17) - 8, random.bounded(17) - 8));
                history << "Move";
            } else if (action == 7 && active) {
                ObjectTransform transform = active->transform;
                transform.rotation = -170.0 + random.generateDouble() * 340.0;
                transform.scale = QPointF((step % 7 == 0 ? -1.0 : 1.0) * (0.15 + random.generateDouble() * 2.0),
                                          0.15 + random.generateDouble() * 2.0);
                if (!transform.hasPivot) {
                    transform.pivotLocal = QPointF(25.0, 20.0);
                    transform.hasPivot = true;
                }
                controller.setObjectTransform(active->id, transform);
                history << "RotateScale";
            } else if (action == 8 && active && active->effects.size() < 6) {
                const QString type = effectTypes.at(random.bounded(effectTypes.size()));
                controller.addEffect(type); history << QStringLiteral("AddEffect(%1)").arg(type);
            } else if (action == 9 && active && !active->effects.isEmpty()) {
                controller.removeEffect(random.bounded(active->effects.size())); history << "RemoveEffect";
            } else if (action == 10 && active && !active->effects.isEmpty()) {
                const int index = random.bounded(active->effects.size());
                controller.setEffectEnabled(index, !active->effects.at(index)->enabled); history << "ToggleEffect";
            } else if (action == 11 && active && active->effects.size() > 1) {
                controller.moveEffect(0, active->effects.size() - 1); history << "ReorderEffect";
            } else if (action == 12 && active && !active->effects.isEmpty()) {
                const int index = random.bounded(active->effects.size());
                const auto parameters = active->effects.at(index)->parameterDefinitions();
                if (!parameters.isEmpty()) {
                    const EffectParameter parameter = parameters.at(random.bounded(parameters.size()));
                    const double value = parameter.minimum
                        + random.generateDouble() * (parameter.maximum - parameter.minimum);
                    controller.setEffectParameter(index, parameter.id, value);
                    history << QStringLiteral("Parameter(%1)").arg(parameter.id);
                }
            } else if (action == 13 && active && !active->effects.isEmpty()) {
                controller.setEffectMasterStrength(random.bounded(active->effects.size()),
                                                   random.generateDouble() * 2.5);
                controller.setEffectStackStrength(random.generateDouble() * 2.0);
                history << "Strength";
            } else if (action == 14 && active && !active->effects.isEmpty()) {
                const int index = random.bounded(active->effects.size());
                const EffectDescriptor* descriptor = EffectRegistry::instance().descriptor(
                    active->effects.at(index)->typeId());
                if (descriptor && descriptor->supportsTextRange && !active->sourceText.isEmpty()) {
                    const int end = qMax(1, active->sourceText.size() / 2);
                    controller.setEffectScope(index, {EffectScopeKind::TextRange, 0, end});
                    history << "Range";
                }
            } else if (action == 15 && active && !active->effects.isEmpty()) {
                const int index = random.bounded(active->effects.size());
                Effect* selectedEffect = active->effects.at(index);
                const EffectDescriptor* descriptor = EffectRegistry::instance().descriptor(selectedEffect->typeId());
                if (descriptor && descriptor->supportsMask) {
                    EffectMaskStroke mask;
                    mask.points = {QPointF(10.0, 10.0), QPointF(40.0, 25.0)};
                    mask.radius = 25.0;
                    controller.addEffectMaskStroke(active->id, selectedEffect->instanceId, mask);
                    history << "Mask";
                }
            } else if (action == 16 && active) {
                DeformationStroke deformation;
                deformation.mode = static_cast<BrushMode>(step % 5);
                deformation.target = step % 2 ? BrushTarget::Shape : BrushTarget::Glyphs;
                deformation.radius = 20.0 + random.bounded(80);
                deformation.strength = 0.2 + random.generateDouble() * 1.6;
                deformation.samples = {{QPointF(15.0, 15.0), QPointF(3.0, -2.0), 0.8}};
                controller.addDeformationStroke(active->id, deformation);
                controller.setDeformationStrength(0.2 + random.generateDouble() * 1.6);
                history << "Deform";
            } else if (action == 17 && currentObjects().size() < 12) {
                controller.duplicateSelectedObjects(); history << "Duplicate";
            } else if (action == 18 && currentObjects().size() > 1) {
                controller.deleteSelectedObjects(); history << "Delete";
            } else if (action == 19 && currentObjects().size() < 12) {
                controller.copySelectedObjects(); controller.pasteObjects(); history << "CopyPaste";
            } else if (action == 20) {
                if (controller.document().pages.size() < 3 && step % 2 == 0) controller.addPage();
                else {
                    const auto& pages = controller.document().pages;
                    controller.switchPage(pages.at(random.bounded(int(pages.size())))->id);
                }
                history << "Page";
            } else if (action == 21 && controller.document().pages.size() < 3) {
                controller.duplicateCurrentPage(); history << "DuplicatePage";
            } else if (action == 22) {
                Page* page = controller.document().currentPage();
                if (page && page->layers.size() < 3) controller.addLayer();
                else if (page && !page->layers.empty()) {
                    controller.switchLayer(page->layers.at(random.bounded(int(page->layers.size())))->id);
                }
                history << "Layer";
            } else if (action == 23 && active) {
                Page* page = controller.document().currentPage();
                if (page && page->layers.size() > 1) {
                    const QString objectId = active->id;
                    const QString destination = page->layers.at(random.bounded(int(page->layers.size())))->id;
                    controller.moveObjectToLayer(objectId, destination);
                    history << "MoveLayer";
                }
            } else if (action == 24) {
                controller.setActiveLayerVisible(false);
                controller.setActiveLayerVisible(true);
                controller.setActiveLayerLocked(true);
                controller.setActiveLayerLocked(false);
                history << "LayerFlags";
            } else if (action == 25) {
                if (random.bounded(2) == 0 && controller.undoStack()->canUndo()) {
                    controller.undoStack()->undo(); history << "Undo";
                } else if (controller.undoStack()->canRedo()) {
                    controller.undoStack()->redo(); history << "Redo";
                }
            } else if (action == 26 && active) {
                const QStringList families = controller.fontFamilies();
                if (!families.isEmpty()) {
                    QString family;
                    const QStringList preferred = {QStringLiteral("Segoe UI"), QStringLiteral("Arial"),
                                                   QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans")};
                    for (int offset = 0; offset < preferred.size(); ++offset) {
                        const QString candidate = preferred.at((step + offset) % preferred.size());
                        const int available = families.indexOf(candidate, 0, Qt::CaseInsensitive);
                        if (available >= 0) {
                            family = families.at(available);
                            break;
                        }
                    }
                    if (family.isEmpty()) family = families.front();
                    controller.setFontFamily(family);
                    const QStringList styles = controller.fontStyles(family);
                    if (!styles.isEmpty()) {
                        controller.setFontStyle(styles.at(step % styles.size()));
                    }
                    history << QStringLiteral("FontFamilyStyle(%1)").arg(family);
                }
            } else if (action == 27) {
                controller.clearSelection();
                history << "ClearSelection";
            } else if (action == 28) {
                QHash<QString, QString> currentLayerForObject;
                if (const Page* page = controller.document().currentPage()) {
                    for (const auto& layer : page->layers) {
                        if (!layer || !layer->visible || layer->locked) continue;
                        for (const auto& object : layer->objects) {
                            if (object && object->visible) {
                                currentLayerForObject.insert(object->id, layer->id);
                            }
                        }
                    }
                }
                QStringList eligible;
                for (const SceneObjectGeometry& sceneObject : controller.sceneGeometry().objects) {
                    if (sceneObject.pageId == controller.document().currentPageId
                        && currentLayerForObject.value(sceneObject.objectId) == sceneObject.layerId
                        && sceneObject.visible && !sceneObject.locked) {
                        eligible.push_back(sceneObject.objectId);
                    }
                }
                if (!eligible.isEmpty()) {
                    controller.clearSelection();
                    const int firstIndex = random.bounded(eligible.size());
                    controller.selectObject(eligible.at(firstIndex));
                    if (eligible.size() > 1) {
                        const int secondIndex = (firstIndex + 1 + random.bounded(eligible.size() - 1))
                            % eligible.size();
                        controller.selectObject(eligible.at(secondIndex), true);
                    }
                    history << (eligible.size() > 1 ? "MultiSelect" : "Select");
                }
            } else if (action == 29 && active) {
                QStringList builtInPresetIds;
                for (const PresetCatalogEntry& entry : controller.presetCatalogEntries()) {
                    if (entry.builtIn) builtInPresetIds.push_back(entry.preset.id);
                }
                if (!builtInPresetIds.isEmpty()) {
                    const QString presetId = builtInPresetIds.at(random.bounded(builtInPresetIds.size()));
                    QString error;
                    QVERIFY2(controller.applyPresetById(presetId, &error),
                             qPrintable(context(step) + QStringLiteral("\naction=ApplyPreset(%1)\n%2")
                                                        .arg(presetId, error)));
                    history << QStringLiteral("ApplyPreset(%1)").arg(presetId);
                }
            } else if (action == 30) {
                const ExportScope scope = random.bounded(2) == 0
                    ? ExportScope::CurrentPage : ExportScope::Selection;
                if (controller.canExport(scope)) {
                    const QString path = exportDirectory.filePath(
                        QStringLiteral("seed-%1-step-%2.svg").arg(seed).arg(step));
                    QString error;
                    QVERIFY2(controller.exportSvg(path, scope, &error),
                             qPrintable(context(step)
                                        + QStringLiteral("\naction=Export(%1)\n%2")
                                              .arg(scope == ExportScope::CurrentPage
                                                       ? QStringLiteral("CurrentPage")
                                                       : QStringLiteral("Selection"),
                                                   error)));
                    QFile svg(path);
                    QVERIFY2(svg.open(QIODevice::ReadOnly), qPrintable(svg.errorString()));
                    const QByteArray bytes = svg.readAll();
                    QVERIFY(bytes.contains("<svg"));
                    history << (scope == ExportScope::CurrentPage
                                    ? "ExportCurrentPage" : "ExportSelection");
                } else {
                    history << "ExportUnavailable";
                }
            }
        }
        QCoreApplication::processEvents();
        const auto report = test::checkInvariants(controller.document(), nullptr,
                                                  controller.selectedObjectIds(),
                                                  controller.selectionModel()->activeObjectId());
        QVERIFY2(report.ok(), qPrintable(context(step) + QLatin1Char('\n') + report.summary()));
        if (step % 40 == 39) {
            Document loaded; QString error;
            QVERIFY2(ProjectSerializer::fromJson(ProjectSerializer::toJson(controller.document()), &loaded, &error), qPrintable(error));
            QVERIFY2(test::semanticallyEqual(controller.document(), loaded, &error),
                     qPrintable(context(step) + QLatin1Char('\n') + error));
        }
        if (step % 50 == 49 && controller.undoStack()->canUndo()) {
            const QString beforeUndo = test::semanticFingerprint(controller.document());
            controller.undoStack()->undo();
            const auto undoReport = test::checkInvariants(controller.document(), nullptr,
                                                           controller.selectedObjectIds(),
                                                           controller.selectionModel()->activeObjectId());
            QVERIFY2(undoReport.ok(), qPrintable(context(step) + QLatin1Char('\n') + undoReport.summary()));
            controller.undoStack()->redo();
            QVERIFY2(test::semanticFingerprint(controller.document()) == beforeUndo,
                     qPrintable(context(step) + QStringLiteral("\nredo did not restore the semantic document fingerprint")));
        }
    }
}

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    WorkflowIntegrationTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "workflow_integration_tests.moc"
