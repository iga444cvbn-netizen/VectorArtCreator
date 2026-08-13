#include "core/effects/effect_registry.h"
#include "core/effects/wave_effect.h"
#include "core/serialization/project_serializer.h"
#include "tests/support/state_fingerprint.h"
#include "ui/editor_canvas.h"
#include "ui/editor_controller.h"
#include "ui/effects_panel.h"
#include "ui/main_window.h"
#include "ui/transform_panel.h"
#include "ui/typography_panel.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QGraphicsView>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QLineF>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QTest>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QWheelEvent>

#include <cmath>

using namespace vt;

class EffectsPanelUiTests final : public QObject {
    Q_OBJECT

private slots:
    void valueRefreshKeepsEmittingControlsAlive();
    void deformationAndMaskStateDoNotOverwriteEachOther();
    void spinBoxArrowHitRegionsIncrementAndDecrement();
    void selectingAnotherLayerObjectEndsNativeEditorSession();
    void nativeEditorViewportRoutesOutsideCanvasInput();
    void addTextStartsFocusedAndAlignedBeforeAndAfterScenePublication();
    void textToolStartsFocusedAtCurrentZoom();
    void traitModeIsShownAfterBoldAndItalic();
    void pathTypographyControlsAndAnchorGesture();
    void scaleControlsPreserveSmallAndMirroredValues();
    void inspectorEditEndsCanvasSessionWithoutStaleOverwrite();
    void styleIntensityGestureHasImmediateDirtyTruthAndOneUndoStep();
    void rotatedMarqueeUsesInkAsNarrowPhase();
    void objectRowLayerButtonsOperateOnParentLayer();
    void unsupportedEffectDisablesMaskUiAcrossRefreshes_data();
    void unsupportedEffectDisablesMaskUiAcrossRefreshes();
};

namespace {

QPoint canvasPositionForDocumentPoint(const EditorCanvas* canvas, const QPointF& documentPoint)
{
    const QPointF pageCenter(600.0, 400.0);
    return (QPointF(canvas->width() * 0.5, canvas->height() * 0.5)
            + (documentPoint - pageCenter) * canvas->zoom()).toPoint();
}

void beginNativeEdit(EditorCanvas* canvas, EditorController* controller, const QString& objectId)
{
    const TextObject* object = controller->document().objectById(objectId);
    QVERIFY(object);
    canvas->beginTextEditing(objectId, object->sourceText,
                             object->font.toQFont(object->typography.fontSize),
                             QRectF(object->transform.position, QSizeF(180.0, 60.0)));
    QVERIFY(canvas->isTextEditing());
}

QPlainTextEdit* nativeTextEditor(QGraphicsView* editorView)
{
    if (!editorView || !editorView->scene()) return nullptr;
    for (QGraphicsItem* item : editorView->scene()->items()) {
        if (auto* proxy = qgraphicsitem_cast<QGraphicsProxyWidget*>(item)) {
            if (auto* editor = qobject_cast<QPlainTextEdit*>(proxy->widget())) return editor;
        }
    }
    return nullptr;
}

QPoint sliderHandleCenter(const QSlider* slider)
{
    QStyleOptionSlider option;
    option.initFrom(slider);
    option.orientation = slider->orientation();
    option.minimum = slider->minimum();
    option.maximum = slider->maximum();
    option.sliderPosition = slider->sliderPosition();
    option.sliderValue = slider->value();
    option.singleStep = slider->singleStep();
    option.pageStep = slider->pageStep();
    option.tickPosition = slider->tickPosition();
    option.tickInterval = slider->tickInterval();
    option.upsideDown = slider->invertedAppearance();
    return slider->style()
        ->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, slider)
        .center();
}

void typeUnicode(QPlainTextEdit* editor, const QString& text)
{
    QInputMethodEvent commit;
    commit.setCommitString(text);
    QCoreApplication::sendEvent(editor, &commit);
}

} // namespace

void EffectsPanelUiTests::valueRefreshKeepsEmittingControlsAlive()
{
    TextObject object;
    auto wave = std::make_unique<WaveEffect>();
    const QString effectId = wave->instanceId;
    object.effects.append(std::move(wave));

    EffectsPanel panel;
    panel.show();
    panel.refresh(&object, {});

    auto* master = panel.findChild<SliderSpinBox*>(QStringLiteral("masterStrength"));
    auto* amplitude = panel.findChild<SliderSpinBox*>(QStringLiteral("effectParameter/amplitude"));
    auto* enabled = panel.findChild<QCheckBox*>(QStringLiteral("effectEnabled"));
    QVERIFY(master);
    QVERIFY(amplitude);
    QVERIFY(enabled);
    QPointer<SliderSpinBox> retainedMaster(master);
    QPointer<SliderSpinBox> retainedAmplitude(amplitude);
    QPointer<QCheckBox> retainedEnabled(enabled);

    connect(&panel, &EffectsPanel::effectMasterStrengthChanged, &panel,
            [&object, &panel, effectId](int, double value) {
                object.effects.byInstanceId(effectId)->masterStrength = value;
                panel.refresh(&object, {});
            });
    connect(&panel, &EffectsPanel::effectParameterChanged, &panel,
            [&object, &panel, effectId](int, const QString& parameter, double value) {
                static_cast<void>(object.effects.byInstanceId(effectId)->setParameter(parameter, value));
                panel.refresh(&object, {});
            });
    connect(&panel, &EffectsPanel::effectEnabledChanged, &panel,
            [&object, &panel, effectId](int, bool value) {
                object.effects.byInstanceId(effectId)->enabled = value;
                panel.refresh(&object, {});
            });

    master->spinBox()->setValue(0.4);
    amplitude->spinBox()->setValue(0.6);
    enabled->setChecked(false);
    QVERIFY(retainedMaster);
    QVERIFY(retainedAmplitude);
    QVERIFY(retainedEnabled);
    QCOMPARE(retainedMaster.data(), master);
    QCOMPARE(retainedAmplitude.data(), amplitude);
    QCOMPARE(retainedEnabled.data(), enabled);
    QCOMPARE(object.effects.byInstanceId(effectId)->masterStrength, 0.4);
    QVERIFY(!object.effects.byInstanceId(effectId)->enabled);
}

