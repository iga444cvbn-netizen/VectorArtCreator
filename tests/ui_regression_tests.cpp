#include "core/effects/wave_effect.h"
#include "ui/editor_canvas.h"
#include "ui/editor_controller.h"
#include "ui/effects_panel.h"
#include "ui/main_window.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPointer>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QTest>

using namespace vt;

class EffectsPanelUiTests final : public QObject {
    Q_OBJECT

private slots:
    void valueRefreshKeepsEmittingControlsAlive();
    void deformationAndMaskStateDoNotOverwriteEachOther();
    void spinBoxArrowHitRegionsIncrementAndDecrement();
};

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
                object.effects.byInstanceId(effectId)->setParameter(parameter, value);
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
    QVERIFY(QTest::qWaitForWindowExposed(&control));

    QDoubleSpinBox* spinBox = control.spinBox();
    QStyleOptionSpinBox option;
    spinBox->initStyleOption(&option);
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

QTEST_MAIN(EffectsPanelUiTests)
#include "ui_regression_tests.moc"
