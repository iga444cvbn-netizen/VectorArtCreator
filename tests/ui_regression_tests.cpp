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
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QPainterPath>
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
    void pathTypographySlidersAreTransactionalPhysicalGestures();
    void pathCubicInsertionSplitsCurveAndPreservesIdentity();
    void pathNodeLineCubicConversionIsReversible();
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

QPoint sliderHandleCenterAt(const QSlider* slider, int value)
{
    QStyleOptionSlider option;
    option.initFrom(slider);
    option.orientation = slider->orientation();
    option.minimum = slider->minimum();
    option.maximum = slider->maximum();
    option.sliderPosition = value;
    option.sliderValue = value;
    option.singleStep = slider->singleStep();
    option.pageStep = slider->pageStep();
    option.tickPosition = slider->tickPosition();
    option.tickInterval = slider->tickInterval();
    option.upsideDown = slider->invertedAppearance();
    return slider->style()
        ->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, slider)
        .center();
}

void dragSliderPhysically(QSlider* slider, const QPoint& target)
{
    const QPoint start = sliderHandleCenter(slider);
    QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, start);
    for (int step = 1; step <= 8; ++step) {
        const qreal fraction = static_cast<qreal>(step) / 8.0;
        const QPoint point = (QPointF(start)
                              + (QPointF(target) - QPointF(start)) * fraction).toPoint();
        QTest::mouseMove(slider, point, 10);
    }
    QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, target);
}

QPointF cubicPointForTest(const QPointF& p0,
                          const QPointF& p1,
                          const QPointF& p2,
                          const QPointF& p3,
                          qreal t)
{
    const qreal oneMinusT = 1.0 - t;
    return p0 * (oneMinusT * oneMinusT * oneMinusT)
        + p1 * (3.0 * oneMinusT * oneMinusT * t)
        + p2 * (3.0 * oneMinusT * t * t)
        + p3 * (t * t * t);
}

PathGeometry curvedPathForTest()
{
    PathGeometry path;
    path.id = QStringLiteral("ui-cubic-path");
    PathNode first;
    first.id = QStringLiteral("ui-cubic-first");
    first.anchor = QPointF(0.0, 0.0);
    first.hasOutgoingHandle = true;
    first.outgoingHandle = QPointF(0.0, 300.0);
    PathNode second;
    second.id = QStringLiteral("ui-cubic-second");
    second.anchor = QPointF(300.0, 0.0);
    second.hasIncomingHandle = true;
    second.incomingHandle = QPointF(300.0, 300.0);
    path.nodes = {first, second};
    return path;
}

qreal splitCurveError(const QPainterPath& original, const QPainterPath& candidate)
{
    qreal maximum = 0.0;
    for (int index = 0; index <= 100; ++index) {
        const qreal t = static_cast<qreal>(index) / 100.0;
        maximum = qMax(maximum,
                       QLineF(original.pointAtPercent(t),
                              candidate.pointAtPercent(t)).length());
    }
    return maximum;
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
    second…7900 tokens truncated…   QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier,
                        stalePress + QPoint(40, 24));
    QCOMPARE(stalePathCommit.count(), 0);
    QCOMPARE(*controller->document().objectById(objectId)->path, pathBeforeStaleGesture);

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

