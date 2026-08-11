#include "core/effects/wave_effect.h"
#include "core/serialization/project_serializer.h"
#include "ui/editor_canvas.h"
#include "ui/editor_controller.h"
#include "ui/effects_panel.h"
#include "ui/main_window.h"
#include "ui/transform_panel.h"
#include "ui/typography_panel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QGraphicsView>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QTest>
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
    void scaleControlsPreserveSmallAndMirroredValues();
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
    auto* addText = window.findChild<QPushButton*>(QStringLiteral("emptyAddText"));
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
        QSKIP("Qt 6.8 offscreen crashes while dispatching a mouse-created QGraphicsProxyWidget session.");
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
    controller->setTool(EditorTool::Text);
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
