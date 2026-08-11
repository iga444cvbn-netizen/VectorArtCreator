#include "tests/support/ui_test_driver.h"

#include "tests/support/geometry_assertions.h"
#include "tests/support/invariant_checker.h"
#include "tests/support/semantic_geometry.h"
#include "core/scene/scene_evaluator.h"
#include "ui/editor_canvas.h"
#include "ui/editor_controller.h"
#include "ui/main_window.h"
#include "ui/slider_spin_box.h"

#include <QCoreApplication>
#include <QApplication>
#include <QDir>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QInputMethodEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>
#include <QToolButton>

#include <algorithm>

namespace vt::test {
namespace {

QPlainTextEdit* findNativeEditor(const EditorCanvas& canvas)
{
    const auto* view = canvas.findChild<QGraphicsView*>();
    if (!view || !view->scene()) return nullptr;
    for (QGraphicsItem* item : view->scene()->items()) {
        if (auto* proxy = qgraphicsitem_cast<QGraphicsProxyWidget*>(item)) {
            if (auto* editor = qobject_cast<QPlainTextEdit*>(proxy->widget())) return editor;
        }
    }
    return nullptr;
}

QGraphicsProxyWidget* findNativeProxy(const EditorCanvas& canvas)
{
    const auto* view = canvas.findChild<QGraphicsView*>();
    if (!view || !view->scene()) return nullptr;
    for (QGraphicsItem* item : view->scene()->items()) {
        if (auto* proxy = qgraphicsitem_cast<QGraphicsProxyWidget*>(item)) {
            if (qobject_cast<QPlainTextEdit*>(proxy->widget())) return proxy;
        }
    }
    return nullptr;
}

} // namespace

UiTestDriver::UiTestDriver()
    : m_window(new MainWindow)
{
    m_window->resize(1400, 900);
    m_window->show();
    QCoreApplication::processEvents();
    m_controller = m_window->findChild<EditorController*>();
    m_canvas = m_window->findChild<EditorCanvas*>();
    QVERIFY2(m_controller, "MainWindow must expose its EditorController child");
    QVERIFY2(m_canvas, "MainWindow must expose its EditorCanvas child");
}

UiTestDriver::~UiTestDriver()
{
    delete m_window;
}

MainWindow& UiTestDriver::window() const { return *m_window; }
EditorController& UiTestDriver::controller() const { return *m_controller; }
EditorCanvas& UiTestDriver::canvas() const { return *m_canvas; }

void UiTestDriver::newProject()
{
    m_controller->newDocument();
    QCoreApplication::processEvents();
}

void UiTestDriver::clickAddText()
{
    auto* button = m_window->findChild<QPushButton*>(QStringLiteral("addTextButton"));
    QVERIFY2(button, "Add Text button needs a stable objectName");
    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(m_canvas->isTextEditing(), 3000);
}

void UiTestDriver::selectTool(const QString& stableToolName)
{
    auto* button = m_window->findChild<QToolButton*>(QStringLiteral("tool/%1").arg(stableToolName));
    QVERIFY2(button, "requested tool button is missing");
    QTest::mouseClick(button, Qt::LeftButton);
}

void UiTestDriver::clickCanvasAtDocumentPoint(const QPointF& point)
{
    QTest::mouseClick(m_canvas, Qt::LeftButton, Qt::NoModifier,
                      m_canvas->mapDocumentToViewport(point).toPoint());
}

void UiTestDriver::typeText(const QString& text)
{
    QWidget* target = QApplication::focusWidget();
    QVERIFY2(target && target->isVisible(), "user typing requires an actual visible focus widget");
    if (text.isEmpty()) return;
    const bool ascii = std::all_of(text.cbegin(), text.cend(), [](QChar value) { return value.unicode() < 0x80; });
    if (ascii) {
        QTest::keyClicks(target, text);
    } else {
        QInputMethodEvent commit;
        commit.setCommitString(text);
        QCoreApplication::sendEvent(target, &commit);
    }
}

void UiTestDriver::pressKey(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    QWidget* target = QApplication::focusWidget();
    QVERIFY2(target && target->isVisible(), "user key input requires an actual visible focus widget");
    QTest::keyClick(target, key, modifiers);
}

void UiTestDriver::forceEditorFocusForSetup()
{
    QPlainTextEdit* editor = nativeEditor();
    QVERIFY2(editor && editor->isVisible(), "visible native editor is required for forced setup focus");
    editor->setFocus(Qt::OtherFocusReason);
}

void UiTestDriver::exitTextEditing()
{
    pressKey(Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(!m_canvas->isTextEditing(), 3000);
}

void UiTestDriver::selectObject(const QString& id)
{
    m_controller->selectObject(id);
    QCoreApplication::processEvents();
}

void UiTestDriver::applyBuiltInStyle(const QString& name)
{
    const auto entries = m_controller->presetCatalogEntries();
    for (const PresetCatalogEntry& entry : entries) {
        if (entry.builtIn && entry.preset.name == name) {
            QString error;
            QVERIFY2(m_controller->applyPresetById(entry.preset.id, &error), qPrintable(error));
            return;
        }
    }
    QFAIL("requested built-in style was not found");
}

void UiTestDriver::setStyleIntensity(double value)
{
    auto* control = m_window->findChild<SliderSpinBox*>(QStringLiteral("styleIntensity"));
    QVERIFY(control);
    control->spinBox()->setValue(value);
}

void UiTestDriver::undo() { m_controller->undoStack()->undo(); QCoreApplication::processEvents(); }
void UiTestDriver::redo() { m_controller->undoStack()->redo(); QCoreApplication::processEvents(); }

void UiTestDriver::waitForSceneGeneration(const QString& objectId) const
{
    if (objectId.isEmpty()) {
        QTRY_VERIFY_WITH_TIMEOUT(m_controller->sceneGeometry().pageId == m_controller->document().currentPageId, 5000);
    } else {
        const TextObject* documentObject = m_controller->document().objectById(objectId);
        QVERIFY(documentObject);
        QTRY_VERIFY_WITH_TIMEOUT(m_controller->sceneGeometry().objectById(objectId) != nullptr, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->sceneGeometry().objectById(objectId)->sourceText,
                                  documentObject->sourceText, 5000);
        if (!documentObject->sourceText.isEmpty()) {
            QTRY_VERIFY_WITH_TIMEOUT(m_controller->sceneGeometry().objectById(objectId)
                                     ->geometry.hasVisibleGeometry(), 5000);
        }
        const SceneGeometry expected = SceneEvaluator::evaluate(*m_controller->document().currentPage());
        const SceneObjectGeometry* expectedObject = expected.objectById(objectId);
        QVERIFY(expectedObject);
        QString difference;
        QVERIFY2(compareSceneObject(sceneObjectSignature(*expectedObject),
                                    sceneObjectSignature(*m_controller->sceneGeometry().objectById(objectId)),
                                    &difference), qPrintable(difference));
    }
}

QString UiTestDriver::activeObjectId() const { return m_controller->selectionModel()->activeObjectId(); }
QPlainTextEdit* UiTestDriver::nativeEditor() const { return findNativeEditor(*m_canvas); }
QGraphicsProxyWidget* UiTestDriver::nativeEditorProxy() const { return findNativeProxy(*m_canvas); }

void UiTestDriver::expectEditing(bool expected) const { QCOMPARE(m_canvas->isTextEditing(), expected); }

void UiTestDriver::expectActiveText(const QString& text) const
{
    const TextObject* object = m_controller->document().objectById(activeObjectId());
    QVERIFY(object);
    QTRY_COMPARE_WITH_TIMEOUT(object->sourceText, text, 3000);
}

void UiTestDriver::expectVectorGeometry() const
{
    const QString id = activeObjectId();
    QVERIFY(!id.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->sceneGeometry().objectById(id) != nullptr, 5000);
    const auto* sceneObject = m_controller->sceneGeometry().objectById(id);
    QVERIFY(sceneObject->geometry.hasVisibleGeometry());
    QString error;
    QVERIFY2(hasFiniteGeometry(sceneObject->geometry, &error), qPrintable(error));
}

void UiTestDriver::expectEditorAttachedToActiveObject() const
{
    const auto* sceneObject = m_controller->sceneGeometry().objectById(activeObjectId());
    QVERIFY(sceneObject);
    QString error;
    const QPointF origin = m_canvas->mapDocumentToViewport(QPointF());
    const QPointF x = m_canvas->mapDocumentToViewport(QPointF(1.0, 0.0)) - origin;
    const QPointF y = m_canvas->mapDocumentToViewport(QPointF(0.0, 1.0)) - origin;
    const QTransform documentToView(x.x(), x.y(), 0.0, y.x(), y.y(), 0.0,
                                    origin.x(), origin.y(), 1.0);
    QVERIFY2(editorOverlayAttached(nativeEditorProxy(), *sceneObject, documentToView,
                                    &error), qPrintable(error));
}

void UiTestDriver::expectInvariants() const
{
    const InvariantReport report = checkInvariants(m_controller->document(), &m_controller->sceneGeometry(),
                                                    m_controller->selectedObjectIds(), activeObjectId(),
                                                    m_canvas->editingObjectId());
    QVERIFY2(report.ok(), qPrintable(report.summary()));
}

void UiTestDriver::saveDiagnostic(const QString& name) const
{
    QDir().mkpath(QStringLiteral("test-artifacts"));
    m_window->grab().save(QStringLiteral("test-artifacts/%1.png").arg(name));
}

} // namespace vt::test
