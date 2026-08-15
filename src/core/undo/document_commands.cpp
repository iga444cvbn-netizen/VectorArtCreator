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
constexpr int EffectStackStrengthCommandId = 107;
constexpr int PathOffsetCommandId = 108;
constexpr int TypographyLayoutCommandId = 109;

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
    TextObject& object = targetObject();
    for (int index = 0; index < object.effects.size(); ++index) {
        const Effect* effect = object.effects.at(index);
        if (!effect || effect->scope.kind != EffectScopeKind::TextRange) continue;
        m_oldScopes.push_back({effect->instanceId, effect->scope});
        m_newScopes.push_back({effect->instanceId, TextRangeRebaser::rebase(effect->scope, m_oldText, m_newText)});
    }
}

namespace {
void applyScopes(TextObject& object, const QVector<QPair<QString, EffectScope>>& scopes)
{
    for (const auto& [id, scope] : scopes) {
        if (Effect* effect = object.effects.byInstanceId(id)) effect->scope = scope;
    }
}
} // namespace

void SetTextCommand::undo()
{
    targetObject().sourceText = m_oldText;
    applyScopes(targetObject(), m_oldScopes);
    notifyChanged();
}

void SetTextCommand::redo()
{
    targetObject().sourceText = m_newText;
    applyScopes(targetObject(), m_newScopes);
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
    m_newScopes = command->m_newScopes;
    bool scopesMatch = m_oldScopes.size() == m_newScopes.size();
    for (int index = 0; scopesMatch && index < m_oldScopes.size(); ++index) {
        const auto& left = m_oldScopes.at(index);
        const auto& right = m_newScopes.at(index);
        scopesMatch = left.first == right.first && left.second.kind == right.second.kind
            && left.second.start == right.second.start && left.second.end == right.second.end;
    }
    setObsolete(m_newText == m_oldText && scopesMatch);
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

SetFontDescriptorCommand::SetFontDescriptorCommand(Document& document,
                                                   FontDescriptor oldFont,
                                                   FontDescriptor newFont,
                                                   DocumentChangeCallback onChanged,
                                                   QString description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_oldFont(std::move(oldFont))
    , m_newFont(std::move(newFont))
{
}

void SetFontDescriptorCommand::apply(const FontDescriptor& font)
{
    targetObject().font = font;
    notifyChanged();
}

void SetFontDescriptorCommand::undo() { apply(m_oldFont); }
void SetFontDescriptorCommand::redo() { apply(m_newFont); }

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

SetFontDecorationCommand::SetFontDecorationCommand(Document& document,
                                                     FontDecoration decoration,
                                                     bool oldValue,
                                                     bool newValue,
                                                     DocumentChangeCallback onChanged)
    : DocumentCommand(document,
                      std::move(onChanged),
                      decoration == FontDecoration::Underline
                          ? QStringLiteral("Toggle underline")
                          : QStringLiteral("Toggle strikeout"))
    , m_decoration(decoration)
    , m_oldValue(oldValue)
    , m_newValue(newValue)
{
}

void SetFontDecorationCommand::apply(bool value)
{
    if (m_decoration == FontDecoration::Underline) {
        targetObject().font.underline = value;
    } else {
        targetObject().font.strikeOut = value;
    }
    notifyChanged();
}

void SetFontDecorationCommand::undo() { apply(m_oldValue); }
void SetFontDecorationCommand::redo() { apply(m_newValue); }

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
    setObsolete(m_newSize == m_oldSize);
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
    setObsolete(m_newTrackingEm == m_oldTrackingEm);
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
    setObsolete(m_newValue == m_oldValue);
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
    setObsolete(m_newValue == m_oldValue);
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
    setObsolete(m_newValue == m_oldValue);
    return true;
}

SetEffectStackStrengthCommand::SetEffectStackStrengthCommand(Document& document,
                                                             QString objectId,
                                                             qreal oldValue,
                                                             qreal newValue,
                                                             quint64 mergeToken,
                                                             DocumentChangeCallback onChanged)
    : QUndoCommand(QStringLiteral("Change style intensity"))
    , m_oldValue(oldValue), m_newValue(newValue)
    , m_mergeToken(mergeToken)
    , m_document(document), m_objectId(std::move(objectId)), m_onChanged(std::move(onChanged))
{
}

void SetEffectStackStrengthCommand::undo()
{
    if (TextObject* object = m_document.objectById(m_objectId)) {
        object->effectStackStrength = m_oldValue;
        m_document.touchModified();
        if (m_onChanged) m_onChanged();
    }
}

void SetEffectStackStrengthCommand::redo()
{
    if (TextObject* object = m_document.objectById(m_objectId)) {
        object->effectStackStrength = m_newValue;
        m_document.touchModified();
        if (m_onChanged) m_onChanged();
    }
}

int SetEffectStackStrengthCommand::id() const
{
    // Non-gesture edits remain independent. A nonzero token belongs to one
    // explicit slider transaction and cannot merge with a later gesture.
    return m_mergeToken == 0 ? -1 : EffectStackStrengthCommandId;
}

bool SetEffectStackStrengthCommand::mergeWith(const QUndoCommand* other)
{
    const auto* command = dynamic_cast<const SetEffectStackStrengthCommand*>(other);
    if (!command || m_mergeToken == 0 || command->m_mergeToken != m_mergeToken
        || command->m_objectId != m_objectId) {
        return false;
    }
    m_newValue = command->m_newValue;
    setObsolete(m_newValue == m_oldValue);
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
                                       const QString& description,
                                       qreal beforeStackStrength,
                                       qreal afterStackStrength)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_beforeStackStrength(beforeStackStrength)
    , m_afterStackStrength(afterStackStrength)
{
}

void ApplyPresetCommand::undo()
{
    targetObject().effects = m_before;
    targetObject().effectStackStrength = m_beforeStackStrength;
    notifyChanged();
}

void ApplyPresetCommand::redo()
{
    targetObject().effects = m_after;
    targetObject().effectStackStrength = m_afterStackStrength;
    notifyChanged();
}

ApplyPresetToObjectsCommand::ApplyPresetToObjectsCommand(Document& document,
                                                         QVector<Target> targets,
                                                         DocumentChangeCallback onChanged,
                                                         const QString& description)
    : QUndoCommand(description)
    , m_document(document)
    , m_targets(std::move(targets))
    , m_onChanged(std::move(onChanged))
{
}

void ApplyPresetToObjectsCommand::apply(bool after)
{
    bool changed = false;
    for (const Target& target : m_targets) {
        TextObject* object = m_document.objectById(target.objectId);
        if (!object) continue;
        object->effects = after ? target.afterEffects : target.beforeEffects;
        object->effectStackStrength = after ? target.afterStackStrength : target.beforeStackStrength;
        changed = true;
    }
    if (changed) {
        m_document.touchModified();
        if (m_onChanged) m_onChanged();
    }
}

void ApplyPresetToObjectsCommand::undo() { apply(false); }
void ApplyPresetToObjectsCommand::redo() { apply(true); }

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
    setObsolete(m_newStrength == m_oldStrength);
    return true;
}