void EffectsPanelUiTests::deformationAndMaskStateDoNotOverwriteEachOther()
{
    MainWindow window;
    auto* controller = window.findChild<EditorController*>();
    auto* canvas = window.findChild<EditorCanvas*>();
    QVERIFY(controller);
    QVERIFY(canvas);

    const QVector<QPair<EditorTool, BrushMode>> tools = {
        {EditorTool::Push, BrushMode::Push}, {EditorTool::Pull, BrushMode::Pull},
        {EditorTool::Inflate, BrushMode::Inflate}, {EditorTool::Pinch, BrushMode::Pinch},
        {EditorTool::Smooth, BrushMode::Smooth},
    };
    for (const auto& [tool, mode] : tools) {
        controller->setTool(tool);
        QCOMPARE(static_cast<int>(canvas->brushMode()), static_cast<int>(mode));
    }

    controller->setTool(EditorTool::Pull);
    controller->setBrushSettings(BrushTarget::Glyphs, 91.0, 1.1, 0.25);
    QCOMPARE(static_cast<int>(canvas->brushMode()), static_cast<int>(BrushMode::Pull));
    QCOMPARE(static_cast<int>(canvas->brushTarget()), static_cast<int>(BrushTarget::Glyphs));

    controller->setTool(EditorTool::Smooth);
    QCOMPARE(static_cast<int>(canvas->brushTarget()), static_cast<int>(BrushTarget::Shape));
    controller->setTool(EditorTool::Pull);
    QCOMPARE(static_cast<int>(canvas->brushTarget()), static_cast<int>(BrushTarget::Glyphs));

    controller->setBrushSettings(BrushTarget::Shape, 91.0, 1.1, 0.25);
    controller->setTool(EditorTool::Smooth);
    controller->setTool(EditorTool::Pull);
    QCOMPARE(static_cast<int>(canvas->brushTarget()), static_cast<int>(BrushTarget::Shape));

    controller->setMaskBrushSettings(15.0, 0.35, 0.8, true);
    controller->setMaskRestoreMode(false);
    QCOMPARE(static_cast<int>(canvas->brushMode()), static_cast<int>(BrushMode::Pull));
    QCOMPARE(static_cast<int>(canvas->brushTarget()), static_cast<int>(BrushTarget::Shape));
}

void EffectsPanelUiTests::spinBoxArrowHitRegionsIncrementAndDecrement()
{
    SliderSpinBox control;
    control.setRange(0.0, 10.0);
    control.setSingleStep(0.5);
    control.setValue(5.0);
    control.show();
    // The CI test target deliberately uses Qt's offscreen platform plugin;
    // it has no native exposed-window event. Process the layout instead of
    // asserting a condition that is impossible for that platform.
    QCoreApplication::processEvents();

    QDoubleSpinBox* spinBox = control.spinBox();
    QStyleOptionSpinBox option;
    option.rect = spinBox->rect();
    option.state = QStyle::State_Enabled;
    option.direction = spinBox->layoutDirection();
    const QRect up = spinBox->style()->subControlRect(QStyle::CC_SpinBox,
                                                       &option,
                                                       QStyle::SC_SpinBoxUp,
                                                       spinBox);
    const QRect down = spinBox->style()->subControlRect(QStyle::CC_SpinBox,
                                                         &option,
                                                         QStyle::SC_SpinBoxDown,
                                                         spinBox);
    QVERIFY(up.isValid());
    QVERIFY(down.isValid());

    QTest::mouseClick(spinBox, Qt::LeftButton, Qt::NoModifier, up.center());
    QCOMPARE(spinBox->value(), 5.5);
    QTest::mouseClick(spinBox, Qt::LeftButton, Qt::NoModifier, down.center());
    QCOMPARE(spinBox->value(), 5.0);
}

void EffectsPanelUiTests::selectingAnotherLayerObjectEndsNativeEditorSession()
{
    MainWindow window;
    auto* controller = window.findChild<EditorController*>();
    auto* canvas = window.findChild<EditorCanvas*>();
    QVERIFY(controller);
    QVERIFY(canvas);
    const QString first = controller->createTextObject(QPointF(20.0, 20.0), QStringLiteral("A"));
    const QString second = controller->createTextObject(QPointF(160.0, 20.0), QStringLiteral("B"));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(controller->sceneGeometry().objectById(first)
                                 && controller->sceneGeometry().objectById(second),
                             5000);
    controller->selectObject(first);
    const TextObject* object = controller->activeObject();
    QVERIFY(object);
    canvas->beginTextEditing(first, object->sourceText,
                             object->font.toQFont(object->typography.fontSize),
                             QRectF(20.0, 20.0, 160.0, 60.0));
    QVERIFY(canvas->isTextEditing());
    controller->selectObject(second); // same path used by LayersPanel::objectSelected
    QCoreApplication::processEvents();
    QVERIFY(!canvas->isTextEditing());
}

void EffectsPanelUiTests::addTextStartsFocusedAndAlignedBeforeAndAfterScenePublication()
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    auto* controller = window.findChild<EditorController*>();
    auto* canvas = window.findChild<EditorCanvas*>();
    auto* addText = window.findChild<QPushButton*>(QStringLiteral("addTextButton"));
    QVERIFY(controller);
    QVERIFY(canvas);
    QVERIFY(addText);

    QTest::mouseClick(addText, Qt::LeftButton);
    QTRY_VERIFY(canvas->isTextEditing());
    auto* editorView = canvas->findChild<QGraphicsView*>();
    QVERIFY(editorView);
    auto* editor = nativeTextEditor(editorView);
    QVERIFY(editor);

    const QString objectId = canvas->editingObjectId();
    const TextObject* object = controller->document().objectById(objectId);
    QVERIFY(object);
    QGraphicsProxyWidget* proxy = nullptr;
    for (QGraphicsItem* item : editorView->scene()->items()) {
        if (auto* candidate = qgraphicsitem_cast<QGraphicsProxyWidget*>(item)) {
            proxy = candidate;
            break;
        }
    }
    QVERIFY(proxy);
    QTRY_VERIFY(editorView->scene()->focusItem() == proxy || editor->hasFocus());
    const QPointF fallbackOrigin = proxy->sceneBoundingRect().topLeft();
    QVERIFY(!fallbackOrigin.isNull()); // regression: this is the pre-async fallback geometry path

    const QString cyrillic = QString::fromUtf8("\320\237\321\200\320\270\320\262\320\265\321\202, \320\274\320\270\321\200!");
    typeUnicode(editor, cyrillic);
    QTRY_COMPARE(controller->document().objectById(objectId)->sourceText, cyrillic);
    QTRY_VERIFY(controller->sceneGeometry().objectById(objectId) != nullptr);
    QTRY_VERIFY(controller->sceneGeometry().objectById(objectId)->geometry.hasVisibleGeometry());
    QVERIFY((proxy->sceneBoundingRect().topLeft() - fallbackOrigin).manhattanLength() <= 3.0);
    canvas->finishTextEditing();
    QCoreApplication::processEvents();
}