void EffectsPanelUiTests::pathTypographySlidersAreTransactionalPhysicalGestures()
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();
    auto* controller = window.findChild<EditorController*>();
    auto* createPath = window.findChild<QPushButton*>(QStringLiteral("createTextPath"));
    auto* startOffset = window.findChild<SliderSpinBox*>(QStringLiteral("pathStartOffset"));
    auto* baselineOffset = window.findChild<SliderSpinBox*>(QStringLiteral("pathBaselineOffset"));
    QVERIFY(controller);
    QVERIFY(createPath);
    QVERIFY(startOffset);
    QVERIFY(baselineOffset);
    QSlider* startSlider = startOffset->findChild<QSlider*>();
    QSlider* baselineSlider = baselineOffset->findChild<QSlider*>();
    QVERIFY(startSlider);
    QVERIFY(baselineSlider);

    const QString first = controller->createTextObject(
        QPointF(160.0, 180.0), QStringLiteral("physical path slider one"));
    QVERIFY(!first.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(createPath->isEnabled(), 5000);
    QTest::mouseClick(createPath, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(controller->document().objectById(first)->path.has_value(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(controller->document().objectById(first)->pathLayout.enabled, 5000);

    const QString second = controller->createTextObject(
        QPointF(500.0, 180.0), QStringLiteral("physical path slider two"));
    QVERIFY(!second.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(createPath->isEnabled(), 5000);
    QTest::mouseClick(createPath, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(controller->document().objectById(second)->path.has_value(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(controller->document().objectById(second)->pathLayout.enabled, 5000);
    controller->selectObject(first);
    QTRY_VERIFY_WITH_TIMEOUT(controller->activeObject()->id == first, 5000);

    controller->undoStack()->clear();
    controller->undoStack()->setClean();
    const QString initial = test::semanticFingerprint(controller->document());
    const int initialCount = controller->undoStack()->count();

    const QPoint firstTarget = sliderHandleCenterAt(startSlider, 760);
    dragSliderPhysically(startSlider, firstTarget);
    QVERIFY(std::abs(controller->document().objectById(first)
                         ->pathLayout.startOffset) > 1.0);
    QVERIFY(controller->isModified());
    QCOMPARE(controller->undoStack()->count(), initialCount + 1);
    const QString firstFinal = test::semanticFingerprint(controller->document());
    controller->undoStack()->undo();
    QCOMPARE(test::semanticFingerprint(controller->document()), initial);
    controller->undoStack()->redo();
    QCOMPARE(test::semanticFingerprint(controller->document()), firstFinal);

    // A physical return to the exact press value must obsolete the gesture,
    // not leave a zero-effect undo command behind.
    controller->undoStack()->clear();
    controller->undoStack()->setClean();
    const QString returnStartFingerprint = test::semanticFingerprint(controller->document());
    const QPoint returnStart = sliderHandleCenter(startSlider);
    const QPoint returnAway = sliderHandleCenterAt(startSlider, 190);
    QTest::mousePress(startSlider, Qt::LeftButton, Qt::NoModifier, returnStart);
    QTest::mouseMove(startSlider, returnAway, 10);
    QTest::mouseMove(startSlider, returnStart, 10);
    QTest::mouseRelease(startSlider, Qt::LeftButton, Qt::NoModifier, returnStart);
    QCOMPARE(test::semanticFingerprint(controller->document()), returnStartFingerprint);
    QVERIFY(controller->undoStack()->isClean());
    QCOMPARE(controller->undoStack()->count(), 0);

    // Two independent press/release gestures remain two independent undo
    // entries, with exact semantic restoration at both boundaries.
    controller->undoStack()->clear();
    controller->undoStack()->setClean();
    const QString twoGestureStart = test::semanticFingerprint(controller->document());
    dragSliderPhysically(startSlider, sliderHandleCenterAt(startSlider, 280));
    const QString afterFirstGesture = test::semanticFingerprint(controller->document());
    dragSliderPhysically(startSlider, sliderHandleCenterAt(startSlider, 840));
    const QString afterSecondGesture = test::semanticFingerprint(controller->document());
    QCOMPARE(controller->undoStack()->count(), 2);
    controller->undoStack()->undo();
    QCOMPARE(test::semanticFingerprint(controller->document()), afterFirstGesture);
    controller->undoStack()->undo();
    QCOMPARE(test::semanticFingerprint(controller->document()), twoGestureStart);
    controller->undoStack()->redo();
    controller->undoStack()->redo();
    QCOMPARE(test::semanticFingerprint(controller->document()), afterSecondGesture);

    // Baseline has the same physical gesture contract and its own merge
    // identity; it must not merge with a start-offset drag.
    controller->undoStack()->clear();
    controller->undoStack()->setClean();
    const QString baselineStart = test::semanticFingerprint(controller->document());
    dragSliderPhysically(baselineSlider, sliderHandleCenterAt(baselineSlider, 670));
    const QString baselineFinal = test::semanticFingerprint(controller->document());
    QCOMPARE(controller->undoStack()->count(), 1);
    controller->undoStack()->undo();
    QCOMPARE(test::semanticFingerprint(controller->document()), baselineStart);
    controller->undoStack()->redo();
    QCOMPARE(test::semanticFingerprint(controller->document()), baselineFinal);

    // Selection changes terminate the first transaction before authority is
    // moved. A held physical slider then creates a separate second-object
    // edit instead of merging or retargeting the first command.
    controller->undoStack()->clear();
    controller->selectObject(first);
    controller->undoStack()->setClean();
    const QPoint heldStart = sliderHandleCenter(startSlider);
    const QPoint heldFirstTarget = sliderHandleCenterAt(startSlider, 430);
    QTest::mousePress(startSlider, Qt::LeftButton, Qt::NoModifier, heldStart);
    QTest::mouseMove(startSlider, heldFirstTarget, 10);
    const qreal firstHeldValue = controller->document().objectById(first)
        ->pathLayout.startOffset;
    const qreal secondBefore = controller->document().objectById(second)
        ->pathLayout.startOffset;
    controller->selectObject(second);
    QTRY_COMPARE(controller->selectionModel()->activeObjectId(), second);
    QTest::mouseMove(startSlider, sliderHandleCenterAt(startSlider, 620), 10);
    QTest::mouseRelease(startSlider, Qt::LeftButton, Qt::NoModifier,
                        sliderHandleCenterAt(startSlider, 620));
    QCOMPARE(controller->document().objectById(first)->pathLayout.startOffset,
             firstHeldValue);
    QVERIFY(controller->document().objectById(second)->pathLayout.startOffset != secondBefore);
    QCOMPARE(controller->undoStack()->count(), 2);

    // Saving while a physical interaction is still held establishes a clean
    // index. A later move cannot merge backward across that boundary.
    controller->undoStack()->clear();
    controller->selectObject(first);
    controller->undoStack()->setClean();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QPoint saveStart = sliderHandleCenter(startSlider);
    QTest::mousePress(startSlider, Qt::LeftButton, Qt::NoModifier, saveStart);
    QTest::mouseMove(startSlider, sliderHandleCenterAt(startSlider, 520), 10);
    const qreal savedValue = controller->document().objectById(first)->pathLayout.startOffset;
    QString saveError;
    QVERIFY2(controller->saveProject(directory.filePath(QStringLiteral("path-slider.vtype")),
                                     &saveError),
             qPrintable(saveError));
    QVERIFY(startSlider->isSliderDown());
    QVERIFY(controller->undoStack()->isClean());
    const int cleanIndex = controller->undoStack()->index();
    const int cleanCount = controller->undoStack()->count();
    QTest::mouseMove(startSlider, sliderHandleCenterAt(startSlider, 730), 10);
    QTest::mouseRelease(startSlider, Qt::LeftButton, Qt::NoModifier,
                        sliderHandleCenterAt(startSlider, 730));
    QCOMPARE(controller->undoStack()->count(), cleanCount + 1);
    QVERIFY(!controller->undoStack()->isClean());
    controller->undoStack()->undo();
    QCOMPARE(controller->document().objectById(first)->pathLayout.startOffset, savedValue);
    QCOMPARE(controller->undoStack()->index(), cleanIndex);
    QVERIFY(controller->undoStack()->isClean());
}

void EffectsPanelUiTests::pathCubicInsertionSplitsCurveAndPreservesIdentity()
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();
    auto* controller = window.findChild<EditorController*>();
    auto* canvas = window.findChild<EditorCanvas*>();
    auto* createPath = window.findChild<QPushButton*>(QStringLiteral("createTextPath"));
    auto* pathTool = window.findChild<QToolButton*>(QStringLiteral("tool/path-edit"));
    QVERIFY(controller);
    QVERIFY(canvas);
    QVERIFY(createPath);
    QVERIFY(pathTool);

    const QString objectId = controller->createTextObject(
        QPointF(220.0, 160.0), QStringLiteral("cubic insertion"));
    QVERIFY(!objectId.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(createPath->isEnabled(), 5000);
    QTest::mouseClick(createPath, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(controller->document().objectById(objectId)->path.has_value(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(pathTool->isEnabled(), 5000);

    PathGeometry curved = curvedPathForTest();
    const TextObject* originalObject = controller->document().objectById(objectId);
    QVERIFY(originalObject && originalObject->path.has_value());
    curved.id = originalObject->path->id;
    controller->setPathGeometry(objectId, curved, controller->spatialRevision());
    ObjectTransform transform = originalObject->transform;
    transform.rotation = 23.0;
    transform.scale = QPointF(-1.25, 0.8);
    transform.pivotLocal = QPointF(140.0, 0.0);
    transform.hasPivot = true;
    controller->setObjectTransform(objectId, transform);
    QTRY_VERIFY_WITH_TIMEOUT(
        controller->sceneGeometry().spatialRevision == controller->spatialRevision(), 5000);
    QTest::mouseClick(pathTool, Qt::LeftButton);
    QTRY_COMPARE(static_cast<int>(controller->tool()), static_cast<int>(EditorTool::PathEdit));

    controller->undoStack()->clear();
    controller->undoStack()->setClean();
    const SceneObjectGeometry* sceneObject = controller->sceneGeometry().objectById(objectId);
    QVERIFY(sceneObject);
    const QPainterPath originalPainterPath = curved.toPainterPath();
    const QPointF splitLocal = cubicPointForTest(
        curved.nodes.at(0).anchor, curved.nodes.at(0).outgoingHandle,
        curved.nodes.at(1).incomingHandle, curved.nodes.at(1).anchor, 0.5);
    const QPoint splitPage = canvas->mapDocumentToViewport(
        sceneObject->frame.localPointToPage(splitLocal)).toPoint();

    PathGeometry committed;
    int commitCount = 0;
    QObject::connect(canvas, &EditorCanvas::pathGeometryCommitted,
                     canvas, [&](const QString& id, const PathGeometry& path, quint64) {
        if (id == objectId) {
            committed = path;
            ++commitCount;
        }
    });
    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, splitPage, 20);
    QTRY_VERIFY_WITH_TIMEOUT(commitCount > 0, 5000);
    QCOMPARE(committed.nodes.size(), 3);
    QCOMPARE(committed.nodes.at(0).id, curved.nodes.at(0).id);
    QCOMPARE(committed.nodes.at(2).id, curved.nodes.at(1).id);
    QVERIFY(committed.nodes.at(1).id != curved.nodes.at(0).id);
    QVERIFY(committed.nodes.at(1).id != curved.nodes.at(1).id);
    QVERIFY(committed.nodes.at(1).hasIncomingHandle);
    QVERIFY(committed.nodes.at(1).hasOutgoingHandle);
    QVERIFY(committed.nodes.at(0).hasOutgoingHandle);
    QVERIFY(committed.nodes.at(2).hasIncomingHandle);
    QVERIFY(QLineF(committed.nodes.at(1).anchor, splitLocal).length() < 4.0);
    QVERIFY2(splitCurveError(originalPainterPath, committed.toPainterPath()) < 1.5,
             "inserting on a cubic must preserve the original curve geometry");

    const PathGeometry inserted = committed;
    controller->undoStack()->undo();
    QCOMPARE(*controller->document().objectById(objectId)->path, curved);
    controller->undoStack()->redo();
    QCOMPARE(*controller->document().objectById(objectId)->path, inserted);
}

void EffectsPanelUiTests::pathNodeLineCubicConversionIsReversible()
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QCoreApplication::processEvents();
    auto* controller = window.findChild<EditorController*>();
    auto* canvas = window.findChild<EditorCanvas*>();
    auto* createPath = window.findChild<QPushButton*>(QStringLiteral("createTextPath"));
    auto* pathTool = window.findChild<QToolButton*>(QStringLiteral("tool/path-edit"));
    QVERIFY(controller);
    QVERIFY(canvas);
    QVERIFY(createPath);
    QVERIFY(pathTool);

    const QString objectId = controller->createTextObject(
        QPointF(240.0, 170.0), QStringLiteral("line cubic conversion"));
    QVERIFY(!objectId.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(createPath->isEnabled(), 5000);
    QTest::mouseClick(createPath, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(controller->document().objectById(objectId)->path.has_value(), 5000);
    PathGeometry line = *controller->document().objectById(objectId)->path;
    line.nodes.front().anchor = QPointF(0.0, 0.0);
    line.nodes.back().anchor = QPointF(240.0, 80.0);
    line.nodes.front().hasIncomingHandle = false;
    line.nodes.front().hasOutgoingHandle = false;
    line.nodes.back().hasIncomingHandle = false;
    line.nodes.back().hasOutgoingHandle = false;
    controller->setPathGeometry(objectId, line, controller->spatialRevision());
    QTRY_VERIFY_WITH_TIMEOUT(
        controller->sceneGeometry().spatialRevision == controller->spatialRevision(), 5000);
    QTest::mouseClick(pathTool, Qt::LeftButton);
    controller->undoStack()->clear();
    controller->undoStack()->setClean();

    const SceneObjectGeometry* sceneObject = controller->sceneGeometry().objectById(objectId);
    QVERIFY(sceneObject);
    const QPoint firstAnchor = canvas->mapDocumentToViewport(
        sceneObject->frame.localPointToPage(line.nodes.front().anchor)).toPoint();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, firstAnchor);
    QSignalSpy commitSpy(canvas, &EditorCanvas::pathGeometryCommitted);
    QTest::keyClick(canvas, Qt::Key_C);
    QTRY_VERIFY_WITH_TIMEOUT(commitSpy.count() > 0, 5000);
    const PathGeometry cubic = *controller->document().objectById(objectId)->path;
    QCOMPARE(cubic.nodes.size(), line.nodes.size());
    QCOMPARE(cubic.nodes.at(0).id, line.nodes.at(0).id);
    QCOMPARE(cubic.nodes.at(1).id, line.nodes.at(1).id);
    QVERIFY(cubic.isCubicSegment(0));
    QVERIFY(cubic.nodes.at(0).hasOutgoingHandle);
    QVERIFY(cubic.nodes.at(1).hasIncomingHandle);
    QVERIFY(!cubic.nodes.at(0).hasIncomingHandle);
    QVERIFY(!cubic.nodes.at(1).hasOutgoingHandle);
    QVERIFY(QLineF(cubic.nodes.at(0).outgoingHandle,
                   cubic.nodes.at(0).anchor
                       + (cubic.nodes.at(1).anchor - cubic.nodes.at(0).anchor) / 3.0)
                .length() < 1.0);
    QVERIFY(QLineF(cubic.nodes.at(1).incomingHandle,
                   cubic.nodes.at(1).anchor
                       - (cubic.nodes.at(1).anchor - cubic.nodes.at(0).anchor) / 3.0)
                .length() < 1.0);

    const PathGeometry cubicSnapshot = cubic;
    controller->undoStack()->undo();
    QCOMPARE(*controller->document().objectById(objectId)->path, line);
    controller->undoStack()->redo();
    QCOMPARE(*controller->document().objectById(objectId)->path, cubicSnapshot);

    QTest::keyClick(canvas, Qt::Key_L);
    QTRY_VERIFY_WITH_TIMEOUT(commitSpy.count() > 1, 5000);
    const PathGeometry restoredLine = *controller->document().objectById(objectId)->path;
    QCOMPARE(restoredLine, line);
    QVERIFY(!restoredLine.isCubicSegment(0));
    QCOMPARE(restoredLine.nodes.at(0).id, line.nodes.at(0).id);
    QCOMPARE(restoredLine.nodes.at(1).id, line.nodes.at(1).id);
    QString serializationError;
    PathGeometry serializedRoundTrip;
    QVERIFY2(PathGeometry::fromJson(restoredLine.toJson(), &serializedRoundTrip,
                                    &serializationError),
             qPrintable(serializationError));
    QCOMPARE(serializedRoundTrip, restoredLine);

    // The conversion operation must also linearize a genuinely curved
    // segment, not merely clear a flag on an already straight one.
    PathGeometry curved = curvedPathForTest();
    curved.id = line.id;
    controller->setPathGeometry(objectId, curved, controller->spatialRevision());
    QTRY_VERIFY_WITH_TIMEOUT(
        controller->sceneGeometry().spatialRevision == controller->spatialRevision(), 5000);
    const SceneObjectGeometry* curvedScene = controller->sceneGeometry().objectById(objectId);
    QVERIFY(curvedScene);
    const QPoint curvedAnchor = canvas->mapDocumentToViewport(
        curvedScene->frame.localPointToPage(curved.nodes.front().anchor)).toPoint();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, curvedAnchor);
    QTest::keyClick(canvas, Qt::Key_L);
    QTRY_VERIFY_WITH_TIMEOUT(controller->document().objectById(objectId)->path.has_value(), 5000);
    const PathGeometry linearized = *controller->document().objectById(objectId)->path;
    QVERIFY(!linearized.isCubicSegment(0));
    QCOMPARE(linearized.nodes.at(0).id, curved.nodes.at(0).id);
    QCOMPARE(linearized.nodes.at(1).id, curved.nodes.at(1).id);
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