SetPathTypographyCommand::SetPathTypographyCommand(
    Document& document,
    QString objectId,
    std::optional<PathGeometry> oldPath,
    PathTypographyProperties oldLayout,
    std::optional<PathGeometry> newPath,
    PathTypographyProperties newLayout,
    DocumentChangeCallback onChanged,
    QString description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_oldPath(std::move(oldPath))
    , m_oldLayout(std::move(oldLayout))
    , m_newPath(std::move(newPath))
    , m_newLayout(std::move(newLayout))
{
    m_objectId = std::move(objectId);
}

void SetPathTypographyCommand::apply(const std::optional<PathGeometry>& path,
                                     const PathTypographyProperties& layout)
{
    TextObject* object = m_document.objectById(m_objectId);
    if (!object) {
        return;
    }
    object->path = path;
    object->pathLayout = layout;
    notifyChanged();
}

void SetPathTypographyCommand::undo()
{
    apply(m_oldPath, m_oldLayout);
}

void SetPathTypographyCommand::redo()
{
    apply(m_newPath, m_newLayout);
}

SetPathOffsetCommand::SetPathOffsetCommand(Document& document,
                                           QString objectId,
                                           PathOffsetProperty property,
                                           qreal oldValue,
                                           qreal newValue,
                                           quint64 mergeToken,
                                           DocumentChangeCallback onChanged,
                                           QString description)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_property(property)
    , m_oldValue(oldValue)
    , m_newValue(newValue)
    , m_mergeToken(mergeToken)
{
    m_objectId = std::move(objectId);
}

void SetPathOffsetCommand::apply(qreal value)
{
    TextObject* object = m_document.objectById(m_objectId);
    if (!object) {
        return;
    }
    if (m_property == PathOffsetProperty::Start) {
        object->pathLayout.startOffset = value;
    } else {
        object->pathLayout.baselineOffset = value;
    }
    notifyChanged();
}

void SetPathOffsetCommand::undo()
{
    apply(m_oldValue);
}

void SetPathOffsetCommand::redo()
{
    apply(m_newValue);
}

int SetPathOffsetCommand::id() const
{
    // A zero token is a standalone numeric edit. Only an explicitly opened
    // physical gesture may merge its successive values.
    return m_mergeToken == 0 ? -1 : PathOffsetCommandId;
}

bool SetPathOffsetCommand::mergeWith(const QUndoCommand* other)
{
    const auto* command = dynamic_cast<const SetPathOffsetCommand*>(other);
    if (!command || m_mergeToken == 0 || command->m_mergeToken != m_mergeToken
        || command->m_objectId != m_objectId || command->m_property != m_property) {
        return false;
    }
    m_newValue = command->m_newValue;
    setObsolete(m_newValue == m_oldValue);
    return true;
}

SetTypographyLayoutCommand::SetTypographyLayoutCommand(
    Document& document,
    QString objectId,
    TypographyLayoutMode oldMode,
    std::optional<PathGeometry> oldPath,
    PathTypographyProperties oldPathLayout,
    std::optional<TypographyRegion> oldRegion,
    RegionTypographyProperties oldRegionLayout,
    TypographyLayoutMode newMode,
    std::optional<PathGeometry> newPath,
    PathTypographyProperties newPathLayout,
    std::optional<TypographyRegion> newRegion,
    RegionTypographyProperties newRegionLayout,
    DocumentChangeCallback onChanged,
    QString description,
    quint64 mergeToken)
    : DocumentCommand(document, std::move(onChanged), description)
    , m_oldMode(oldMode)
    , m_oldPath(std::move(oldPath))
    , m_oldPathLayout(std::move(oldPathLayout))
    , m_oldRegion(std::move(oldRegion))
    , m_oldRegionLayout(std::move(oldRegionLayout))
    , m_newMode(newMode)
    , m_newPath(std::move(newPath))
    , m_newPathLayout(std::move(newPathLayout))
    , m_newRegion(std::move(newRegion))
    , m_newRegionLayout(std::move(newRegionLayout))
    , m_mergeToken(mergeToken)
{
    m_objectId = std::move(objectId);
}

void SetTypographyLayoutCommand::apply(
    TypographyLayoutMode mode,
    const std::optional<PathGeometry>& path,
    const PathTypographyProperties& pathLayout,
    const std::optional<TypographyRegion>& region,
    const RegionTypographyProperties& regionLayout)
{
    TextObject* object = m_document.objectById(m_objectId);
    if (!object) return;
    object->layoutMode = mode;
    object->path = path;
    object->pathLayout = pathLayout;
    object->region = region;
    object->regionLayout = regionLayout;
    notifyChanged();
}

void SetTypographyLayoutCommand::undo()
{
    apply(m_oldMode, m_oldPath, m_oldPathLayout, m_oldRegion, m_oldRegionLayout);
}

void SetTypographyLayoutCommand::redo()
{
    apply(m_newMode, m_newPath, m_newPathLayout, m_newRegion, m_newRegionLayout);
}

int SetTypographyLayoutCommand::id() const
{
    return m_mergeToken == 0 ? -1 : TypographyLayoutCommandId;
}

bool SetTypographyLayoutCommand::mergeWith(const QUndoCommand* other)
{
    const auto* command = dynamic_cast<const SetTypographyLayoutCommand*>(other);
    if (!command || m_mergeToken == 0 || command->m_mergeToken != m_mergeToken
        || command->m_objectId != m_objectId) {
        return false;
    }
    m_newMode = command->m_newMode;
    m_newPath = command->m_newPath;
    m_newPathLayout = command->m_newPathLayout;
    m_newRegion = command->m_newRegion;
    m_newRegionLayout = command->m_newRegionLayout;
    setObsolete(m_newMode == m_oldMode
                && m_newPath == m_oldPath
                && m_newPathLayout == m_oldPathLayout
                && m_newRegion == m_oldRegion
                && m_newRegionLayout == m_oldRegionLayout);
    return true;
}

} // namespace vt
