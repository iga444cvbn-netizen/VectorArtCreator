#include "tests/support/invariant_checker.h"
#include "tests/support/state_fingerprint.h"

#include "core/serialization/project_serializer.h"
#include "ui/editor_controller.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QRandomGenerator>
#include <QTest>

using namespace vt;

class WorkflowIntegrationTests final : public QObject {
    Q_OBJECT

private slots:
    void semanticSaveLoadAndUndoRedoEquivalence();
    void latestAsyncTextGenerationWins();
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

void WorkflowIntegrationTests::seededValidWorkflows_data()
{
    QTest::addColumn<quint32>("seed");
    for (quint32 seed : {1u, 42u, 1337u, 8675309u, 20260811u}) QTest::newRow(QByteArray::number(seed).constData()) << seed;
}

void WorkflowIntegrationTests::seededValidWorkflows()
{
    QFETCH(quint32, seed);
    EditorController controller;
    QRandomGenerator random(seed);
    QStringList history;
    for (int step = 0; step < 120; ++step) {
        const int action = random.bounded(8);
        if (controller.selectedObjectIds().isEmpty() || action == 0) {
            const QString id = controller.createTextObject(QPointF(random.bounded(500), random.bounded(300)),
                                                           QStringLiteral("Seed %1 step %2").arg(seed).arg(step));
            history << QStringLiteral("Create(%1)").arg(id);
        } else if (action == 1) { controller.setText(QStringLiteral("edit %1").arg(step)); history << "Text"; }
        else if (action == 2) { controller.setFontItalic(step % 2); history << "Italic"; }
        else if (action == 3) { controller.moveSelectedObjects(QPointF(3, -2)); history << "Move"; }
        else if (action == 4) { controller.addEffect(step % 2 ? QStringLiteral("wave") : QStringLiteral("bend")); history << "Effect"; }
        else if (action == 5) { controller.duplicateSelectedObjects(); history << "Duplicate"; }
        else if (action == 6 && controller.undoStack()->canUndo()) { controller.undoStack()->undo(); history << "Undo"; }
        else if (action == 7 && controller.undoStack()->canRedo()) { controller.undoStack()->redo(); history << "Redo"; }
        QCoreApplication::processEvents();
        const auto report = test::checkInvariants(controller.document(), nullptr,
                                                  controller.selectedObjectIds(),
                                                  controller.selectionModel()->activeObjectId());
        QVERIFY2(report.ok(), qPrintable(QStringLiteral("seed %1 step %2\n%3\n%4")
                                          .arg(seed).arg(step).arg(history.join(", "), report.summary())));
        if (step % 30 == 29) {
            Document loaded; QString error;
            QVERIFY2(ProjectSerializer::fromJson(ProjectSerializer::toJson(controller.document()), &loaded, &error), qPrintable(error));
            QVERIFY2(test::semanticallyEqual(controller.document(), loaded, &error), qPrintable(error));
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
