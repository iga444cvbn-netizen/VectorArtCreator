#include "ui/deformation_panel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace vt {

namespace {

std::optional<BrushMode> brushModeFor(EditorTool tool)
{
    switch (tool) {
    case EditorTool::Push: return BrushMode::Push;
    case EditorTool::Pull: return BrushMode::Pull;
    case EditorTool::Inflate: return BrushMode::Inflate;
    case EditorTool::Pinch: return BrushMode::Pinch;
    case EditorTool::Smooth: return BrushMode::Smooth;
    default: return std::nullopt;
    }
}

QString toolName(EditorTool tool)
{
    switch (tool) {
    case EditorTool::Select: return QStringLiteral("Select");
    case EditorTool::Move: return QStringLiteral("Move");
    case EditorTool::Text: return QStringLiteral("Text");
    case EditorTool::Push: return QStringLiteral("Push");
    case EditorTool::Pull: return QStringLiteral("Pull");
    case EditorTool::Inflate: return QStringLiteral("Inflate");
    case EditorTool::Pinch: return QStringLiteral("Pinch");
    case EditorTool::Smooth: return QStringLiteral("Smooth");
    case EditorTool::EffectMask: return QStringLiteral("Effect mask / eraser");
    }
    return QStringLiteral("Tool");
}

}

DeformationPanel::DeformationPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* group = new QGroupBox(QStringLiteral("Manual deformation"), this);
    auto* layout = new QFormLayout(group);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_toolLabel = new QLabel(group);
    m_toolLabel->setStyleSheet(QStringLiteral("font-weight: 600; color: #8fc7ff;"));
    layout->addRow(QStringLiteral("Active tool"), m_toolLabel);

    m_targetCombo = new QComboBox(group);
    m_targetCombo->setObjectName(QStringLiteral("deformationTarget"));
    m_targetCombo->addItem(QStringLiteral("Glyphs"));
    m_targetCombo->addItem(QStringLiteral("Shape"));
    m_targetCombo->setCurrentIndex(1);
    layout->addRow(QStringLiteral("Target"), m_targetCombo);

    m_maskModeCombo = new QComboBox(group);
    m_maskModeCombo->setObjectName(QStringLiteral("maskMode"));
    m_maskModeCombo->addItem(QStringLiteral("Erase effect"), false);
    m_maskModeCombo->addItem(QStringLiteral("Restore effect"), true);
    layout->addRow(QStringLiteral("Mask mode"), m_maskModeCombo);

    m_radiusSlider = new SliderSpinBox(group);
    m_radiusSlider->setObjectName(QStringLiteral("brushRadius"));
    m_radiusSlider->setRange(1.0, 100000.0);
    m_radiusSlider->setLogarithmic(true);
    m_radiusSlider->setSingleStep(1.0);
    m_radiusSlider->setDecimals(0);
    m_radiusSlider->setSuffix(QStringLiteral(" units"));
    m_radiusSlider->setValue(40.0);
    layout->addRow(QStringLiteral("Radius"), m_radiusSlider);

    m_strengthSlider = new SliderSpinBox(group);
    m_strengthSlider->setObjectName(QStringLiteral("brushStrength"));
    m_strengthSlider->setRange(0.0, 4.0);
    m_strengthSlider->setSingleStep(0.05);
    m_strengthSlider->setDecimals(2);
    m_strengthSlider->setValue(0.7);
    layout->addRow(QStringLiteral("Strength"), m_strengthSlider);

    m_hardnessSlider = new SliderSpinBox(group);
    m_hardnessSlider->setObjectName(QStringLiteral("brushHardness"));
    m_hardnessSlider->setRange(0.0, 1.0);
    m_hardnessSlider->setSingleStep(0.05);
    m_hardnessSlider->setDecimals(2);
    m_hardnessSlider->setValue(0.5);
    layout->addRow(QStringLiteral("Hardness"), m_hardnessSlider);

    m_enabledCheck = new QCheckBox(QStringLiteral("Apply stored strokes"), group);
    m_enabledCheck->setChecked(true);
    layout->addRow(m_enabledCheck);

    m_overallStrengthSlider = new SliderSpinBox(group);
    m_overallStrengthSlider->setRange(0.0, 4.0);
    m_overallStrengthSlider->setSingleStep(0.05);
    m_overallStrengthSlider->setDecimals(2);
    m_overallStrengthSlider->setValue(1.0);
    layout->addRow(QStringLiteral("Overall strength"), m_overallStrengthSlider);

    auto* clearButton = new QPushButton(QStringLiteral("Clear deformation strokes"), group);
    layout->addRow(clearButton);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(group);

    connect(m_targetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { emitBrushSettings(); });
    const auto emitActiveSettings = [this](double) {
        if (m_tool == EditorTool::EffectMask) {
            emitMaskBrushSettings();
        } else {
            emitBrushSettings();
        }
    };
    connect(m_radiusSlider, &SliderSpinBox::valueChanged, this, emitActiveSettings);
    connect(m_strengthSlider, &SliderSpinBox::valueChanged, this, emitActiveSettings);
    connect(m_hardnessSlider, &SliderSpinBox::valueChanged, this, emitActiveSettings);
    connect(m_enabledCheck, &QCheckBox::toggled, this, &DeformationPanel::enabledChanged);
    connect(m_overallStrengthSlider, &SliderSpinBox::valueChanged,
            this, &DeformationPanel::overallStrengthChanged);
    connect(m_maskModeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                const bool restore = m_maskModeCombo->itemData(index).toBool();
                emit maskSettingsChanged(restore);
                emitMaskBrushSettings();
            });
    connect(clearButton, &QPushButton::clicked, this, &DeformationPanel::clearRequested);
    setTool(EditorTool::Select);
}

