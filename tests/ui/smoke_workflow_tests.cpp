#include "tests/support/ui_test_driver.h"
#include "tests/support/state_fingerprint.h"

#include "core/serialization/project_serializer.h"
#include "ui/editor_controller.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace vt;

class SmokeWorkflowTests final : public QObject {
    Q_OBJECT

private slots:
    void addTextLatin();
    void addTextCyrillic();
    void escapeLeavesVisibleVectorText();
    void emptyObjectPersistsAndAcceptsFirstText();
    void firstFiveMinutesCanary();
    void regionHoleEditRemoveAndPersistence();
};

void SmokeWorkflowTests::addTextLatin()
{
    test::UiTestDriver driver;
    driver.clickAddText();
    driver.typeText(QStringLiteral("Hello world"));
    driver.expectActiveText(QStringLiteral("Hello world"));
    driver.waitForSceneGeneration(driver.activeObjectId());
    driver.expectVectorGeometry();
    driver.expectEditorAttachedToActiveObject();
    driver.expectInvariants();
}

void SmokeWorkflowTests::addTextCyrillic()
{
    test::UiTestDriver driver;
    const QString text = QString::fromUtf8("\320\237\321\200\320\270\320\262\320\265\321\202, \320\274\320\270\321\200!");
    driver.clickAddText();
    driver.typeText(text);
    driver.expectActiveText(text);
    driver.waitForSceneGeneration(driver.activeObjectId());
    driver.expectVectorGeometry();
    driver.expectEditorAttachedToActiveObject();
    driver.expectInvariants();
}

void SmokeWorkflowTests::escapeLeavesVisibleVectorText()
{
    test::UiTestDriver driver;
    driver.clickAddText();
    driver.typeText(QStringLiteral("Escape keeps this text"));
    const QString id = driver.activeObjectId();
    driver.waitForSceneGeneration(id);
    driver.exitTextEditing();
    driver.expectEditing(false);
    driver.expectVectorGeometry();
    driver.expectInvariants();
}

void SmokeWorkflowTests::emptyObjectPersistsAndAcceptsFirstText()
{
    test::UiTestDriver driver;
    driver.clickAddText();
    const QString id = driver.activeObjectId();
    driver.exitTextEditing();
    QVERIFY(driver.controller().document().objectById(id));
    driver.selectObject(id);
    driver.controller().setText(QStringLiteral("First text"));
    driver.waitForSceneGeneration(id);
    driver.expectVectorGeometry();
    driver.expectInvariants();
}

void SmokeWorkflowTests::firstFiveMinutesCanary()
{
    test::UiTestDriver driver;
    driver.clickAddText();
    driver.typeText(QStringLiteral("DON'T LOOK BACK"));
    const QString id = driver.activeObjectId();
    driver.waitForSceneGeneration(id);
    driver.exitTextEditing();
    driver.applyBuiltInStyle(QStringLiteral("Panic"));
    driver.setStyleIntensity(1.5);
    ObjectTransform transform = driver.controller().document().objectById(id)->transform;
    transform.rotation = 30.0;
    driver.controller().setObjectTransform(id, transform);
    driver.waitForSceneGeneration(id);
    driver.controller().setTypographyLayoutMode(TypographyLayoutMode::Region);
    driver.controller().createRegionRectangle();
    driver.controller().setText(QString::fromUtf8("Region canary: Привет 😀 shape typography"));
    driver.waitForSceneGeneration(id);
    driver.controller().setRegionPadding(RegionPaddingSide::Left, 12.0);
    driver.controller().setRegionHorizontalAlignment(RegionHorizontalAlignment::Center);
    driver.controller().setRegionVerticalAlignment(RegionVerticalAlignment::Bottom);
    TextObject* regionObject = driver.controller().document().objectById(id);
    QVERIFY(regionObject && regionObject->region.has_value());
    PathGeometry editedOuter = regionObject->region->outer;
    editedOuter.nodes[1].anchor += QPointF(20.0, 5.0);
    driver.controller().setPathGeometry(id, editedOuter, driver.controller().spatialRevision());
    driver.waitForSceneGeneration(id);
    const QString afterRegionEdits = test::semanticFingerprint(driver.controller().document());
    driver.undo();
    driver.redo();
    QCOMPARE(test::semanticFingerprint(driver.controller().document()), afterRegionEdits);

    QTemporaryDir artifacts;
    QVERIFY(artifacts.isValid());
    const QString svgPath = artifacts.filePath(QStringLiteral("region-canary.svg"));
    const QString projectPath = artifacts.filePath(QStringLiteral("region-canary.vtproj"));
    QString error;
    QVERIFY2(driver.controller().exportSvg(svgPath, ExportScope::CurrentPage, &error),
             qPrintable(error));
    QFile svg(svgPath);
    QVERIFY(svg.open(QIODevice::ReadOnly));
    QVERIFY(svg.readAll().contains("<path"));
    QVERIFY2(driver.controller().saveProject(projectPath, &error), qPrintable(error));
    const QString savedFingerprint = test::semanticFingerprint(driver.controller().document());
    QVERIFY2(driver.controller().openProject(projectPath, &error), qPrintable(error));
    QCOMPARE(test::semanticFingerprint(driver.controller().document()), savedFingerprint);
    driver.selectObject(id);
    driver.waitForSceneGeneration(id);
    driver.expectVectorGeometry();
    driver.expectInvariants();
    driver.saveDiagnostic(QStringLiteral("smoke_first_five_minutes"));
}