void EffectsPanelUiTests::textToolStartsFocusedAtCurrentZoom()
{
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        QSKIP("Qt 6.8 offscreen crashes while dispatching a mouse-created QGraphicsProxyWidget session; native-platform coverage remains required.");
    }
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    auto* controller = window.findChild<EditorController*>();
    auto* canvas = window.findChild<EditorCanvas*>();
    QVERIFY(controller);
    QVERIFY(canvas);
    canvas->zoomOut();
    canvas->zoomOut();
    auto* textTool = window.findChild<QToolButton*>(QStringLiteral("tool/text"));
    QVERIFY(textTool);
    QTest::mouseClick(textTool, Qt::LeftButton);
    const QPoint createAt = canvasPositionForDocumentPoint(canvas, QPointF(240.0, 220.0));
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, createAt);
    QTRY_VERIFY(canvas->isTextEditing());
    auto* editorView = canvas->findChild<QGraphicsView*>();
    QVERIFY(editorView);
    auto* editor = nativeTextEditor(editorView);
    QVERIFY(editor);
    QTRY_VERIFY(editorView->scene()->focusItem() != nullptr || editor->hasFocus());
    typeUnicode(editor, QStringLiteral("Test 123"));
    const QString objectId = canvas->editingObjectId();
    QTRY_COMPARE(controller->document().objectById(objectId)->sourceText, QStringLiteral("Test 123"));
    QTRY_VERIFY(controller->sceneGeometry().objectById(objectId) != nullptr);
    QTRY_VERIFY(controller->sceneGeometry().objectById(objectId)->geometry.hasVisibleGeometry());
    canvas->finishTextEditing();
    QCoreApplication::processEvents();
}

void EffectsPanelUiTests::nativeEditorViewportRoutesOutsideCanvasInput()
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();
    auto* controller = window.findChild<EditorController*>();
    auto* canvas = window.findChild<EditorCanvas*>();
    QVERIFY(controller);
    QVERIFY(canvas);

    const QPointF firstPosition(120.0, 400.0);
    const QPointF secondPosition(1080.0, 400.0);
    const QString first = controller->createTextObject(firstPosition, QStringLiteral("A"));
    const QString second = controller->createTextObject(secondPosition, QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(controller->sceneGeometry().objectById(first)
                                 && controller->sceneGeometry().objectById(second),
                             5000);
    controller->selectObject(first);
    beginNativeEdit(canvas, controller, first);

    auto* editorView = canvas->findChild<QGraphicsView*>();
    QVERIFY(editorView);
    QPlainTextEdit* editor = nullptr;
    QGraphicsProxyWidget* editorProxy = nullptr;
    for (QGraphicsItem* item : editorView->scene()->items()) {
        auto* proxy = qgraphicsitem_cast<QGraphicsProxyWidget*>(item);
        if (proxy) {
            editor = qobject_cast<QPlainTextEdit*>(proxy->widget());
            if (editor) {
                editorProxy = proxy;
                break;
            }
        }
    }
    QVERIFY(editor);
    QVERIFY(editorProxy);
    QWidget* viewport = editorView->viewport();
    QVERIFY(viewport);

    const QPoint insideEditor = viewport->mapFrom(editorView,
        editorView->mapFromScene(editorProxy->mapToScene(editorProxy->boundingRect().center())));
    QVERIFY(viewport->rect().contains(insideEditor));
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, insideEditor);
    QVERIFY(canvas->isTextEditing());

    // Use a simple page-space hit region for B. This keeps the routing test
    // independent of font availability while still exercising MainWindow's
    // real objectClicked -> selection path.
    SceneGeometry hitScene = controller->sceneGeometry();
    SceneObjectGeometry* secondObject = hitScene.objectById(second);
    QVERIFY(secondObject);
    GeometryPiece secondHitRegion;
    secondHitRegion.path.addRect(QRectF(1020.0, 360.0, 120.0, 80.0));
    secondObject->geometry = VectorGeometry();
    secondObject->geometry.pieces.push_back(secondHitRegion);
    secondObject->geometry.setReferenceBounds(secondHitRegion.path.boundingRect());
    secondObject->geometry.recomputeBounds();
    canvas->setScene(hitScene, controller->selectedObjectIds(),
                     controller->selectionModel()->activeObjectId());
    const QPoint secondInViewport = viewport->mapFrom(canvas,
        canvasPositionForDocumentPoint(canvas, QPointF(1080.0, 400.0)));
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, secondInViewport);
    QTRY_COMPARE(controller->selectionModel()->activeObjectId(), second);
    QVERIFY(!canvas->isTextEditing());

    controller->selectObject(first);
    beginNativeEdit(canvas, controller, first);
    const QPoint emptyInCanvas = canvasPositionForDocumentPoint(canvas, QPointF(600.0, 740.0));
    const QPoint emptyInViewport = viewport->mapFrom(canvas, emptyInCanvas);
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, emptyInViewport);
    // The canvas owns the drag after the outside press, so deliver the paired
    // release to the canvas just as a real mouse grab would.
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, emptyInCanvas);
    QTRY_VERIFY(!canvas->isTextEditing());
    QTRY_VERIFY(controller->selectionModel()->selectedObjectIds().isEmpty());

    controller->selectObject(first);
    beginNativeEdit(canvas, controller, first);
    QSignalSpy zoomSpy(canvas, &EditorCanvas::zoomChanged);
    const qreal beforeZoom = canvas->zoom();
    QWheelEvent wheel(QPointF(emptyInViewport),
                      QPointF(viewport->mapToGlobal(emptyInViewport)),
                      QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(viewport, &wheel);
    QTRY_VERIFY(!zoomSpy.isEmpty());
    QVERIFY(canvas->zoom() > beforeZoom);
    QVERIFY(canvas->isTextEditing());
}

