#pragma once

#include "core/document/document.h"
#include "core/effects/text_range_rebaser.h"

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
    QVector<QPair<QString, EffectScope>> m_oldScopes;
    QVector<QPair<QString, EffectScope>> m_newScopes;
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

class SetFontDescriptorCommand final : public DocumentCommand {
public:
    SetFontDescriptorCommand(Document& document,
                             FontDescriptor oldFont,
                             FontDescriptor newFont,
                             DocumentChangeCallback onChanged,
                             QString description = QStringLiteral("Change font"));

    void undo() override;
    void redo() override;

private:
    void apply(const FontDescriptor& font);
    FontDescriptor m_oldFont;
    FontDescriptor m_newFont;
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

class SetFontItalicCommand final : public DocumentCommand {
public:
    SetFontItalicCommand(Document& document,
                         bool oldItalic,
                         bool newItalic,
                         DocumentChangeCallback onChanged);
    void undo() override;
    void redo() override;

private:
    bool m_oldItalic = false;
    bool m_newItalic = false;
};

enum class FontDecoration {
    Underline,
    StrikeOut,
};

class SetFontDecorationCommand final : public DocumentCommand {
public:
    SetFontDecorationCommand(Document& document,
                             FontDecoration decoration,
                             bool oldValue,
                             bool newValue,
                             DocumentChangeCallback onChanged);
    void undo() override;
    void redo() override;

private:
    void apply(bool value);

    FontDecoration m_decoration;
    bool m_oldValue = false;
    bool m_newValue = false;
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

class SetEffectStackStrengthCommand final : public QUndoCommand {
public:
    SetEffectStackStrengthCommand(Document& document, QString objectId, qreal oldValue,
                                  qreal newValue, quint64 mergeToken,
                                  DocumentChangeCallback onChanged);
    void undo() override;
    void redo() override;
    [[nodiscard]] int id() const override;
    bool mergeWith(const QUndoCommand* other) override;
private:
    qreal m_oldValue = 1.0;
    qreal m_newValue = 1.0;
    quint64 m_mergeToken = 0;
    Document& m_document;
    QString m_objectId;
    DocumentChangeCallback m_onChanged;
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
                       const QString& description,
                       qreal beforeStackStrength = 1.0,
                       qreal afterStackStrength = 1.0);

    void undo() override;
    void redo() override;

private:
    EffectStack m_before;
    EffectStack m_after;
    qreal m_beforeStackStrength = 1.0;
    qreal m_afterStackStrength = 1.0;
};

class ApplyPresetToObjectsCommand final : public QUndoCommand {
public:
    struct Target {
        QString objectId;
        EffectStack beforeEffects;
        EffectStack afterEffects;
        qreal beforeStackStrength = 1.0;
        qreal afterStackStrength = 1.0;
    };

    ApplyPresetToObjectsCommand(Document& document, QVector<Target> targets,
                                DocumentChangeCallback onChanged, const QString& description);
    void undo() override;
    void redo() override;

private:
    void apply(bool after);
    Document& m_document;
    QVector<Target> m_targets;
    DocumentChangeCallback m_onChanged;
};

class AddDeformationStrokeCommand final : public DocumentCommand {
public:
    AddDeformationStrokeCommand(Document& document,
                                QString objectId,
                                int index,
                                DeformationStroke stroke,
                                DocumentChangeCallback onChanged);

    void undo() override;
    void redo() override;

private:
    QString m_targetObjectId;
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

class SetPathTypographyCommand final : public DocumentCommand {
public:
    SetPathTypographyCommand(Document& document,
                             QString objectId,
                             std::optional<PathGeometry> oldPath,
                             PathTypographyProperties oldLayout,
                             std::optional<PathGeometry> newPath,
                             PathTypographyProperties newLayout,
                             DocumentChangeCallback onChanged,
                             QString description = QStringLiteral("Change text path"));

    void undo() override;
    void redo() override;

private:
    void apply(const std::optional<PathGeometry>& path,
               const PathTypographyProperties& layout);

    std::optional<PathGeometry> m_oldPath;
    PathTypographyProperties m_oldLayout;
    std::optional<PathGeometry> m_newPath;
    PathTypographyProperties m_newLayout;
};

} // namespace vt
