#include "ui/effects_panel.h"

#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>

namespace vt {

EffectsPanel::EffectsPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto* effectsGroup = new QGroupBox(QStringLiteral("Effect stack"), this);
    auto* effectsLayout = new QVBoxLayout(effectsGroup);

    auto* addRow = new QHBoxLayout();
    m_addEffectCombo = new QComboBox(effectsGroup);
    for (const auto& [typeId, displayName] : availableEffectTypes()) {
        m_addEffectCombo->addItem(displayName, typeId);
    }
    m_addEffectButton = new QPushButton(QStringLiteral("Add"), effectsGroup);
    addRow->addWidget(m_addEffectCombo, 1);
    addRow->addWidget(m_addEffectButton);
    effectsLayout->addLayout(addRow);

    m_effectList = new QListWidget(effectsGroup);
    m_effectList->setMinimumHeight(130);
    effectsLayout->addWidget(m_effectList);

    auto* buttonRow = new QHBoxLayout();
    m_upButton = new QPushButton(QStringLiteral("Up"), effectsGroup);
    m_downButton = new QPushButton(QStringLiteral("Down"), effectsGroup);
    m_removeButton = new QPushButton(QStringLiteral("Remove"), effectsGroup);
    buttonRow->addWidget(m_upButton);
    buttonRow->addWidget(m_downButton);
    buttonRow->addWidget(m_removeButton);
    effectsLayout->addLayout(buttonRow);
    outerLayout->addWidget(effectsGroup);

    auto* parametersGroup = new QGroupBox(QStringLiteral("Selected effect"), this);
    m_parameterHostLayout = new QVBoxLayout(parametersGroup);
    m_parameterHost = parametersGroup;
    outerLayout->addWidget(parametersGroup);

    auto* presetGroup = new QGroupBox(QStringLiteral("Presets"), this);
    auto* presetLayout = new QVBoxLayout(presetGroup);
    auto* saveRow = new QHBoxLayout();
    m_presetNameEdit = new QLineEdit(presetGroup);
    m_presetNameEdit->setPlaceholderText(QStringLiteral("Preset name"));
    m_savePresetButton = new QPushButton(QStringLiteral("Save current"), presetGroup);
    saveRow->addWidget(m_presetNameEdit, 1);
    saveRow->addWidget(m_savePresetButton);
    presetLayout->addLayout(saveRow);

    auto* applyRow = new QHBoxLayout();
    m_presetCombo = new QComboBox(presetGroup);
    m_applyPresetButton = new QPushButton(QStringLiteral("Apply"), presetGroup);
    m_deletePresetButton = new QPushButton(QStringLiteral("Delete"), presetGroup);
    applyRow->addWidget(m_presetCombo, 1);
    applyRow->addWidget(m_applyPresetButton);
    applyRow->addWidget(m_deletePresetButton);
    presetLayout->addLayout(applyRow);
    outerLayout->addWidget(presetGroup);
    outerLayout->addStretch(1);

    connect(m_addEffectButton, &QPushButton::clicked, this, [this] {
        emit addEffectRequested(m_addEffectCombo->currentData().toString());
    });
    connect(m_effectList, &QListWidget::itemChanged, this, &EffectsPanel::handleEffectItemChanged);
    connect(m_effectList, &QListWidget::currentRowChanged, this, [this](int) {
        rebuildParameterEditor();
    });
    connect(m_upButton, &QPushButton::clicked, this, &EffectsPanel::requestMoveUp);
    connect(m_downButton, &QPushButton::clicked, this, &EffectsPanel::requestMoveDown);
    connect(m_removeButton, &QPushButton::clicked, this, &EffectsPanel::requestRemove);
    connect(m_savePresetButton, &QPushButton::clicked, this, [this] {
        emit savePresetRequested(m_presetNameEdit->text().trimmed());
    });
    connect(m_applyPresetButton, &QPushButton::clicked, this, [this] {
        emit applyPresetRequested(m_presetCombo->currentText());
    });
    connect(m_deletePresetButton, &QPushButton::clicked, this, [this] {
        emit deletePresetRequested(m_presetCombo->currentText());
    });

    rebuildParameterEditor();
}

void EffectsPanel::refresh(const TextObject& object, const QStringList& presetNames)
{
    m_currentObject = &object;
    const int previousRow = m_effectList->currentRow();
    {
        const QSignalBlocker blocker(m_effectList);
        m_effectList->clear();
        for (int index = 0; index < object.effects.size(); ++index) {
            const Effect* effect = object.effects.at(index);
            if (!effect) {
                continue;
            }
            auto* item = new QListWidgetItem(effect->displayName(), m_effectList);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(effect->enabled ? Qt::Checked : Qt::Unchecked);
        }
        if (m_effectList->count() > 0) {
            m_effectList->setCurrentRow(qBound(0, previousRow, m_effectList->count() - 1));
        }
    }

    {
        const QSignalBlocker blocker(m_presetCombo);
        m_presetCombo->clear();
        m_presetCombo->addItems(presetNames);
    }

    const int currentIndex = m_effectList->currentRow();
    const Effect* currentEffect = currentIndex >= 0 ? object.effects.at(currentIndex) : nullptr;
    const bool canUpdateInPlace = currentEffect
        && currentIndex == m_parameterIndex
        && currentEffect->typeId() == m_parameterType
        && currentEffect->parameterDefinitions().size() == m_parameterSpins.size();
    if (canUpdateInPlace) {
        updateParameterEditorValues();
    } else {
        rebuildParameterEditor();
    }
}

