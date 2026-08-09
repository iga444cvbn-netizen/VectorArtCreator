#include "ui/deformation_panel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace vt {

namespace {

QDoubleSpinBox* makeSpin(double minimum, double maximum, double step, QWidget* parent)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setSingleStep(step);
    spin->setDecimals(3);
    return spin;
}

} // namespace

DeformationPanel::DeformationPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* group = new QGroupBox(QStringLiteral("Manual deformation"), this);
    auto* layout = new QFormLayout(group);
    layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_modeCombo = new QComboBox(group);
    m_modeCombo->addItem(QStringLiteral("Push"));
    m_modeCombo->addItem(QStringLiteral("Pull"));
    m_modeCombo->addItem(QStringLiteral("Inflate"));
    m_modeCombo->addItem(QStringLiteral("Pinch"));
    m_modeCombo->addItem(QStringLiteral("Smooth"));
    layout->addRow(QStringLiteral("Tool"), m_modeCombo);

    m_targetCombo = new QComboBox(group);
    m_targetCombo->addItem(QStringLiteral("Glyphs"));
    m_targetCombo->addItem(QStringLiteral("Shape"));
    m_targetCombo->setCurrentIndex(1);
    layout->addRow(QStringLiteral("Target"), m_targetCombo);

    m_radiusSpin = makeSpin(1.0, 100000.0, 1.0, group);
    m_radiusSpin->setValue(40.0);
    m_radiusSpin->setSuffix(QStringLiteral(" document units"));
    layout->addRow(QStringLiteral("Radius"), m_radiusSpin);

    m_strengthSpin = makeSpin(0.0, 4.0, 0.05, group);
    m_strengthSpin->setValue(0.7);
    layout->addRow(QStringLiteral("Brush strength"), m_strengthSpin);

    m_hardnessSpin = makeSpin(0.0, 1.0, 0.05, group);
    m_hardnessSpin->setValue(0.5);
    layout->addRow(QStringLiteral("Hardness"), m_hardnessSpin);

    m_enabledCheck = new QCheckBox(QStringLiteral("Apply stored strokes"), group);
    m_enabledCheck->setChecked(true);
    layout->addRow(m_enabledCheck);

    m_overallStrengthSpin = makeSpin(0.0, 4.0, 0.05, group);
    m_overallStrengthSpin->setValue(1.0);
    layout->addRow(QStringLiteral("Overall strength"), m_overallStrengthSpin);

    auto* clearButton = new QPushButton(QStringLiteral("Clear deformation strokes"), group);
    layout->addRow(clearButton);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(group);

    connect(m_modeCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int) { emitBrushSettings(); });
    connect(m_targetCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int) { emitBrushSettings(); });
    connect(m_radiusSpin, &QDoubleSpinBox::valueChanged, this, [this] { emitBrushSettings(); });
    connect(m_strengthSpin, &QDoubleSpinBox::valueChanged, this, [this] { emitBrushSettings(); });
    connect(m_hardnessSpin, &QDoubleSpinBox::valueChanged, this, [this] { emitBrushSettings(); });
    connect(m_enabledCheck, &QCheckBox::toggled, this, &DeformationPanel::enabledChanged);
    connect(m_overallStrengthSpin,
            &QDoubleSpinBox::valueChanged,
            this,
            &DeformationPanel::overallStrengthChanged);
    connect(clearButton, &QPushButton::clicked, this, &DeformationPanel::clearRequested);
}

void DeformationPanel::refresh(const ManualDeformation& deformation)
{
    const QSignalBlocker enabledBlocker(m_enabledCheck);
    const QSignalBlocker strengthBlocker(m_overallStrengthSpin);
    m_enabledCheck->setChecked(deformation.enabled);
    m_overallStrengthSpin->setValue(deformation.strength);
}

void DeformationPanel::emitBrushSettings()
{
    BrushMode mode = BrushMode::Push;
    switch (m_modeCombo->currentIndex()) {
    case 1:
        mode = BrushMode::Pull;
        break;
    case 2:
        mode = BrushMode::Inflate;
        break;
    case 3:
        mode = BrushMode::Pinch;
        break;
    case 4:
        mode = BrushMode::Smooth;
        break;
    default:
        break;
    }
    const BrushTarget target = m_targetCombo->currentIndex() == 0
        ? BrushTarget::Glyphs
        : BrushTarget::Shape;
    emit brushSettingsChanged(mode,
                              target,
                              m_radiusSpin->value(),
                              m_strengthSpin->value(),
                              m_hardnessSpin->value());
}

} // namespace vt