void EffectsPanelUiTests::inspectorEditEndsCanvasSessionWithoutStaleOverwrite()
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();
    auto* controller = window.findChild<EditorController*>();
    auto* canvas = window.findChild<EditorCanvas*>();
    auto* addText = window.findChild<QPushButton*>(QStringLiteral("addTextButton"));
    auto* inspectorText = window.findChild<QPlainTextEdit*>(QStringLiteral("textSource"));
    auto* italic = window.findChild<QPushButton*>(QStringLiteral("fontItalic"));
    auto* fontSize = window.findChild<SliderSpinBox*>(QStringLiteral("fontSize"));
    QVERIFY(controller);
    QVERIFY(canvas);
    QVERIFY(addText);
    QVERIFY(inspectorText);
    QVERIFY(italic);
    QVERIFY(fontSize);

    QTest::mouseClick(addText, Qt::LeftButton);
    QTRY_VERIFY(canvas->isTextEditing());
    auto* editorView = canvas->findChild<QGraphicsView*>();
    auto* editor = nativeTextEditor(editorView);
    QVERIFY(editor);
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClicks(editor, QStringLiteral("abc"));
    const QString objectId = canvas->editingObjectId();
    QTRY_COMPARE(controller->document().objectById(objectId)->sourceText, QStringLiteral("abc"));

    QTest::mouseClick(inspectorText->viewport(), Qt::LeftButton);
    inspectorText->selectAll();
    QTest::keyClicks(inspectorText, QStringLiteral("XYZ"));
    QTRY_COMPARE(controller->document().objectById(objectId)->sourceText, QStringLiteral("XYZ"));
    QVERIFY(!canvas->isTextEditing());
    QVERIFY(editor); // the reusable widget may remain allocated, but is inactive
    QVERIFY(!editor->isVisible());

    QTRY_VERIFY_WITH_TIMEOUT(controller->sceneGeometry().objectById(objectId) != nullptr, 5000);
    // This test owns the canvas/inspector transaction boundary. The exact
    // mouse hit-test path is covered separately and has a documented native-
    // platform gap when the offscreen runner supplies no fillable font ink.
    beginNativeEdit(canvas, controller, objectId);
    QTRY_VERIFY(canvas->isTextEditing());
    QTRY_COMPARE(editor->toPlainText(), QStringLiteral("XYZ"));
    QTest::keyClicks(editor, QStringLiteral("!"));
    QTRY_COMPARE(controller->document().objectById(objectId)->sourceText, QStringLiteral("XYZ!"));

    const bool originalItalic = controller->document().objectById(objectId)->font.italic;
    QTest::mouseClick(italic, Qt::LeftButton);
    QTRY_COMPARE(controller->document().objectById(objectId)->font.italic, !originalItalic);
    QVERIFY(!canvas->isTextEditing());
    controller->undoStack()->undo();
    QTRY_COMPARE(controller->document().objectById(objectId)->font.italic, originalItalic);
    QCOMPARE(controller->document().objectById(objectId)->sourceText, QStringLiteral("XYZ!"));

    beginNativeEdit(canvas, controller, objectId);
    QTRY_VERIFY(canvas->isTextEditing());
    QDoubleSpinBox* sizeSpin = fontSize->spinBox();
    QTest::mouseClick(sizeSpin, Qt::LeftButton);
    sizeSpin->selectAll();
    QTest::keyClicks(sizeSpin, QStringLiteral("88"));
    QTest::keyClick(sizeSpin, Qt::Key_Enter);
    QTRY_COMPARE(controller->document().objectById(objectId)->typography.fontSize, 88.0);
    QVERIFY(!canvas->isTextEditing());
}

