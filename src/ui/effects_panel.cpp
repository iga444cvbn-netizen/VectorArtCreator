#include "ui/effects_panel.h"

#include <QDoubleSpinBox>
#include <QAbstractItemView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRandomGenerator>
#include <QSignalBlocker>

namespace vt {

namespace {

QString categoryFor(const QString& typeId)
{
    if (typeId == QStringLiteral("wave") || typeId == QStringLiteral("bounce")
        || typeId == QStringLiteral("staircase") || typeId == QStringLiteral("arc")
        || typeId == QStringLiteral("zigzag") || typeId == QStringLiteral("sineRotation")
        || typeId == QStringLiteral("baselineDrift")) {
        return QStringLiteral("Baseline");
    }
    if (typeId.contains(QStringLiteral("random"), Qt::CaseInsensitive)
        || typeId == QStringLiteral("glyphJitter")
        || typeId == QStringLiteral("alternatingTilt")) {
        return QStringLiteral("Random");
    }
    if (typeId == QStringLiteral("stretch") || typeId == QStringLiteral("compression")
        || typeId == QStringLiteral("skew") || typeId == QStringLiteral("crescendo")
        || typeId == QStringLiteral("shrink") || typeId == QStringLiteral("horizontalSpread")
        || typeId == QStringLiteral("verticalSpread") || typeId == QStringLiteral("expandCenter")
        || typeId == QStringLiteral("squeezeCenter")) {
        return QStringLiteral("Transform");
    }
    return QStringLiteral("Warp");
}

void addEffectChoice(QComboBox* combo, const QString& typeId, const QString& displayName)
{
    combo->addItem(QStringLiteral("[%1] %2").arg(categoryFor(typeId), displayName), typeId);
}

}

EffectsPanel::EffectsPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(6);

    auto* effectsGroup = new QGroupBox(QStringLiteral("Effects"), this);
    auto* effectsLayout = new QVBoxLayout(effectsGroup);
    effectsLayout->setContentsMargins(8, 8, 8, 8);

    auto* searchRow = new QHBoxLayout();
    m_effectSearch = new QLineEdit(effectsGroup);
    m_effectSearch->setPlaceholderText(QStringLiteral("Search effects…"));
    searchRow->addWidget(m_effectSearch, 1);
    effectsLayout->addLayout(searchRow);

    auto* addRow = new QHBoxLayout();
    m_addEffectCombo = new QComboBox(effectsGroup);
    for (const auto& [typeId, displayName] : availableEffectTypes()) {
        addEffectChoice(m_addEffectCombo, typeId, displayName);
    }
    m_addEffectButton = new QPushButton(QStringLiteral("Add"), effectsGroup);
    addRow->addWidget(m_addEffectCombo, 1);
    addRow->addWidget(m_addEffectButton);
    effectsLayout->addLayout(addRow);

    m_effectList = new QListWidget(effectsGroup);
    m_effectList->setMinimumHeight(120);
    m_effectList->setSelectionMode(QAbstractItemView::SingleSelection);
    effectsLayout->addWidget(m_effectList);

    auto* buttonRow = new QHBoxLayout();
    m_upButton = new QPushButton(QStringLiteral("↑"), effectsGroup);
    m_downButton = new QPushButton(QStringLiteral("↓"), effectsGroup);
    m_removeButton = new QPushButton(QStringLiteral("Remove"), effectsGroup);
    m_upButton->setToolTip(QStringLiteral("Move effect up"));
    m_downButton->setToolTip(QStringLiteral("Move effect down"));
    buttonRow->addWidget(m_upButton);
    buttonRow->addWidget(m_downButton);
    buttonRow->addWidget(m_removeButton, 1);
    effectsLayout->addLayout(buttonRow);
    outerLayout->addWidget(effectsGroup);

    auto* parametersGroup = new QGroupBox(QStringLiteral("Selected effect"), this);
    m_parameterHostLayout = new QVBoxLayout(parametersGroup);
    m_parameterHostLayout->setContentsMargins(8, 8, 8, 8);
    m_parameterHost = parametersGroup;
    outerLayout->addWidget(parametersGroup);

    auto* presetGroup = new QGroupBox(QStringLiteral("Presets"), this);
    auto* presetLayout = new QVBoxLayout(presetGroup);
    auto* saveRow = new QHBoxLayout();
    m_presetNameEdit = new QLineEdit(presetGroup);
    m_presetNameEdit->setPlaceholderText(QStringLiteral("Preset name"));
    m_savePresetButton = new QPushButton(QStringLiteral("Save"), presetGroup);
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

