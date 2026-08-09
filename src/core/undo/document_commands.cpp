#include "core/undo/document_commands.h"

#include <utility>

namespace vt {

namespace {

constexpr int TextCommandId = 100;
constexpr int FontSizeCommandId = 101;
constexpr int TrackingCommandId = 102;
constexpr int LineSpacingCommandId = 106;
constexpr int EffectParameterCommandId = 103;
constexpr int DeformationStrengthCommandId = 104;
constexpr int EffectMasterStrengthCommandId = 105;

} // namespace

DocumentCommand::DocumentCommand(Document& document,
                                 DocumentChangeCallback onChanged,
                                 const QString& description)
    : QUndoCommand(description)
    , m_document(document)
    , m_onChanged(std::move(onChanged))
    , m_objectId(document.activeObjectId)
{
}

void DocumentCommand::notifyChanged()
{
    m_document.touchModified();
    if (m_onChanged) {
        m_onChanged();
    }
}

TextObject& DocumentCommand::targetObject()
{
    if (!m_objectId.isEmpty()) {
        if (TextObject* object = m_document.objectById(m_objectId)) {
            return *object;
        }
    }
    // Commands are bound to the object ID selected when they were created.
    // A missing target must never be redirected to whichever object is active
    // now.  The inert sink keeps legacy reference-returning commands harmless
    // until they can be retired in favour of pointer-returning commands.
    static TextObject missingTargetSink;
    return missingTargetSink;
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
    targetObject().sourceText = m_oldText;
    notifyChanged();
}

void SetTextCommand::redo()
{
    targetObject().sourceText = m_newText;
    notifyChanged();
}

int SetTextCommand::id() const
{
    return TextCommandId;
}

bool SetTextCommand::mergeWith(const QUndoCommand* other)
{
    const auto* command = dynamic_cast<const SetTextCommand*>(other);
    if (!command || command->m_objectId != m_objectId) {
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
    targetObject().font.family = m_oldFamily;
    notifyChanged();
}

void SetFontFamilyCommand::redo()
{
    targetObject().font.family = m_newFamily;
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
    targetObject().font.styleName = m_oldStyle;
    notifyChanged();
}

void SetFontStyleCommand::redo()
{
    targetObject().font.styleName = m_newStyle;
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
    targetObject().font.weight = m_oldWeight;
    notifyChanged();
}

void SetFontWeightCommand::redo()
{
    targetObject().font.weight = m_newWeight;
    notifyChanged();
}

SetFontItalicCommand::SetFontItalicCommand(Document& document,
                                           bool oldItalic,
                                           bool newItalic,
                                           DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Toggle italic"))
    , m_oldItalic(oldItalic)
    , m_newItalic(newItalic)
{
}

void SetFontItalicCommand::undo()
{
    targetObject().font.italic = m_oldItalic;
    notifyChanged();
}

void SetFontItalicCommand::redo()
{
    targetObject().font.italic = m_newItalic;
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
    targetObject().typography.fontSize = m_oldSize;
    notifyChanged();
}

void SetFontSizeCommand::redo()
{
    targetObject().typography.fontSize = m_newSize;
    notifyChanged();
}

int SetFontSizeCommand::id() const
{
    return FontSizeCommandId;
}

bool SetFontSizeCommand::mergeWith(const QUndoCommand* other)
{
    const auto* command = dynamic_cast<const SetFontSizeCommand*>(other);
    if (!command || command->m_objectId != m_objectId) {
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
    targetObject().typography.trackingEm = m_oldTrackingEm;
    notifyChanged();
}

void SetTrackingCommand::redo()
{
    targetObject().typography.trackingEm = m_newTrackingEm;
    notifyChanged();
}

int SetTrackingCommand::id() const
{
    return TrackingCommandId;
}

bool SetTrackingCommand::mergeWith(const QUndoCommand* other)
{
    const auto* command = dynamic_cast<const SetTrackingCommand*>(other);
    if (!command || command->m_objectId != m_objectId) {
        return false;
    }
    m_newTrackingEm = command->m_newTrackingEm;
    return true;
}

SetLineSpacingCommand::SetLineSpacingCommand(Document& document,
                                             qreal oldValue,
                                             qreal newValue,
                                             DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Change line spacing"))
    , m_oldValue(oldValue)
    , m_newValue(newValue)
{
}

void SetLineSpacingCommand::undo()
{
    targetObject().typography.lineSpacing = m_oldValue;
    notifyChanged();
}

void SetLineSpacingCommand::redo()
{
    targetObject().typography.lineSpacing = m_newValue;
    notifyChanged();
}

int SetLineSpacingCommand::id() const
{
    return LineSpacingCommandId;
}

bool SetLineSpacingCommand::mergeWith(const QUndoCommand* other)
{
    const auto* command = dynamic_cast<const SetLineSpacingCommand*>(other);
    if (!command || command->m_objectId != m_objectId) {
        return false;
    }
    m_newValue = command->m_newValue;
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
    targetObject().fill = m_oldColor;
    notifyChanged();
}

void SetFillColorCommand::redo()
{
    targetObject().fill = m_newColor;
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
    targetObject().effects.removeAt(m_index);
    notifyChanged();
}

void AddEffectCommand::redo()
{
    if (m_effect) {
        targetObject().effects.insert(m_index, m_effect->clone());
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
        targetObject().effects.insert(m_index, m_effect->clone());
        notifyChanged();
    }
}

void RemoveEffectCommand::redo()
{
    targetObject().effects.removeAt(m_index);
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
    targetObject().effects.move(m_to, m_from);
    notifyChanged();
}

void ReorderEffectCommand::redo()
{
    targetObject().effects.move(m_from, m_to);
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
    if (Effect* effect = targetObject().effects.at(m_index)) {
        effect->enabled = m_oldEnabled;
        notifyChanged();
    }
}

void SetEffectEnabledCommand::redo()
{
    if (Effect* effect = targetObject().effects.at(m_index)) {
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
    if (Effect* effect = targetObject().effects.at(m_index)) {
        if (effect->setParameter(m_parameterId, m_oldValue)) {
            notifyChanged();
        }
    }
}

void SetEffectParameterCommand::redo()
{
    if (Effect* effect = targetObject().effects.at(m_index)) {
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
    if (!command || command->m_objectId != m_objectId || command->m_index != m_index
        || command->m_parameterId != m_parameterId) {
        return false;
    }
    m_newValue = command->m_newValue;
    return true;
}

SetEffectMasterStrengthCommand::SetEffectMasterStrengthCommand(Document& document,
                                                               int index,
                                                               double oldValue,
                                                               double newValue,
                                                               DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Change effect strength"))
    , m_index(index)
    , m_oldValue(oldValue)
    , m_newValue(newValue)
{
}

void SetEffectMasterStrengthCommand::undo()
{
    if (Effect* effect = targetObject().effects.at(m_index)) {
        effect->masterStrength = m_oldValue;
        notifyChanged();
    }
}

void SetEffectMasterStrengthCommand::redo()
{
    if (Effect* effect = targetObject().effects.at(m_index)) {
        effect->masterStrength = m_newValue;
        notifyChanged();
    }
}

int SetEffectMasterStrengthCommand::id() const
{
    return EffectMasterStrengthCommandId;
}

bool SetEffectMasterStrengthCommand::mergeWith(const QUndoCommand* other)
{
    const auto* command = dynamic_cast<const SetEffectMasterStrengthCommand*>(other);
    if (!command || command->m_objectId != m_objectId || command->m_index != m_index) {
        return false;
    }
    m_newValue = command->m_newValue;
    return true;
}

SetEffectScopeCommand::SetEffectScopeCommand(Document& document,
                                             int index,
                                             EffectScope oldScope,
                                             EffectScope newScope,
                                             DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Change effect scope"))
    , m_index(index)
    , m_oldScope(std::move(oldScope))
    , m_newScope(std::move(newScope))
{
}

void SetEffectScopeCommand::undo()
{
    if (Effect* effect = targetObject().effects.at(m_index)) {
        effect->scope = m_oldScope;
        notifyChanged();
    }
}

void SetEffectScopeCommand::redo()
{
    if (Effect* effect = targetObject().effects.at(m_index)) {
        effect->scope = m_newScope;
        notifyChanged();
    }
}

AddEffectMaskStrokeCommand::AddEffectMaskStrokeCommand(Document& document,
                                                       QString objectId,
                                                       QString effectId,
                                                       EffectMaskStroke stroke,
                                                       DocumentChangeCallback onChanged,
                                                       QString description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_objectId(std::move(objectId))
    , m_effectId(std::move(effectId))
    , m_stroke(std::move(stroke))
{
}

void AddEffectMaskStrokeCommand::undo()
{
    TextObject* object = m_document.objectById(m_objectId);
    if (!object) {
        return;
    }
    Effect* effect = object->effects.byInstanceId(m_effectId);
    if (!effect || effect->maskStrokes.isEmpty()) {
        return;
    }
    effect->maskStrokes.removeLast();
    notifyChanged();
}

void AddEffectMaskStrokeCommand::redo()
{
    TextObject* object = m_document.objectById(m_objectId);
    if (!object) {
        return;
    }
    Effect* effect = object->effects.byInstanceId(m_effectId);
    if (!effect) {
        return;
    }
    effect->maskStrokes.push_back(m_stroke);
    notifyChanged();
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
    targetObject().effects = m_before;
    notifyChanged();
}

void ApplyPresetCommand::redo()
{
    targetObject().effects = m_after;
    notifyChanged();
}

AddDeformationStrokeCommand::AddDeformationStrokeCommand(Document& document,
                                                         QString objectId,
                                                         int index,
                                                         DeformationStroke stroke,
                                                         DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Add deformation stroke"))
    , m_targetObjectId(std::move(objectId))
    , m_index(index)
    , m_stroke(std::move(stroke))
{
}

void AddDeformationStrokeCommand::undo()
{
    TextObject* object = m_document.objectById(m_targetObjectId);
    if (!object) {
        return;
    }
    auto& strokes = object->deformation.strokes;
    if (m_index >= 0 && m_index < strokes.size()) {
        strokes.removeAt(m_index);
        notifyChanged();
    }
}

void AddDeformationStrokeCommand::redo()
{
    TextObject* object = m_document.objectById(m_targetObjectId);
    if (!object) {
        return;
    }
    auto& strokes = object->deformation.strokes;
    if (m_index < 0) {
        m_index = strokes.size();
    }
    if (m_index <= strokes.size()) {
        strokes.insert(m_index, m_stroke);
        notifyChanged();
    }
}

ClearDeformationCommand::ClearDeformationCommand(Document& document,
                                                 ManualDeformation before,
                                                 DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Clear deformation"))
    , m_before(std::move(before))
    , m_after(m_before)
{
    m_after.strokes.clear();
}

void ClearDeformationCommand::undo()
{
    targetObject().deformation = m_before;
    notifyChanged();
}

void ClearDeformationCommand::redo()
{
    targetObject().deformation = m_after;
    notifyChanged();
}

SetDeformationEnabledCommand::SetDeformationEnabledCommand(Document& document,
                                                           bool oldEnabled,
                                                           bool newEnabled,
                                                           DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Toggle deformation"))
    , m_oldEnabled(oldEnabled)
    , m_newEnabled(newEnabled)
{
}

void SetDeformationEnabledCommand::undo()
{
    targetObject().deformation.enabled = m_oldEnabled;
    notifyChanged();
}

void SetDeformationEnabledCommand::redo()
{
    targetObject().deformation.enabled = m_newEnabled;
    notifyChanged();
}

SetDeformationStrengthCommand::SetDeformationStrengthCommand(Document& document,
                                                             qreal oldStrength,
                                                             qreal newStrength,
                                                             DocumentChangeCallback onChanged)
    : DocumentCommand(document, std::move(onChanged), QStringLiteral("Change deformation strength"))
    , m_oldStrength(oldStrength)
    , m_newStrength(newStrength)
{
}

void SetDeformationStrengthCommand::undo()
{
    targetObject().deformation.strength = m_oldStrength;
    notifyChanged();
}

void SetDeformationStrengthCommand::redo()
{
    targetObject().deformation.strength = m_newStrength;
    notifyChanged();
}

int SetDeformationStrengthCommand::id() const
{
    return DeformationStrengthCommandId;
}

bool SetDeformationStrengthCommand::mergeWith(const QUndoCommand* other)
{
    const auto* command = dynamic_cast<const SetDeformationStrengthCommand*>(other);
    if (!command || command->m_objectId != m_objectId) {
        return false;
    }
    m_newStrength = command->m_newStrength;
    return true;
}

} // namespace vt