void SmokeWorkflowTests::regionHoleEditRemoveAndPersistence()
{
    test::UiTestDriver driver;
    driver.clickAddText();
    driver.typeText(QStringLiteral("Region hole smoke workflow"));
    const QString id = driver.activeObjectId();
    driver.exitTextEditing();
    driver.controller().setTypographyLayoutMode(TypographyLayoutMode::Region);
    driver.controller().createRegionRectangle();
    driver.waitForSceneGeneration(id);

    TextObject* object = driver.controller().document().objectById(id);
    QVERIFY(object && object->region.has_value());
    QCOMPARE(object->region->holes.size(), 0);

    driver.controller().addRegionHole();
    object = driver.controller().document().objectById(id);
    QVERIFY(object && object->region.has_value());
    QCOMPARE(object->region->holes.size(), 1);
    const QString holeId = object->region->holes.front().id;
    driver.controller().editRegionHoleContour();
    QVERIFY(driver.controller().activeRegionContour());
    QCOMPARE(driver.controller().activeRegionContour()->id, holeId);

    PathGeometry editedHole = *driver.controller().activeRegionContour();
    editedHole.nodes.front().anchor += QPointF(2.0, 1.0);
    driver.controller().setPathGeometry(id, editedHole, driver.controller().spatialRevision());
    driver.waitForSceneGeneration(id);
    object = driver.controller().document().objectById(id);
    QVERIFY(object && object->region.has_value());
    QCOMPARE(object->region->holes.size(), 1);
    QCOMPARE(object->region->holes.front().id, holeId);
    QCOMPARE(object->region->holes.front().nodes.front().anchor, editedHole.nodes.front().anchor);
    driver.expectInvariants();

    const QString afterEdit = test::semanticFingerprint(driver.controller().document());
    driver.undo();
    object = driver.controller().document().objectById(id);
    QVERIFY(object && object->region.has_value());
    QCOMPARE(object->region->holes.size(), 1);
    QVERIFY(object->region->holes.front().nodes.front().anchor != editedHole.nodes.front().anchor);
    driver.redo();
    QCOMPARE(test::semanticFingerprint(driver.controller().document()), afterEdit);

    driver.controller().removeRegionHole();
    object = driver.controller().document().objectById(id);
    QVERIFY(object && object->region.has_value());
    QCOMPARE(object->region->holes.size(), 0);
    driver.undo();
    object = driver.controller().document().objectById(id);
    QVERIFY(object && object->region.has_value());
    QCOMPARE(object->region->holes.size(), 1);
    driver.redo();
    object = driver.controller().document().objectById(id);
    QVERIFY(object && object->region.has_value());
    QCOMPARE(object->region->holes.size(), 0);
    driver.expectInvariants();

    QTemporaryDir artifacts;
    QVERIFY(artifacts.isValid());
    const QString projectPath = artifacts.filePath(QStringLiteral("region-hole-smoke.vtproj"));
    QString error;
    QVERIFY2(driver.controller().saveProject(projectPath, &error), qPrintable(error));
    const QString savedFingerprint = test::semanticFingerprint(driver.controller().document());
    QVERIFY2(driver.controller().openProject(projectPath, &error), qPrintable(error));
    QCOMPARE(test::semanticFingerprint(driver.controller().document()), savedFingerprint);
    driver.selectObject(id);
    driver.waitForSceneGeneration(id);
    driver.expectVectorGeometry();
    driver.expectInvariants();
}

QTEST_MAIN(SmokeWorkflowTests)
#include "smoke_workflow_tests.moc"
