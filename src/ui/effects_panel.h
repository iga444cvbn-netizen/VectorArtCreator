#pragma once

#include "core/document/document.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace vt {

class EffectsPanel final : public QWidget {
    Q_OBJECT

public:
    explicit EffectsPanel(QWidget* parent = nullptr);

    void refresh(const TextObject& object, const QStringList& presetNames);

signals:
    void addEffectRequested(const QString& typeId);
    void removeEffectRequested(int index);
    void moveEffectRequested(int from, int to);
    void effectEnabledChanged(int index, bool enabled);
    void effectParameterChanged(int index, const QString& parameterId, double value);
    void effectMasterStrengthChanged(int index, double value);
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

private:
    void clearParameterEditor();

    QListWidget* m_effectList = nullptr;
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
    QVector<QDoubleSpinBox*> m_parameterSpins;
    QDoubleSpinBox* m_masterStrengthSpin = nullptr;
    int m_parameterIndex = -1;
    QString m_parameterType;
};

} // namespace vt
