#pragma once

#include "core/undo/document_commands.h"

#include <QStringList>
#include <QVariant>

namespace vt {

class AddTextObjectCommand final : public DocumentCommand {
public:
    AddTextObjectCommand(Document& document,
                         QString layerId,
                         TextObject object,
                         DocumentChangeCallback onChanged,
                         QString description = QStringLiteral("Create text object"));

    void undo() override;
    void redo() override;

private:
    QString m_layerId;
    TextObject m_object;
};

class RemoveTextObjectCommand final : public DocumentCommand {
public:
    RemoveTextObjectCommand(Document& document,
                            QString layerId,
                            TextObject object,
                            int index,
                            DocumentChangeCallback onChanged,
                            QString description = QStringLiteral("Delete text object"));

    void undo() override;
    void redo() override;

private:
    QString m_layerId;
    TextObject m_object;
    int m_index = -1;
};

class MoveObjectsCommand final : public DocumentCommand {
public:
    MoveObjectsCommand(Document& document,
                       QStringList objectIds,
                       QPointF delta,
                       DocumentChangeCallback onChanged,
                       QString description = QStringLiteral("Move objects"));

    void undo() override;
    void redo() override;
    [[nodiscard]] int id() const override;
    bool mergeWith(const QUndoCommand* other) override;

private:
    void apply(const QPointF& delta);

    QStringList m_objectIds;
    QPointF m_delta;
};

class SetObjectTransformCommand final : public DocumentCommand {
public:
    SetObjectTransformCommand(Document& document,
                              QString objectId,
                              ObjectTransform oldTransform,
                              ObjectTransform newTransform,
                              DocumentChangeCallback onChanged,
                              QString description = QStringLiteral("Transform object"));

    void undo() override;
    void redo() override;

private:
    QString m_objectId;
    ObjectTransform m_oldTransform;
    ObjectTransform m_newTransform;
};

class AddLayerCommand final : public DocumentCommand {
public:
    AddLayerCommand(Document& document,
                    QString pageId,
                    Layer layer,
                    int index,
                    DocumentChangeCallback onChanged,
                    QString description = QStringLiteral("Add layer"));

    void undo() override;
    void redo() override;

private:
    QString m_pageId;
    Layer m_layer;
    int m_index = -1;
};

class RemoveLayerCommand final : public DocumentCommand {
public:
    RemoveLayerCommand(Document& document,
                       QString pageId,
                       Layer layer,
                       int index,
                       DocumentChangeCallback onChanged,
                       QString description = QStringLiteral("Delete layer"));

    void undo() override;
    void redo() override;

private:
    QString m_pageId;
    Layer m_layer;
    int m_index = -1;
};

class SetLayerStateCommand final : public DocumentCommand {
public:
    enum class Property { Name, Visible, Locked };

    SetLayerStateCommand(Document& document,
                         QString layerId,
                         Property property,
                         QVariant oldValue,
                         QVariant newValue,
                         DocumentChangeCallback onChanged,
                         QString description = QStringLiteral("Change layer"));

    void undo() override;
    void redo() override;

private:
    void apply(const QVariant& value);

    QString m_layerId;
    Property m_property;
    QVariant m_oldValue;
    QVariant m_newValue;
};

class AddPageCommand final : public DocumentCommand {
public:
    AddPageCommand(Document& document,
                   Page page,
                   int index,
                   DocumentChangeCallback onChanged,
                   QString description = QStringLiteral("Add page"));

    void undo() override;
    void redo() override;

private:
    Page m_page;
    int m_index = -1;
};

class RemovePageCommand final : public DocumentCommand {
public:
    RemovePageCommand(Document& document,
                      Page page,
                      int index,
                      DocumentChangeCallback onChanged,
                      QString description = QStringLiteral("Delete page"));

    void undo() override;
    void redo() override;

private:
    Page m_page;
    int m_index = -1;
};

class SetCurrentPageCommand final : public DocumentCommand {
public:
    SetCurrentPageCommand(Document& document,
                          QString oldPageId,
                          QString newPageId,
                          QString oldLayerId,
                          QString newLayerId,
                          DocumentChangeCallback onChanged,
                          QString description = QStringLiteral("Switch page"));

    void undo() override;
    void redo() override;

private:
    void apply(const QString& pageId, const QString& layerId);

    QString m_oldPageId;
    QString m_newPageId;
    QString m_oldLayerId;
    QString m_newLayerId;
};

} // namespace vt
