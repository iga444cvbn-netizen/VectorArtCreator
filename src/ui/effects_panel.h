#pragma once

#include "core/document/document.h"
#include "ui/collapsible_section.h"
#include "ui/slider_spin_box.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

class QLabel;
class QCheckBox;

namespace vt {

class EffectsPanel final : public QWidget {
    Q_OBJECT

public:
    explicit EffectsPanel(QWidget* parent = nullptr);

    void refresh(const TextObject* object, const QStringList& presetNames);
    void refresh(const TextObject& object, const QStringList& presetNames)
    {
        refresh(&object, presetNames);
    }
    void setSelectedEffectId(const QString& effectId);
    void setTextRange(int start, int end);
    [[nodiscard]] QString selectedEffectId() const { return m_selectedEffectId; }

signals:
    void addEffectRequested(const QString& typeId);
    void removeEffectRequested(int index);
    void moveEffectRequested(int from, int to);
    void effectEnabledChanged(int index, bool enabled);
    void effectParameterChanged(int index, const QString& parameterId, double value);
    void effectMasterStrengthChanged(int index, double value);
    void effectSelected(const QString& effectId);
    void effectScopeChanged(const QString& effectId, const EffectScope& scope);
    void savePresetRequested(const QString& name);
    void applyPresetRequested(const QString& name);
    void deletePresetRequested(const QString& name);

private slots:
    void handleEffectItemChanged(QListWidgetItem* item);
    void rebuildParameterEditor();
    void updateParameterEditorValues();
    void requestMoveUp();
    void requestMoveDown();
    void requestRemove();
    void handleScopeChanged(int index);
    void resetScope();

private:
    void clearParameterEditor();

    QListWidget* m_effectList = nullptr;
    QLineEdit* m_effectSearch = nullptr;
    QComboBox* m_addEffectCombo = nullptr;
    QPushButton* m_addEffectButton = nullptr;
    QPushButton* m_upButton = nullptr;
    QPushButton* m_downButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QWidget* m_parameterHost = nullptr;
    QVBoxLayout* m_parameterHostLayout = nullptr;
    QLineEdit* m_presetNameEdit = nullptr;
    QPushButton* m_savePresetButton = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QPushButton* m_applyPresetButton = nullptr;
    QPushButton* m_deletePresetButton = nullptr;

    const TextObject* m_currentObject = nullptr;
    QVector<QWidget*> m_parameterControls;
    QDoubleSpinBox* m_masterStrengthSpin = nullptr;
    SliderSpinBox* m_masterStrengthSlider = nullptr;
    QCheckBox* m_effectEnabledCheck = nullptr;
    CollapsibleSection* m_advancedSection = nullptr;
    QComboBox* m_scopeCombo = nullptr;
    QLabel* m_scopeLabel = nullptr;
    QPushButton* m_resetScopeButton = nullptr;
    int m_parameterIndex = -1;
    QString m_parameterType;
    QString m_selectedEffectId;
    int m_textRangeStart = -1;
    int m_textRangeEnd = -1;
};

} // namespace vt