void EffectsPanelUiTests::styleIntensityGestureHasImmediateDirtyTruthAndOneUndoStep()
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();
    auto* controller = window.findChild<EditorController*>();
    auto* intensity = window.findChild<SliderSpinBox*>(QStringLiteral("styleIntensity"));
    QVERIFY(controller);
    QVERIFY(intensity);
    QSlider* slider = intensity->findChild<QSlider*>();
    QVERIFY(slider);

    const QString first = controller->createTextObject(QPointF(100, 100), QStringLiteral("gesture one"));
    const QString second = controller->createTextObject(QPointF(400, 100), QStringLiteral("gesture two"));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(controller->sceneGeometry().objectById(first)
                                 && controller->sceneGeometry().objectById(second), 5000);
    controller->selectObject(first);
    controller->undoStack()->setClean();
    const QString saved = test::semanticFingerprint(controller->document());
    const int savedIndex = controller->undoStack()->index();
    const int savedCount = controller->undoStack()->count();

    const QPoint start = sliderHandleCenter(slider);
    const QPoint away(slider->width() * 3 / 4, slider->height() / 2);
    QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(slider, away, 10);
    QVERIFY(controller->document().objectById(first)->effectStackStrength != 1.0);
    QVERIFY2(controller->isModified(), "held persistent gesture must be immediately dirty");
    QVERIFY2(!controller->undoStack()->isClean(),
             "QUndoStack clean state must remain the sole dirty-state authority");
    QCOMPARE(controller->undoStack()->count(), savedCount + 1);
    QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, away);
    QVERIFY(controller->isModified());
    QVERIFY(controller->undoStack()->canUndo());
    QCOMPARE(controller->undoStack()->count(), savedCount + 1);
    controller->undoStack()->undo();
    QVERIFY(controller->undoStack()->isClean());
    QCOMPARE(test::semanticFingerprint(controller->document()), saved);
    QCOMPARE(controller->undoStack()->index(), savedIndex);

    // Remove the intentional redo branch so the following no-op assertion
    // measures only the gesture under test, not earlier setup history.
    controller->undoStack()->clear();
    controller->undoStack()->setClean();

    // Returning to the exact slider start is a semantic no-op and must not
    // create a misleading dirty/undo entry.
    QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(slider, away, 10);
    slider->setValue(500);
    QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, start);
    QVERIFY(!controller->isModified());
    QVERIFY(controller->undoStack()->isClean());
    QCOMPARE(controller->undoStack()->index(), 0);
    QCOMPARE(controller->undoStack()->count(), 0);
    QVERIFY(!controller->undoStack()->canUndo());

    // Selection change is an explicit interruption boundary: it commits the
    // held transaction against the original object before authority moves.
    QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(slider, away, 10);
    QVERIFY(controller->isModified());
    controller->selectObject(second);
    QCOMPARE(controller->selectionModel()->activeObjectId(), second);
    slider->setValue(650);
    QCOMPARE(controller->document().objectById(second)->effectStackStrength, 1.3);
    QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, away);
    QCOMPARE(controller->undoStack()->count(), 2);
    controller->undoStack()->undo();
    QCOMPARE(controller->document().objectById(second)->effectStackStrength, 1.0);
    QVERIFY(controller->document().objectById(first)->effectStackStrength != 1.0);
    controller->undoStack()->undo();
    QCOMPARE(controller->document().objectById(first)->effectStackStrength, 1.0);

    controller->undoStack()->clear();
    controller->selectObject(first);
    controller->undoStack()->setClean();
    const QString multiStart = test::semanticFingerprint(controller->document());

    // Many values inside one physical press/release are one command, while a
    // second press receives a fresh token and therefore a separate command.
    QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, start);
    slider->setValue(575);
    slider->setValue(700);
    slider->setValue(825);
    QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, away);
    QCOMPARE(controller->document().objectById(first)->effectStackStrength, 1.65);
    QCOMPARE(controller->undoStack()->count(), 1);
    const QString firstGestureFinal = test::semanticFingerprint(controller->document());
    controller->undoStack()->undo();
    QCOMPARE(test::semanticFingerprint(controller->document()), multiStart);
    controller->undoStack()->redo();
    QCOMPARE(test::semanticFingerprint(controller->document()), firstGestureFinal);

    const QPoint current = sliderHandleCenter(slider);
    QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, current);
    slider->setValue(750);
    slider->setValue(625);
    QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, start);
    QCOMPARE(controller->document().objectById(first)->effectStackStrength, 1.25);
    QCOMPARE(controller->undoStack()->count(), 2);
    const QString secondGestureFinal = test::semanticFingerprint(controller->document());
    controller->undoStack()->undo();
    QCOMPARE(test::semanticFingerprint(controller->document()), firstGestureFinal);
    controller->undoStack()->redo();
    QCOMPARE(test::semanticFingerprint(controller->document()), secondGestureFinal);

    // A real save is a clean/merge boundary even if invoked while the widget
    // still holds the mouse interaction. The following gesture must not merge
    // backward across the clean index.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QPoint saveHandle = sliderHandleCenter(slider);
    QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, saveHandle);
    QVERIFY(slider->isSliderDown());
    slider->setValue(550);
    const qreal savedStrength =
        controller->document().objectById(first)->effectStackStrength;
    QString saveError;
    QVERIFY2(controller->saveProject(directory.filePath(QStringLiteral("gesture-clean.vtype")),
                                     &saveError),
             qPrintable(saveError));
    QVERIFY(slider->isSliderDown());
    QVERIFY(controller->undoStack()->isClean());
    const int cleanIndex = controller->undoStack()->index();
    const int cleanCount = controller->undoStack()->count();
    QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, saveHandle);

    const QPoint postSaveHandle = sliderHandleCenter(slider);
    QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, postSaveHandle);
    QVERIFY(slider->isSliderDown());
    slider->setValue(725);
    slider->setValue(775);
    QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, away);
    QCOMPARE(controller->undoStack()->count(), cleanCount + 1);
    QVERIFY(!controller->undoStack()->isClean());
    const QString afterCleanGesture = test::semanticFingerprint(controller->document());
    controller->undoStack()->undo();
    QCOMPARE(controller->document().objectById(first)->effectStackStrength, savedStrength);
    QCOMPARE(controller->undoStack()->index(), cleanIndex);
    QVERIFY(controller->undoStack()->isClean());
    controller->undoStack()->redo();
    QCOMPARE(test::semanticFingerprint(controller->document()), afterCleanGesture);
}

