#include "core/undo/document_commands.h"

#include <utility>

namespace vt {

namespace {

constexpr int TextCommandId = 100;
constexpr int FontSizeCommandId = 101;
constexpr int TrackingCommandId = 102;
constexpr int EffectParameterCommandId = 103;

} // namespace

DocumentCommand::DocumentCommand(Document& document,
                                 DocumentChangeCallback onChanged,
                                 const QString& description)
    : QUndoCommand(description)
    , m_document(document)
    , m_onChanged(std::move(onChanged))
{
}

void DocumentCommand::notifyChanged()
{
    m_document.touchModified();
    if (m_onChanged) {
        m_onChanged();
    }
}

SetTextCommand::SetTextCommand(Document& document,
                               QString oldText,
                               QString newText,
                               DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Change text"))
    , m_oldText(std::move(oldText))
    , m_newText(std::move(newText))
{
}

void SetTextCommand::undo()
{
    m_document.primaryTextObject().sourceText = m_oldText;
    notifyChanged();
}

void SetTextCommand::redo()
{
    m_document.primaryTextObject().sourceText = m_newText;
    notifyChanged();
}

int SetTextCommand::id() const
{
    return TextCommandId;
}

bool SetTextCommand::mergeWith(const QUndoCommand* other)
{
    const auto* command = dynamic_cast<const SetTextCommand*>(other);
    if (!command) {
        return false;
    }
    m_newText = command->m_newText;
    return true;
}

SetFontFamilyCommand::SetFontFamilyCommand(Document& document,
                                           QString oldFamily,
                                           QString newFamily,
                                           DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Change font family"))
    , m_oldFamily(std::move(oldFamily))
    , m_newFamily(std::move(newFamily))
{
}

void SetFontFamilyCommand::undo()
{
    m_document.primaryTextObject().font.family = m_oldFamily;
    notifyChanged();
}

void SetFontFamilyCommand::redo()
{
    m_document.primaryTextObject().font.family = m_newFamily;
    notifyChanged();
}

SetFontStyleCommand::SetFontStyleCommand(Document& document,
                                         QString oldStyle,
                                         QString newStyle,
                                         DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Change font style"))
    , m_oldStyle(std::move(oldStyle))
    , m_newStyle(std::move(newStyle))
{
}

void SetFontStyleCommand::undo()
{
    m_document.primaryTextObject().font.styleName = m_oldStyle;
    notifyChanged();
}

void SetFontStyleCommand::redo()
{
    m_document.primaryTextObject().font.styleName = m_newStyle;
    notifyChanged();
}

SetFontWeightCommand::SetFontWeightCommand(Document& document,
                                           int oldWeight,
                                           int newWeight,
                                           DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Change font weight"))
    , m_oldWeight(oldWeight)
    , m_newWeight(newWeight)
{
}

void SetFontWeightCommand::undo()
{
    m_document.primaryTextObject().font.weight = m_oldWeight;
    notifyChanged();
}

void SetFontWeightCommand::redo()
{
    m_document.primaryTextObject().font.weight = m_newWeight;
    notifyChanged();
}

SetFontSizeCommand::SetFontSizeCommand(Document& document,
                                       qreal oldSize,
                                       qreal newSize,
                                       DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Change font size"))
    , m_oldSize(oldSize)
    , m_newSize(newSize)
{
}

void SetFontSizeCommand::undo()
{
    m_document.primaryTextObject().typography.fontSize = m_oldSize;
    notifyChanged();
}

void SetFontSizeCommand::redo()
{
    m_document.primaryTextObject().typography.fontSize = m_newSize;
    notifyChanged();
}

int SetFontSizeCommand::id() const
{
    return FontSizeCommandId;
}

bool SetFontSizeCommand::mergeWith(const QUndoCommand* other)
{
    const auto* command = dynamic_cast<const SetFontSizeCommand*>(other);
    if (!command) {
        return false;
    }
    m_newSize = command->m_newSize;
    return true;
}

SetTrackingCommand::SetTrackingCommand(Document& document,
                                       qreal oldTrackingEm,
                                       qreal newTrackingEm,
                                       DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Change tracking"))
    , m_oldTrackingEm(oldTrackingEm)
    , m_newTrackingEm(newTrackingEm)
{
}

void SetTrackingCommand::undo()
{
    m_document.primaryTextObject().typography.trackingEm = m_oldTrackingEm;
    notifyChanged();
}

void SetTrackingCommand::redo()
{
    m_document.primaryTextObject().typography.trackingEm = m_newTrackingEm;
    notifyChanged();
}

int SetTrackingCommand::id() const
{
    return TrackingCommandId;
}

bool SetTrackingCommand::mergeWith(const QUndoCommand* other)
{
    const auto* command = dynamic_cast<const SetTrackingCommand*>(other);
    if (!command) {
        return false;
    }
    m_newTrackingEm = command->m_newTrackingEm;
    return true;
}

SetFillColorCommand::SetFillColorCommand(Document& document,
                                         QColor oldColor,
                                         QColor newColor,
                                         DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Change fill color"))
    , m_oldColor(std::move(oldColor))
    , m_newColor(std::move(newColor))
{
}

void SetFillColorCommand::undo()
{
    m_document.primaryTextObject().fill = m_oldColor;
    notifyChanged();
}

void SetFillColorCommand::redo()
{
    m_document.primaryTextObject().fill = m_newColor;
    notifyChanged();
}

AddEffectCommand::AddEffectCommand(Document& document,
                                   int index,
                                   std::unique_ptr<Effect> effect,
                                   DocumentChangeCallback onChanged,
                                   const QString& description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_index(index)
    , m_effect(std::move(effect))
{
}

void AddEffectCommand::undo()
{
    m_document.primaryTextObject().effects.removeAt(m_index);
    notifyChanged();
}

void AddEffectCommand::redo()
{
    if (m_effect) {
        m_document.primaryTextObject().effects.insert(m_index, m_effect->clone());
        notifyChanged();
    }
}

RemoveEffectCommand::RemoveEffectCommand(Document& document,
                                         int index,
                                         std::unique_ptr<Effect> effect,
                                         DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Remove effect"))
    , m_index(index)
    , m_effect(std::move(effect))
{
}

void RemoveEffectCommand::undo()
{
    if (m_effect) {
        m_document.primaryTextObject().effects.insert(m_index, m_effect->clone());
        notifyChanged();
    }
}

void RemoveEffectCommand::redo()
{
    m_document.primaryTextObject().effects.removeAt(m_index);
    notifyChanged();
}

ReorderEffectCommand::ReorderEffectCommand(Document& document,
                                           int from,
                                           int to,
                                           DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Reorder effects"))
    , m_from(from)
    , m_to(to)
{
}

void ReorderEffectCommand::undo()
{
    m_document.primaryTextObject().effects.move(m_to, m_from);
    notifyChanged();
}

void ReorderEffectCommand::redo()
{
    m_document.primaryTextObject().effects.move(m_from, m_to);
    notifyChanged();
}

SetEffectEnabledCommand::SetEffectEnabledCommand(Document& document,
                                                 int index,
                                                 bool oldEnabled,
                                                 bool newEnabled,
                                                 DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Toggle effect"))
    , m_index(index)
    , m_oldEnabled(oldEnabled)
    , m_newEnabled(newEnabled)
{
}

void SetEffectEnabledCommand::undo()
{
    if (Effect* effect = m_document.primaryTextObject().effects.at(m_index)) {
        effect->enabled = m_oldEnabled;
        notifyChanged();
    }
}

void SetEffectEnabledCommand::redo()
{
    if (Effect* effect = m_document.primaryTextObject().effects.at(m_index)) {
        effect->enabled = m_newEnabled;
        notifyChanged();
    }
}

SetEffectParameterCommand::SetEffectParameterCommand(Document& document,
                                                     int index,
                                                     QString parameterId,
                                                     double oldValue,
                                                     double newValue,
                                                     DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Change effect parameter"))
    , m_index(index)
    , m_parameterId(std::move(parameterId))
    , m_oldValue(oldValue)
    , m_newValue(newValue)
{
}

void SetEffectParameterCommand::undo()
{
    if (Effect* effect = m_document.primaryTextObject().effects.at(m_index)) {
        if (effect->setParameter(m_parameterId, m_oldValue)) {
            notifyChanged();
        }
    }
}

void SetEffectParameterCommand::redo()
{
    if (Effect* effect = m_document.primaryTextObject().effects.at(m_index)) {
        if (effect->setParameter(m_parameterId, m_newValue)) {
            notifyChanged();
        }
    }
}

int SetEffectParameterCommand::id() const
{
    return EffectParameterCommandId;
}

bool SetEffectParameterCommand::mergeWith(const QUndoCommand* other)
{
    const auto* command = dynamic_cast<const SetEffectParameterCommand*>(other);
    if (!command || command->m_index != m_index || command->m_parameterId != m_parameterId) {
        return false;
    }
    m_newValue = command->m_newValue;
    return true;
}

ApplyPresetCommand::ApplyPresetCommand(Document& document,
                                       EffectStack before,
                                       EffectStack after,
                                       DocumentChangeCallback onChanged,
                                       const QString& description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

void ApplyPresetCommand::undo()
{
    m_document.primaryTextObject().effects = m_before;
    notifyChanged();
}

void ApplyPresetCommand::redo()
{
    m_document.primaryTextObject().effects = m_after;
    notifyChanged();
}

} // namespace vt
