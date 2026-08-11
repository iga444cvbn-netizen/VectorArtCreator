#include "tests/support/ui_test_driver.h"
#include "tests/support/state_fingerprint.h"

#include "core/serialization/project_serializer.h"

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
    driver.expectVectorGeometry();
    driver.expectInvariants();
    driver.saveDiagnostic(QStringLiteral("smoke_first_five_minutes"));
}

QTEST_MAIN(SmokeWorkflowTests)
#include "smoke_workflow_tests.moc"