    connect(m_effectSearch, &QLineEdit::textChanged, this, [this](const QString& query) {
        const QString currentType = m_addEffectCombo->currentData().toString();
        const QSignalBlocker blocker(m_addEffectCombo);
        m_addEffectCombo->clear();
        for (const auto& [typeId, displayName] : availableEffectTypes()) {
            if (query.trimmed().isEmpty()
                || displayName.contains(query, Qt::CaseInsensitive)
                || categoryFor(typeId).contains(query, Qt::CaseInsensitive)) {
                addEffectChoice(m_addEffectCombo, typeId, displayName);
            }
        }
        const int index = m_addEffectCombo->findData(currentType);
        m_addEffectCombo->setCurrentIndex(index >= 0 ? index : 0);
    });
    connect(m_addEffectButton, &QPushButton::clicked, this, [this] {
        emit addEffectRequested(m_addEffectCombo->currentData().toString());
    });
    connect(m_effectList, &QListWidget::itemChanged, this, &EffectsPanel::handleEffectItemChanged);
    connect(m_effectList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_selectedEffectId = row >= 0 && m_effectList->item(row)
            ? m_effectList->item(row)->data(Qt::UserRole).toString()
            : QString();
        emit effectSelected(m_selectedEffectId);
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

void EffectsPanel::setTextRange(int start, int end)
{
    m_textRangeStart = start;
    m_textRangeEnd = end;
    rebuildParameterEditor();
}

void EffectsPanel::setSelectedEffectId(const QString& effectId)
{
    int row = -1;
    for (int index = 0; index < m_effectList->count(); ++index) {
        if (m_effectList->item(index)->data(Qt::UserRole).toString() == effectId) {
            row = index;
            break;
        }
    }
    const QSignalBlocker blocker(m_effectList);
    m_effectList->setCurrentRow(row);
    m_selectedEffectId = row >= 0 ? effectId : QString();
    rebuildParameterEditor();
}

void EffectsPanel::refresh(const TextObject* object, const QStringList& presetNames)
{
    m_currentObject = object;
    setEnabled(object != nullptr);
    const QString previousId = m_selectedEffectId;
    {
        const QSignalBlocker blocker(m_effectList);
        m_effectList->clear();
        if (object) {
            for (int index = 0; index < object->effects.size(); ++index) {
                const Effect* effect = object->effects.at(index);
                if (!effect) {
                    continue;
                }
                auto* item = new QListWidgetItem(effect->displayName(), m_effectList);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(effect->enabled ? Qt::Checked : Qt::Unchecked);
                item->setData(Qt::UserRole, effect->instanceId);
                item->setToolTip(QStringLiteral("%1\nScope: %2")
                                     .arg(effect->displayName(),
                                          effect->scope.kind == EffectScopeKind::WholeObject
                                              ? QStringLiteral("Whole Object")
                                              : QStringLiteral("Selected Text Range")));
            }
        }
        int selectedRow = -1;
        for (int row = 0; row < m_effectList->count(); ++row) {
            if (m_effectList->item(row)->data(Qt::UserRole).toString() == previousId) {
                selectedRow = row;
                break;
            }
        }
        if (selectedRow < 0 && m_effectList->count() > 0) {
            selectedRow = 0;
        }
        m_effectList->setCurrentRow(selectedRow);
        m_selectedEffectId = selectedRow >= 0 && m_effectList->item(selectedRow)
            ? m_effectList->item(selectedRow)->data(Qt::UserRole).toString()
            : QString();
    }

    {
        const QSignalBlocker blocker(m_presetCombo);
        m_presetCombo->clear();
        m_presetCombo->addItems(presetNames);
    }

    if (!object) {
        m_selectedEffectId.clear();
    }
    rebuildParameterEditor();
}

void EffectsPanel::handleEffectItemChanged(QListWidgetItem* item)
{
    if (!item || !m_currentObject) {
        return;
    }
    const QString effectId = item->data(Qt::UserRole).toString();
    const int index = m_currentObject->effects.indexByInstanceId(effectId);
    if (index >= 0) {
        emit effectEnabledChanged(index, item->checkState() == Qt::Checked);
    }
}

void EffectsPanel::rebuildParameterEditor()
{
    clearParameterEditor();
    if (!m_currentObject || m_selectedEffectId.isEmpty()) {
        auto* label = new QLabel(m_currentObject
                                     ? QStringLiteral("Select an effect to edit its parameters.")
                                     : QStringLiteral("No object selected."),
                                 m_parameterHost);
        label->setWordWrap(true);
        m_parameterHostLayout->addWidget(label);
        return;
    }

    const int index = m_currentObject->effects.indexByInstanceId(m_selectedEffectId);
    if (index < 0) {
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
    m_masterStrengthSlider = new SliderSpinBox(masterRow);
    m_masterStrengthSlider->setRange(0.0, 1.0);
    m_masterStrengthSlider->setSingleStep(0.01);
    m_masterStrengthSlider->setDecimals(2);
    m_masterStrengthSlider->setValue(effect->masterStrength);
    masterLayout->addWidget(m_masterStrengthSlider, 1);
    m_parameterHostLayout->addWidget(masterRow);
    const QString effectId = m_selectedEffectId;
    connect(m_masterStrengthSlider, &SliderSpinBox::valueChanged, this,
            [this, effectId](double value) {
                if (m_currentObject) {
                    const int currentIndex = m_currentObject->effects.indexByInstanceId(effectId);
                    if (currentIndex >= 0) {
                        emit effectMasterStrengthChanged(currentIndex, value);
                    }
                }
            });

    auto* scopeRow = new QWidget(m_parameterHost);
    auto* scopeLayout = new QHBoxLayout(scopeRow);
    scopeLayout->setContentsMargins(0, 4, 0, 0);
    scopeLayout->addWidget(new QLabel(QStringLiteral("Scope"), scopeRow));
    m_scopeCombo = new QComboBox(scopeRow);
    m_scopeCombo->addItem(QStringLiteral("Whole Object"), false);
    m_scopeCombo->addItem(QStringLiteral("Selected Text Range"), true);
    m_scopeCombo->setCurrentIndex(effect->scope.kind == EffectScopeKind::TextRange ? 1 : 0);
    m_scopeCombo->setItemData(1, m_textRangeStart >= 0 && m_textRangeEnd > m_textRangeStart,
                              Qt::UserRole + 1);
    const bool hasRange = m_textRangeStart >= 0 && m_textRangeEnd > m_textRangeStart;
    m_scopeCombo->setEnabled(hasRange);
    scopeLayout->addWidget(m_scopeCombo, 1);
    m_resetScopeButton = new QPushButton(QStringLiteral("Reset"), scopeRow);
    m_resetScopeButton->setToolTip(QStringLiteral("Reset effect to Whole Object"));
    scopeLayout->addWidget(m_resetScopeButton);
    m_parameterHostLayout->addWidget(scopeRow);
    connect(m_scopeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &EffectsPanel::handleScopeChanged);
    connect(m_resetScopeButton, &QPushButton::clicked, this, &EffectsPanel::resetScope);
    m_scopeLabel = new QLabel(m_parameterHost);
    if (effect->scope.kind == EffectScopeKind::TextRange) {
        m_scopeLabel->setText(QStringLiteral("Characters %1–%2")
                                  .arg(effect->scope.start)
                                  .arg(effect->scope.end));
    } else {
        m_scopeLabel->setText(QStringLiteral("All glyphs in this object"));
    }
    m_scopeLabel->setStyleSheet(QStringLiteral("color: #9da8b8;"));
    m_parameterHostLayout->addWidget(m_scopeLabel);

    for (const EffectParameter& parameter : effect->parameterDefinitions()) {
        auto* row = new QWidget(m_parameterHost);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 2, 0, 2);
        auto* label = new QLabel(parameter.label, row);
        label->setMinimumWidth(96);
        rowLayout->addWidget(label);

        QWidget* control = nullptr;
        if (parameter.integer) {
            auto* spin = new QDoubleSpinBox(row);
            spin->setRange(parameter.minimum, parameter.maximum);
            spin->setSingleStep(parameter.step);
            spin->setDecimals(0);
            spin->setValue(parameter.value);
            control = spin;
            rowLayout->addWidget(spin, 1);
            const QString parameterId = parameter.id;
            connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                    [this, effectId, parameterId](double value) {
                        if (m_currentObject) {
                            const int currentIndex = m_currentObject->effects.indexByInstanceId(effectId);
                            if (currentIndex >= 0) {
                                emit effectParameterChanged(currentIndex, parameterId, value);
                            }
                        }
                    });
            if (parameter.id == QStringLiteral("seed")) {
                auto* randomize = new QPushButton(QStringLiteral("↻"), row);
                randomize->setToolTip(QStringLiteral("Randomize seed"));
                rowLayout->addWidget(randomize);
                connect(randomize, &QPushButton::clicked, this, [this, effectId, parameterId] {
                    if (m_currentObject) {
                        const int currentIndex = m_currentObject->effects.indexByInstanceId(effectId);
                        if (currentIndex >= 0) {
                            emit effectParameterChanged(currentIndex,
                                                        parameterId,
                                                        QRandomGenerator::global()->generate());
                        }
                    }
                });
            }
        } else {
            auto* slider = new SliderSpinBox(row);
            slider->setRange(parameter.minimum, parameter.maximum);
            slider->setSingleStep(parameter.step);
            slider->setDecimals(3);
            slider->setValue(parameter.value);
            control = slider;
            rowLayout->addWidget(slider, 1);
            const QString parameterId = parameter.id;
            connect(slider, &SliderSpinBox::valueChanged, this,
                    [this, effectId, parameterId](double value) {
                        if (m_currentObject) {
                            const int currentIndex = m_currentObject->effects.indexByInstanceId(effectId);
                            if (currentIndex >= 0) {
                                emit effectParameterChanged(currentIndex, parameterId, value);
                            }
                        }
                    });
        }
        m_parameterHostLayout->addWidget(row);
        m_parameterControls.push_back(control);
    }

    m_upButton->setEnabled(index > 0);
    m_downButton->setEnabled(index + 1 < m_currentObject->effects.size());
    m_removeButton->setEnabled(true);
}

void EffectsPanel::updateParameterEditorValues()
{
    if (!m_currentObject || m_selectedEffectId.isEmpty()) {
        rebuildParameterEditor();
        return;
    }
    const int index = m_currentObject->effects.indexByInstanceId(m_selectedEffectId);
    const Effect* effect = index >= 0 ? m_currentObject->effects.at(index) : nullptr;
    if (!effect || effect->typeId() != m_parameterType
        || effect->parameterDefinitions().size() != m_parameterControls.size()) {
        rebuildParameterEditor();
        return;
    }
    const QVector<EffectParameter> parameters = effect->parameterDefinitions();
    for (int parameterIndex = 0; parameterIndex < parameters.size(); ++parameterIndex) {
        if (auto* slider = qobject_cast<SliderSpinBox*>(m_parameterControls[parameterIndex])) {
            const QSignalBlocker blocker(slider);
            slider->setValue(parameters[parameterIndex].value);
        } else if (auto* spin = qobject_cast<QDoubleSpinBox*>(m_parameterControls[parameterIndex])) {
            const QSignalBlocker blocker(spin);
            spin->setValue(parameters[parameterIndex].value);
        }
    }
    if (m_masterStrengthSlider) {
        const QSignalBlocker blocker(m_masterStrengthSlider);
        m_masterStrengthSlider->setValue(effect->masterStrength);
    }
}

void EffectsPanel::requestMoveUp()
{
    if (!m_currentObject) {
        return;
    }
    const int index = m_currentObject->effects.indexByInstanceId(m_selectedEffectId);
    if (index > 0) {
        emit moveEffectRequested(index, index - 1);
    }
}

void EffectsPanel::requestMoveDown()
{
    if (!m_currentObject) {
        return;
    }
    const int index = m_currentObject->effects.indexByInstanceId(m_selectedEffectId);
    if (index >= 0 && index + 1 < m_currentObject->effects.size()) {
        emit moveEffectRequested(index, index + 1);
    }
}

void EffectsPanel::requestRemove()
{
    if (!m_currentObject) {
        return;
    }
    const int index = m_currentObject->effects.indexByInstanceId(m_selectedEffectId);
    if (index >= 0) {
        emit removeEffectRequested(index);
    }
}

void EffectsPanel::handleScopeChanged(int index)
{
    if (m_selectedEffectId.isEmpty()) {
        return;
    }
    EffectScope scope;
    if (index == 1 && m_textRangeStart >= 0 && m_textRangeEnd > m_textRangeStart) {
        scope.kind = EffectScopeKind::TextRange;
        scope.start = m_textRangeStart;
        scope.end = m_textRangeEnd;
    }
    emit effectScopeChanged(m_selectedEffectId, scope);
}

void EffectsPanel::resetScope()
{
    if (!m_selectedEffectId.isEmpty()) {
        emit effectScopeChanged(m_selectedEffectId, EffectScope());
    }
}

void EffectsPanel::clearParameterEditor()
{
    m_parameterControls.clear();
    m_masterStrengthSpin = nullptr;
    m_masterStrengthSlider = nullptr;
    m_scopeCombo = nullptr;
    m_scopeLabel = nullptr;
    m_resetScopeButton = nullptr;
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