void EffectsPanelUiTests::rotatedMarqueeUsesInkAsNarrowPhase()
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();
    auto* controller = window.findChild<EditorController*>();
    auto* canvas = window.findChild<EditorCanvas*>();
    QVERIFY(controller);
    QVERIFY(canvas);
    const QString rotatedId = controller->createTextObject(QPointF(420, 260), QStringLiteral("IIII"));
    const QString otherId = controller->createTextObject(QPointF(760, 460), QStringLiteral("Other"));
    QTRY_VERIFY_WITH_TIMEOUT(controller->sceneGeometry().objectById(rotatedId)
                                 && controller->sceneGeometry().objectById(otherId), 5000);
    ObjectTransform transform = controller->document().objectById(rotatedId)->transform;
    transform.rotation = 45.0;
    transform.scale = QPointF(0.55, 1.8);
    controller->setObjectTransform(rotatedId, transform);
    QTRY_COMPARE_WITH_TIMEOUT(controller->sceneGeometry().objectById(rotatedId)->transform.rotation,
                              45.0, 5000);

    const SceneObjectGeometry* rotated = controller->sceneGeometry().objectById(rotatedId);
    QVERIFY(rotated);
    const QPainterPath ink = rotated->geometry.combinedPath();
    const QRectF aabb = rotated->frame.pageAabb();
    QVERIFY(!ink.isEmpty());
    QVector<QRectF> cornerCandidates = {
        QRectF(aabb.topLeft(), QSizeF(8, 8)),
        QRectF(aabb.topRight() - QPointF(8, 0), QSizeF(8, 8)),
        QRectF(aabb.bottomLeft() - QPointF(0, 8), QSizeF(8, 8)),
        QRectF(aabb.bottomRight() - QPointF(8, 8), QSizeF(8, 8))};
    QRectF emptyCorner;
    for (const QRectF& candidate : cornerCandidates) {
        QPainterPath candidatePath;
        candidatePath.addRect(candidate);
        if (!candidatePath.intersects(ink)) {
            emptyCorner = candidate;
            break;
        }
    }
    QVERIFY2(!emptyCorner.isEmpty(), "rotated fixture needs an empty page-AABB corner");

    controller->clearSelection();
    const QPoint emptyStart = canvas->mapDocumentToViewport(emptyCorner.topLeft()).toPoint();
    const QPoint emptyEnd = canvas->mapDocumentToViewport(emptyCorner.bottomRight()).toPoint();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, emptyStart);
    QTest::mouseMove(canvas, emptyEnd, 10);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, emptyEnd);
    QVERIFY2(!controller->selectedObjectIds().contains(rotatedId),
             "marquee touching only an empty AABB corner selected rotated text");

    const QRectF containing = aabb.adjusted(-12, -12, 12, 12);
    const QPoint containStart = canvas->mapDocumentToViewport(containing.topLeft()).toPoint();
    const QPoint containEnd = canvas->mapDocumentToViewport(containing.bottomRight()).toPoint();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, containStart);
    QTest::mouseMove(canvas, containEnd, 10);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, containEnd);
    QVERIFY(controller->selectedObjectIds().contains(rotatedId));

    controller->selectObject(otherId);
    controller->selectObjectsInRect(containing, true);
    QVERIFY(controller->selectedObjectIds().contains(otherId));
    QVERIFY(controller->selectedObjectIds().contains(rotatedId));

    transform.scale.setX(-0.55);
    controller->setObjectTransform(rotatedId, transform);
    QTRY_COMPARE_WITH_TIMEOUT(controller->sceneGeometry().objectById(rotatedId)->transform.scale.x(),
                              -0.55, 5000);
    controller->selectObjectsInRect(
        controller->sceneGeometry().objectById(rotatedId)->frame.pageAabb().adjusted(-2, -2, 2, 2), false);
    QVERIFY(controller->selectedObjectIds().contains(rotatedId));

    controller->setActiveLayerLocked(true);
    QTRY_VERIFY_WITH_TIMEOUT(controller->sceneGeometry().objectById(rotatedId)->locked, 5000);
    controller->selectObjectsInRect(aabb.adjusted(-20, -20, 20, 20), false);
    QVERIFY(!controller->selectedObjectIds().contains(rotatedId));
    controller->setActiveLayerLocked(false);
    controller->setActiveLayerVisible(false);
    QTRY_VERIFY_WITH_TIMEOUT(controller->sceneGeometry().objectById(rotatedId) == nullptr, 5000);
    controller->selectObjectsInRect(aabb.adjusted(-20, -20, 20, 20), false);
    QVERIFY(!controller->selectedObjectIds().contains(rotatedId));
}

void EffectsPanelUiTests::objectRowLayerButtonsOperateOnParentLayer()
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();
    auto* controller = window.findChild<EditorController*>();
    auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("layersTree"));
    auto* visible = window.findChild<QPushButton*>(QStringLiteral("layerVisibleButton"));
    auto* lock = window.findChild<QPushButton*>(QStringLiteral("layerLockButton"));
    QVERIFY(controller);
    QVERIFY(tree);
    QVERIFY(visible);
    QVERIFY(lock);
    const QString rowObjectId =
        controller->createTextObject(QPointF(100, 100), QStringLiteral("row metadata"));
    QVERIFY(!rowObjectId.isEmpty());
    QCoreApplication::processEvents();
    QVERIFY(tree->topLevelItemCount() > 0);
    QTreeWidgetItem* layerItem = tree->topLevelItem(0);
    QVERIFY(layerItem && layerItem->childCount() > 0);
    QTreeWidgetItem* objectItem = layerItem->child(0);
    const QString layerId = layerItem->data(0, Qt::UserRole).toString();
    QVERIFY(!layerId.isEmpty());

    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tree->visualItemRect(layerItem).center());
    QTRY_COMPARE(controller->document().activeLayerId, layerId);
    QTest::mouseClick(visible, Qt::LeftButton);
    QTRY_VERIFY(!controller->document().layerById(layerId)->visible);
    QTest::mouseClick(visible, Qt::LeftButton);
    QTRY_VERIFY(controller->document().layerById(layerId)->visible);

    layerItem = tree->topLevelItem(0);
    objectItem = layerItem->child(0);
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tree->visualItemRect(objectItem).center());
    QCOMPARE(controller->selectionModel()->activeObjectId(), rowObjectId);
    QTest::mouseClick(visible, Qt::LeftButton);
    QTRY_VERIFY(!controller->document().layerById(layerId)->visible);
    QTest::mouseClick(visible, Qt::LeftButton);
    QTRY_VERIFY(controller->document().layerById(layerId)->visible);

    layerItem = tree->topLevelItem(0);
    objectItem = layerItem->child(0);
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tree->visualItemRect(objectItem).center());
    QTest::mouseClick(lock, Qt::LeftButton);
    QTRY_VERIFY(controller->document().layerById(layerId)->locked);
    QTest::mouseClick(lock, Qt::LeftButton);
    QTRY_VERIFY(!controller->document().layerById(layerId)->locked);
}

void EffectsPanelUiTests::unsupportedEffectDisablesMaskUiAcrossRefreshes_data()
{
    QTest::addColumn<QString>("unsupportedTypeId");
    for (const EffectDescriptor& descriptor : EffectRegistry::instance().descriptors()) {
        if (!descriptor.supportsMask) {
            QTest::newRow(descriptor.typeId.toUtf8().constData()) << descriptor.typeId;
        }
    }
}