void DeformationPanel::setTool(EditorTool tool)
{
    saveCurrentSettings();
    m_tool = tool;
    loadSettingsForCurrentTool();
    m_toolLabel->setText(toolName(tool));
    const bool brush = brushModeFor(tool).has_value();
    const bool mask = tool == EditorTool::EffectMask;
    m_targetCombo->setEnabled(brush && tool != EditorTool::Smooth);
    m_maskModeCombo->setVisible(mask);
    m_maskModeCombo->setEnabled(mask);
    updateTargetUi();
    // Tool selection is owned by EditorController.  Do not echo a panel
    // refresh back as brush settings: while entering Smooth the combo still
    // displays its old value, which would overwrite the controller's saved
    // pre-Smooth target before it publishes the authoritative Shape target.
}

void DeformationPanel::setNormalBrushSettings(BrushTarget target,
                                              qreal radius,
                                              qreal strength,
                                              qreal hardness)
{
    m_normalRadius = radius;
    m_normalStrength = strength;
    m_normalHardness = hardness;
    if (m_tool != EditorTool::EffectMask) {
        loadSettingsForCurrentTool();
        const QSignalBlocker blocker(m_targetCombo);
        m_targetCombo->setCurrentIndex(target == BrushTarget::Glyphs ? 0 : 1);
    }
}

void DeformationPanel::setMaskBrushSettings(qreal radius,
                                            qreal opacity,
                                            qreal hardness,
                                            bool restore)
{
    m_maskRadius = radius;
    m_maskOpacity = opacity;
    m_maskHardness = hardness;
    {
        const QSignalBlocker blocker(m_maskModeCombo);
        m_maskModeCombo->setCurrentIndex(restore ? 1 : 0);
    }
    if (m_tool == EditorTool::EffectMask) {
        loadSettingsForCurrentTool();
    }
}

void DeformationPanel::refresh(const ManualDeformation* deformation)
{
    const bool hasObject = deformation != nullptr;
    setEnabled(hasObject);
    if (!deformation) {
        return;
    }
    const QSignalBlocker enabledBlocker(m_enabledCheck);
    const QSignalBlocker strengthBlocker(m_overallStrengthSlider);
    m_enabledCheck->setChecked(deformation->enabled);
    m_overallStrengthSlider->setValue(deformation->strength);
}

void DeformationPanel::updateTargetUi()
{
    m_targetCombo->setEnabled(brushModeFor(m_tool).has_value() && m_tool != EditorTool::Smooth);
}

void DeformationPanel::emitBrushSettings()
{
    const std::optional<BrushMode> mode = brushModeFor(m_tool);
    if (!mode.has_value()) {
        return;
    }
    emit brushSettingsChanged(*mode,
                              m_targetCombo->currentIndex() == 0 ? BrushTarget::Glyphs : BrushTarget::Shape,
                              m_radiusSlider->value(),
                              m_strengthSlider->value(),
                              m_hardnessSlider->value());
}

void DeformationPanel::emitMaskBrushSettings()
{
    if (m_tool != EditorTool::EffectMask) {
        return;
    }
    emit maskBrushSettingsChanged(m_radiusSlider->value(),
                                  qBound<qreal>(0.0, m_strengthSlider->value() / 4.0, 1.0),
                                  m_hardnessSlider->value(),
                                  m_maskModeCombo->currentData().toBool());
}

void DeformationPanel::saveCurrentSettings()
{
    if (m_tool == EditorTool::EffectMask) {
        m_maskRadius = m_radiusSlider->value();
        m_maskOpacity = qBound<qreal>(0.0, m_strengthSlider->value() / 4.0, 1.0);
        m_maskHardness = m_hardnessSlider->value();
    } else {
        m_normalRadius = m_radiusSlider->value();
        m_normalStrength = m_strengthSlider->value();
        m_normalHardness = m_hardnessSlider->value();
    }
}

void DeformationPanel::loadSettingsForCurrentTool()
{
    const QSignalBlocker radiusBlocker(m_radiusSlider);
    const QSignalBlocker strengthBlocker(m_strengthSlider);
    const QSignalBlocker hardnessBlocker(m_hardnessSlider);
    if (m_tool == EditorTool::EffectMask) {
        m_radiusSlider->setValue(m_maskRadius);
        m_strengthSlider->setValue(m_maskOpacity * 4.0);
        m_hardnessSlider->setValue(m_maskHardness);
    } else {
        m_radiusSlider->setValue(m_normalRadius);
        m_strengthSlider->setValue(m_normalStrength);
        m_hardnessSlider->setValue(m_normalHardness);
    }
}

} // namespace vt
