#pragma once

#include "core/deformation/manual_deformation.h"
#include "ui/deformation_tool_state.h"
#include "ui/slider_spin_box.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;

namespace vt {

class DeformationPanel final : public QWidget {
    Q_OBJECT

public:
    explicit DeformationPanel(QWidget* parent = nullptr);

    void setTool(EditorTool tool);
    void setNormalBrushSettings(BrushTarget target, qreal radius, qreal strength, qreal hardness);
    void setMaskBrushSettings(qreal radius, qreal opacity, qreal hardness, bool restore);
    void refresh(const ManualDeformation* deformation);
    void refresh(const ManualDeformation& deformation) { refresh(&deformation); }

signals:
    void toolChanged(EditorTool tool);
    void brushSettingsChanged(BrushMode mode,
                              BrushTarget target,
                              qreal radius,
                              qreal strength,
                              qreal hardness);
    void maskSettingsChanged(bool restore);
    void maskBrushSettingsChanged(qreal radius, qreal opacity, qreal hardness, bool restore);
    void enabledChanged(bool enabled);
    void overallStrengthChanged(qreal strength);
    void clearRequested();

private:
    void emitBrushSettings();
    void emitMaskBrushSettings();
    void saveCurrentSettings();
    void loadSettingsForCurrentTool();
    void updateTargetUi();

    QLabel* m_toolLabel = nullptr;
    QComboBox* m_targetCombo = nullptr;
    SliderSpinBox* m_radiusSlider = nullptr;
    SliderSpinBox* m_strengthSlider = nullptr;
    SliderSpinBox* m_hardnessSlider = nullptr;
    QCheckBox* m_enabledCheck = nullptr;
    SliderSpinBox* m_overallStrengthSlider = nullptr;
    QComboBox* m_maskModeCombo = nullptr;
    EditorTool m_tool = EditorTool::Select;
    qreal m_normalRadius = 40.0;
    qreal m_normalStrength = 0.7;
    qreal m_normalHardness = 0.5;
    qreal m_maskRadius = 40.0;
    qreal m_maskOpacity = 0.7 / 4.0;
    qreal m_maskHardness = 0.5;
    BrushTarget m_targetBeforeSmooth = BrushTarget::Shape;
};

} // namespace vt
