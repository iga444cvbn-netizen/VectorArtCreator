#include "core/effects/wave_effect.h"
#include "ui/effects_panel.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPointer>
#include <QTest>

using namespace vt;

class EffectsPanelUiTests final : public QObject {
    Q_OBJECT

private slots:
    void valueRefreshKeepsEmittingControlsAlive();
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

QTEST_MAIN(EffectsPanelUiTests)
#include "ui_regression_tests.moc"
