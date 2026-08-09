#pragma once

#include "core/deformation/manual_deformation.h"
#include "ui/deformation_tool_state.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;

namespace vt {

class DeformationPanel final : public QWidget {
    Q_OBJECT

public:
    explicit DeformationPanel(QWidget* parent = nullptr);

    void refresh(const ManualDeformation& deformation);

signals:
    void toolChanged(EditorTool tool);
    void brushSettingsChanged(BrushMode mode,
                              BrushTarget target,
                              qreal radius,
                              qreal strength,
                              qreal hardness);
    void enabledChanged(bool enabled);
    void overallStrengthChanged(qreal strength);
    void clearRequested();

private:
    void handleToolChanged(int index);
    void emitBrushSettings();
    void updateTargetUi();

    QComboBox* m_modeCombo = nullptr;
    QComboBox* m_targetCombo = nullptr;
    QDoubleSpinBox* m_radiusSpin = nullptr;
    QDoubleSpinBox* m_strengthSpin = nullptr;
    QDoubleSpinBox* m_hardnessSpin = nullptr;
    QCheckBox* m_enabledCheck = nullptr;
    QDoubleSpinBox* m_overallStrengthSpin = nullptr;
    DeformationToolState m_toolState;
};

} // namespace vt