void EffectsPanelUiTests::unsupportedEffectDisablesMaskUiAcrossRefreshes()
{
    QFETCH(QString, unsupportedTypeId);
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();
    auto* controller = window.findChild<EditorController*>();
    auto* canvas = window.findChild<EditorCanvas*>();
    auto* effectList = window.findChild<QListWidget*>(QStringLiteral("effectList"));
    auto* maskButton = window.findChild<QToolButton*>(QStringLiteral("tool/effect-mask"));
    auto* maskAction = window.findChild<QAction*>(QStringLiteral("tool.effectMask"));
    QVERIFY(controller);
    QVERIFY(canvas);
    QVERIFY(effectList);
    QVERIFY(maskButton);
    QVERIFY(maskAction);

    const QString objectId = controller->createTextObject(QPointF(100, 100), QStringLiteral("Mask capability"));
    controller->addEffect(unsupportedTypeId);
    const TextObject* object = controller->document().objectById(objectId);
    QVERIFY(object);
    QCOMPARE(object->effects.size(), 1);
    const Effect* unsupportedEffect = object->effects.at(0);
    QVERIFY(unsupportedEffect);
    QCOMPARE(unsupportedEffect->typeId(), unsupportedTypeId);
    const QString unsupportedId = unsupportedEffect->instanceId;
    QCoreApplication::processEvents();
    QVERIFY(!canvas->maskEnabled());
    QVERIFY(canvas->maskEffectId().isEmpty());
    QVERIFY(!maskButton->isEnabled());
    QVERIFY(!maskAction->isEnabled());
    QTest::mouseClick(maskButton, Qt::LeftButton);
    maskAction->trigger();
    QCOMPARE(static_cast<int>(controller->tool()), static_cast<int>(EditorTool::Select));

    controller->addEffect(QStringLiteral("wave"));
    object = controller->document().objectById(objectId);
    QVERIFY(object);
    QCOMPARE(object->effects.size(), 2);
    const QString waveId = object->effects.at(1)->instanceId;
    QTRY_COMPARE(controller->selectedEffectId(), waveId);
    QTRY_VERIFY(canvas->maskEnabled());
    QCOMPARE(canvas->maskEffectId(), waveId);
    QVERIFY(maskButton->isEnabled());
    QVERIFY(maskAction->isEnabled());

    QTest::mouseClick(maskButton, Qt::LeftButton);
    QTRY_COMPARE(static_cast<int>(controller->tool()), static_cast<int>(EditorTool::EffectMask));

    int unsupportedRow = -1;
    for (int row = 0; row < effectList->count(); ++row) {
        if (effectList->item(row)->data(Qt::UserRole).toString() == unsupportedId) {
            unsupportedRow = row;
            break;
        }
    }
    QVERIFY(unsupportedRow >= 0);
    QTest::mouseClick(effectList->viewport(), Qt::LeftButton, Qt::NoModifier,
                      effectList->visualItemRect(effectList->item(unsupportedRow)).center());
    QTRY_COMPARE(controller->selectedEffectId(), unsupportedId);
    QTRY_COMPARE(static_cast<int>(controller->tool()), static_cast<int>(EditorTool::Select));
    QVERIFY(!canvas->maskEnabled());
    QVERIFY(canvas->maskEffectId().isEmpty());
    QVERIFY(!maskButton->isEnabled());
    QVERIFY(!maskAction->isEnabled());

    // A document refresh and its later asynchronous scene publication used to
    // re-enable masking merely because an effect ID remained selected.
    QSignalSpy sceneSpy(controller, &EditorController::sceneChanged);
    controller->setText(QStringLiteral("Mask capability after refresh"));
    QTRY_COMPARE(controller->document().objectById(objectId)->sourceText,
                 QStringLiteral("Mask capability after refresh"));
    QTRY_VERIFY_WITH_TIMEOUT(sceneSpy.count() > 0, 5000);
    QVERIFY(!canvas->maskEnabled());
    QVERIFY(canvas->maskEffectId().isEmpty());
    QVERIFY(!maskButton->isEnabled());
    QVERIFY(!maskAction->isEnabled());

    EffectMaskStroke stroke;
    stroke.points = {QPointF(100, 100), QPointF(120, 110)};
    controller->addEffectMaskStroke(objectId, unsupportedId, stroke);
    QVERIFY(controller->document().objectById(objectId)
                ->effects.byInstanceId(unsupportedId)->maskStrokes.isEmpty());
    const QJsonArray serializedEffects = ProjectSerializer::textObjectToJson(
        *controller->document().objectById(objectId)).value(QStringLiteral("effects")).toArray();
    bool foundSerializedEffect = false;
    for (const QJsonValue& value : serializedEffects) {
        const QJsonObject serializedEffect = value.toObject();
        if (serializedEffect.value(QStringLiteral("id")).toString() == unsupportedId) {
            foundSerializedEffect = true;
            QVERIFY(serializedEffect.value(QStringLiteral("mask")).toArray().isEmpty());
        }
    }
    QVERIFY(foundSerializedEffect);
    Document restored;
    QString error;
    QVERIFY2(ProjectSerializer::fromJson(ProjectSerializer::toJson(controller->document()),
                                         &restored, &error),
             qPrintable(error));
    QVERIFY(restored.objectById(objectId)
                ->effects.byInstanceId(unsupportedId)->maskStrokes.isEmpty());
}

void EffectsPanelUiTests::traitModeIsShownAfterBoldAndItalic()
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();
    auto* controller = window.findChild<EditorController*>();
    auto* typography = window.findChild<TypographyPanel*>();
    QVERIFY(controller);
    QVERIFY(typography);

    const QString objectId = controller->createTextObject(QPointF(100.0, 100.0), QStringLiteral("Traits"));
    const TextObject* object = controller->document().objectById(objectId);
    QVERIFY(object);
    const QString style = controller->fontStyles(object->font.family).value(0);
    if (style.isEmpty()) {
        QSKIP("No named font styles are available in this test environment.");
    }
    auto* styleCombo = typography->findChild<QComboBox*>(QStringLiteral("fontStyle"));
    auto* bold = typography->findChild<QPushButton*>(QStringLiteral("fontBold"));
    auto* italic = typography->findChild<QPushButton*>(QStringLiteral("fontItalic"));
    QVERIFY(styleCombo);
    QVERIFY(bold);
    QVERIFY(italic);

    const auto assertTraitMode = [&] {
        const TextObject* current = controller->document().objectById(objectId);
        QVERIFY(current);
        QVERIFY(current->font.styleName.isEmpty());
        QCOMPARE(styleCombo->currentText(), QStringLiteral("Auto / Traits"));
    };
    const auto restoreExactStyle = [&] {
        controller->setFontStyle(style);
        QTRY_COMPARE(controller->document().objectById(objectId)->font.styleName, style);
        QTRY_COMPARE(styleCombo->currentText(), style);
    };

    restoreExactStyle();
    QTest::mouseClick(bold, Qt::LeftButton);
    QTest::mouseClick(italic, Qt::LeftButton);
    assertTraitMode();

    restoreExactStyle();
    QTest::mouseClick(italic, Qt::LeftButton);
    QTest::mouseClick(bold, Qt::LeftButton);
    assertTraitMode();
}