void EffectsPanel::handleEffectItemChanged(QListWidgetItem* item)
{
    if (!item) {
        return;
    }
    emit effectEnabledChanged(m_effectList->row(item), item->checkState() == Qt::Checked);
}

void EffectsPanel::rebuildParameterEditor()
{
    clearParameterEditor();
    const int index = m_effectList->currentRow();
    if (!m_currentObject || index < 0 || index >= m_currentObject->effects.size()) {
        auto* label = new QLabel(QStringLiteral("Select an effect to edit its parameters."), m_parameterHost);
        label->setWordWrap(true);
        m_parameterHostLayout->addWidget(label);
        m_upButton->setEnabled(false);
        m_downButton->setEnabled(false);
        m_removeButton->setEnabled(false);
        return;
    }

    const Effect* effect = m_currentObject->effects.at(index);
    if (!effect) {
        return;
    }

    m_parameterIndex = index;
    m_parameterType = effect->typeId();

    auto* masterRow = new QWidget(m_parameterHost);
    auto* masterLayout = new QHBoxLayout(masterRow);
    masterLayout->setContentsMargins(0, 0, 0, 0);
    masterLayout->addWidget(new QLabel(QStringLiteral("Master strength"), masterRow));
    m_masterStrengthSpin = new QDoubleSpinBox(masterRow);
    m_masterStrengthSpin->setRange(0.0, 1.0);
    m_masterStrengthSpin->setSingleStep(0.01);
    m_masterStrengthSpin->setDecimals(3);
    m_masterStrengthSpin->setValue(effect->masterStrength);
    masterLayout->addWidget(m_masterStrengthSpin, 1);
    m_parameterHostLayout->addWidget(masterRow);
    connect(m_masterStrengthSpin, &QDoubleSpinBox::valueChanged, this,
            [this, index](double value) { emit effectMasterStrengthChanged(index, value); });

    for (const EffectParameter& parameter : effect->parameterDefinitions()) {
        auto* row = new QWidget(m_parameterHost);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto* label = new QLabel(parameter.label, row);
        label->setMinimumWidth(108);
        rowLayout->addWidget(label);

        auto* spin = new QDoubleSpinBox(row);
        spin->setRange(parameter.minimum, parameter.maximum);
        spin->setSingleStep(parameter.step);
        spin->setDecimals(parameter.integer ? 0 : 3);
        spin->setValue(parameter.value);
        rowLayout->addWidget(spin, 1);
        m_parameterHostLayout->addWidget(row);
        m_parameterSpins.push_back(spin);

        const QString parameterId = parameter.id;
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, index, parameterId](double value) {
            emit effectParameterChanged(index, parameterId, value);
        });
    }

    m_upButton->setEnabled(index > 0);
    m_downButton->setEnabled(index + 1 < m_currentObject->effects.size());
    m_removeButton->setEnabled(true);
}

void EffectsPanel::updateParameterEditorValues()
{
    if (!m_currentObject || m_parameterIndex < 0
        || m_parameterIndex >= m_currentObject->effects.size()) {
        rebuildParameterEditor();
        return;
    }
    const Effect* effect = m_currentObject->effects.at(m_parameterIndex);
    if (!effect || effect->typeId() != m_parameterType) {
        rebuildParameterEditor();
        return;
    }
    const QVector<EffectParameter> parameters = effect->parameterDefinitions();
    if (parameters.size() != m_parameterSpins.size()) {
        rebuildParameterEditor();
        return;
    }
    for (int index = 0; index < parameters.size(); ++index) {
        const QSignalBlocker blocker(m_parameterSpins[index]);
        m_parameterSpins[index]->setValue(parameters[index].value);
    }
    if (m_masterStrengthSpin) {
        const QSignalBlocker blocker(m_masterStrengthSpin);
        m_masterStrengthSpin->setValue(effect->masterStrength);
    }
}

void EffectsPanel::requestMoveUp()
{
    const int index = m_effectList->currentRow();
    if (index > 0) {
        emit moveEffectRequested(index, index - 1);
    }
}

void EffectsPanel::requestMoveDown()
{
    const int index = m_effectList->currentRow();
    if (index >= 0 && index + 1 < m_effectList->count()) {
        emit moveEffectRequested(index, index + 1);
    }
}

void EffectsPanel::requestRemove()
{
    const int index = m_effectList->currentRow();
    if (index >= 0) {
        emit removeEffectRequested(index);
    }
}

void EffectsPanel::clearParameterEditor()
{
    m_parameterSpins.clear();
    m_masterStrengthSpin = nullptr;
    m_parameterIndex = -1;
    m_parameterType.clear();
    while (QLayoutItem* item = m_parameterHostLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            delete widget;
        }
        delete item;
    }
}

} // namespace vt
