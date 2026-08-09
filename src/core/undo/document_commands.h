#pragma once

#include "core/document/document.h"

#include <QColor>
#include <QString>
#include <QUndoCommand>

#include <functional>
#include <memory>

namespace vt {

using DocumentChangeCallback = std::function<void()>;

class DocumentCommand : public QUndoCommand {
public:
    DocumentCommand(Document& document,
                    DocumentChangeCallback onChanged,
                    const QString& description);

protected:
    void notifyChanged();
    [[nodiscard]] TextObject& targetObject();

    Document& m_document;
    DocumentChangeCallback m_onChanged;
    QString m_objectId;
};

class SetTextCommand final : public DocumentCommand {
public:
    SetTextCommand(Document& document,
                   QString oldText,
                   QString newText,
                   DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;
    [[nodiscard]] int id() const override;
    bool mergeWith(const QUndoCommand* other) override;

private:
    QString m_oldText;
    QString m_newText;
};

class SetFontFamilyCommand final : public DocumentCommand {
public:
    SetFontFamilyCommand(Document& document,
                         QString oldFamily,
                         QString newFamily,
                         DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;

private:
    QString m_oldFamily;
    QString m_newFamily;
};

class SetFontStyleCommand final : public DocumentCommand {
public:
    SetFontStyleCommand(Document& document,
                        QString oldStyle,
                        QString newStyle,
                        DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;

private:
    QString m_oldStyle;
    QString m_newStyle;
};

class SetFontWeightCommand final : public DocumentCommand {
public:
    SetFontWeightCommand(Document& document,
                         int oldWeight,
                         int newWeight,
                         DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;

private:
    int m_oldWeight = 0;
    int m_newWeight = 0;
};

class SetFontSizeCommand final : public DocumentCommand {
public:
    SetFontSizeCommand(Document& document,
                       qreal oldSize,
                       qreal newSize,
                       DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;
    [[nodiscard]] int id() const override;
    bool mergeWith(const QUndoCommand* other) override;

private:
    qreal m_oldSize = 0.0;
    qreal m_newSize = 0.0;
};

class SetTrackingCommand final : public DocumentCommand {
public:
    SetTrackingCommand(Document& document,
                       qreal oldTrackingEm,
                       qreal newTrackingEm,
                       DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;
    [[nodiscard]] int id() const override;
    bool mergeWith(const QUndoCommand* other) override;

private:
    qreal m_oldTrackingEm = 0.0;
    qreal m_newTrackingEm = 0.0;
};

class SetLineSpacingCommand final : public DocumentCommand {
public:
    SetLineSpacingCommand(Document& document,
                          qreal oldValue,
                          qreal newValue,
                          DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;
    [[nodiscard]] int id() const override;
    bool mergeWith(const QUndoCommand* other) override;

private:
    qreal m_oldValue = 1.0;
    qreal m_newValue = 1.0;
};

class SetFillColorCommand final : public DocumentCommand {
public:
    SetFillColorCommand(Document& document,
                        QColor oldColor,
                        QColor newColor,
                        DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;

private:
    QColor m_oldColor;
    QColor m_newColor;
};

class AddEffectCommand final : public DocumentCommand {
public:
    AddEffectCommand(Document& document,
                     int index,
                     std::unique_ptr<Effect> effect,
                     DocumentChangeCallback onChanged,
                     const QString& description);

    void undo() override;
    void redo() override;

private:
    int m_index = -1;
    std::unique_ptr<Effect> m_effect;
};

class RemoveEffectCommand final : public DocumentCommand {
public:
    RemoveEffectCommand(Document& document,
                        int index,
                        std::unique_ptr<Effect> effect,
                        DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;

private:
    int m_index = -1;
    std::unique_ptr<Effect> m_effect;
};

class ReorderEffectCommand final : public DocumentCommand {
public:
    ReorderEffectCommand(Document& document,
                         int from,
                         int to,
                         DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;

private:
    int m_from = -1;
    int m_to = -1;
};

class SetEffectEnabledCommand final : public DocumentCommand {
public:
    SetEffectEnabledCommand(Document& document,
                            int index,
                            bool oldEnabled,
                            bool newEnabled,
                            DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;

private:
    int m_index = -1;
    bool m_oldEnabled = true;
    bool m_newEnabled = true;
};

class SetEffectParameterCommand final : public DocumentCommand {
public:
    SetEffectParameterCommand(Document& document,
                              int index,
                              QString parameterId,
                              double oldValue,
                              double newValue,
                              DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;
    [[nodiscard]] int id() const override;
    bool mergeWith(const QUndoCommand* other) override;

private:
    int m_index = -1;
    QString m_parameterId;
    double m_oldValue = 0.0;
    double m_newValue = 0.0;
};

class SetEffectMasterStrengthCommand final : public DocumentCommand {
public:
    SetEffectMasterStrengthCommand(Document& document,
                                   int index,
                                   double oldValue,
                                   double newValue,
                                   DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;
    [[nodiscard]] int id() const override;
    bool mergeWith(const QUndoCommand* other) override;

private:
    int m_index = -1;
    double m_oldValue = 1.0;
    double m_newValue = 1.0;
};

class SetEffectScopeCommand final : public DocumentCommand {
public:
    SetEffectScopeCommand(Document& document,
                          int index,
                          EffectScope oldScope,
                          EffectScope newScope,
                          DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;

private:
    int m_index = -1;
    EffectScope m_oldScope;
    EffectScope m_newScope;
};

class AddEffectMaskStrokeCommand final : public DocumentCommand {
public:
    AddEffectMaskStrokeCommand(Document& document,
                               QString objectId,
                               QString effectId,
                               EffectMaskStroke stroke,
                               DocumentChangeCallback onChanged,
                               QString description = QStringLiteral("Paint effect mask"));

    void undo() override;
    void redo() override;

private:
    QString m_objectId;
    QString m_effectId;
    EffectMaskStroke m_stroke;
};

class ApplyPresetCommand final : public DocumentCommand {
public:
    ApplyPresetCommand(Document& document,
                       EffectStack before,
                       EffectStack after,
                       DocumentChangeCallback onChanged,
                       const QString& description);

    void undo() override;
    void redo() override;

private:
    EffectStack m_before;
    EffectStack m_after;
};

class AddDeformationStrokeCommand final : public DocumentCommand {
public:
    AddDeformationStrokeCommand(Document& document,
                                int index,
                                DeformationStroke stroke,
                                DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;

private:
    int m_index = -1;
    DeformationStroke m_stroke;
};

class ClearDeformationCommand final : public DocumentCommand {
public:
    ClearDeformationCommand(Document& document,
                            ManualDeformation before,
                            DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;

private:
    ManualDeformation m_before;
    ManualDeformation m_after;
};

class SetDeformationEnabledCommand final : public DocumentCommand {
public:
    SetDeformationEnabledCommand(Document& document,
                                 bool oldEnabled,
                                 bool newEnabled,
                                 DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;

private:
    bool m_oldEnabled = true;
    bool m_newEnabled = true;
};

class SetDeformationStrengthCommand final : public DocumentCommand {
public:
    SetDeformationStrengthCommand(Document& document,
                                  qreal oldStrength,
                                  qreal newStrength,
                                  DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;
    [[nodiscard]] int id() const override;
    bool mergeWith(const QUndoCommand* other) override;

private:
    qreal m_oldStrength = 1.0;
    qreal m_newStrength = 1.0;
};

} // namespace vt