void EffectsPanelUiTests::pathTypographyControlsAndAnchorGesture()
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();
    auto* controller = window.findChild<EditorController*>();
    auto* canvas = window.findChild<EditorCanvas*>();
    auto* typography = window.findChild<TypographyPanel*>();
    auto* createPath = window.findChild<QPushButton*>(QStringLiteral("createTextPath"));
    auto* pathEnabled = window.findChild<QCheckBox*>(QStringLiteral("textOnPathEnabled"));
    auto* pathTool = window.findChild<QToolButton*>(QStringLiteral("tool/path-edit"));
    auto* startOffset = window.findChild<SliderSpinBox*>(QStringLiteral("pathStartOffset"));
    QVERIFY(controller);
    QVERIFY(canvas);
    QVERIFY(typography);
    QVERIFY(createPath);
    QVERIFY(pathEnabled);
    QVERIFY(pathTool);
    QVERIFY(startOffset);

    const QString objectId = controller->createTextObject(
        QPointF(180.0, 180.0), QStringLiteral("Path UI"));
    QVERIFY(!objectId.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(createPath->isEnabled(), 5000);
    QTest::mouseClick(createPath, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(controller->document().objectById(objectId)->path.has_value(), 5000);
    QVERIFY(controller->document().objectById(objectId)->pathLayout.enabled);
    QTRY_VERIFY_WITH_TIMEOUT(pathTool->isEnabled(), 5000);
    QTest::mouseClick(pathTool, Qt::LeftButton);
    QTRY_COMPARE(static_cast<int>(controller->tool()), static_cast<int>(EditorTool::PathEdit));
    QTRY_VERIFY_WITH_TIMEOUT(
        controller->sceneGeometry().spatialRevision == controller->spatialRevision(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->pathEditorObjectId() == objectId, 5000);

    const TextObject* object = controller->document().objectById(objectId);
    const SceneObjectGeometry* sceneObject = controller->sceneGeometry().objectById(objectId);
    QVERIFY(object && object->path.has_value() && sceneObject);
    const QPointF oldAnchor = object->path->nodes.front().anchor;
    const QPointF expectedAnchorPage = sceneObject->frame.localPointToPage(oldAnchor);
    const QPointF overlayAnchorPage = canvas->pathEditorAnchorPage(0);
    QVERIFY2(QLineF(expectedAnchorPage, overlayAnchorPage).length() < 1.0e-6,
             qPrintable(QStringLiteral("path overlay frame diverged by %1")
                            .arg(QLineF(expectedAnchorPage, overlayAnchorPage).length())));
    const QPoint press = canvas->mapDocumentToViewport(
        expectedAnchorPage).toPoint();
    const int hitNode = canvas->pathEditorHitNodeAtViewport(QPointF(press));
    QVERIFY2(hitNode == 0,
             qPrintable(QStringLiteral("path hit-test selected node %1 at zoom %2")
                            .arg(hitNode)
                            .arg(canvas->zoom())));
    QSignalSpy pathCommit(canvas, &EditorCanvas::pathGeometryCommitted);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, press);
    QTest::mouseMove(canvas, press + QPoint(20, 12), 20);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, press + QPoint(20, 12));
    QTRY_VERIFY_WITH_TIMEOUT(pathCommit.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        controller->document().objectById(objectId)->path->nodes.front().anchor != oldAnchor,
        5000);

    startOffset->spinBox()->setValue(18.0);
    QTRY_VERIFY_WITH_TIMEOUT(
        std::abs(controller->document().objectById(objectId)->pathLayout.startOffset - 18.0) < 0.01,
        5000);
    QTest::mouseClick(pathEnabled, Qt::LeftButton);
    QTRY_VERIFY(!controller->document().objectById(objectId)->pathLayout.enabled);
    QVERIFY(controller->document().objectById(objectId)->path.has_value());
    controller->undoStack()->undo();
    QTRY_VERIFY(controller->document().objectById(objectId)->pathLayout.enabled);
}

void EffectsPanelUiTests::scaleControlsPreserveSmallAndMirroredValues()
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();
    auto* controller = window.findChild<EditorController*>();
    auto* transform = window.findChild<TransformPanel*>();
    QVERIFY(controller);
    QVERIFY(transform);
    const QString objectId = controller->createTextObject(QPointF(100.0, 100.0), QStringLiteral("Scale"));
    auto* scaleX = transform->findChild<QDoubleSpinBox*>(QStringLiteral("transformScaleX"));
    QVERIFY(scaleX);

    const QVector<qreal> inputs = {1.0, 0.1, 0.01, 0.001, 0.0, -0.001, -1.0};
    for (const qreal input : inputs) {
        scaleX->setValue(input);
        const qreal expected = qAbs(input) < ObjectTransform::MinimumScale
            ? ObjectTransform::MinimumScale : input;
        QTRY_VERIFY(std::abs(controller->document().objectById(objectId)->transform.scale.x() - expected) < 1.0e-8);
        QVERIFY(std::abs(scaleX->value() - expected) < 1.0e-8);

        Document restored;
        QString error;
        QVERIFY2(ProjectSerializer::fromJson(ProjectSerializer::toJson(controller->document()),
                                              &restored, &error), qPrintable(error));
        const TextObject* reloaded = restored.objectById(objectId);
        QVERIFY(reloaded);
        QVERIFY(std::abs(reloaded->transform.scale.x() - expected) < 1.0e-8);
    }
}

QTEST_MAIN(EffectsPanelUiTests)
#include "ui_regression_tests.moc"
