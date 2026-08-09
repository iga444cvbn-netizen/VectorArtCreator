#pragma once

#include <QObject>
#include <QPair>
#include <QStringList>
#include <QSet>

namespace vt {

class SelectionModel final : public QObject {
    Q_OBJECT

public:
    explicit SelectionModel(QObject* parent = nullptr);

    [[nodiscard]] QStringList selectedObjectIds() const;
    [[nodiscard]] QString activeObjectId() const;
    [[nodiscard]] bool contains(const QString& objectId) const;
    [[nodiscard]] QPair<int, int> textRange() const;
    [[nodiscard]] bool hasTextRange() const;

public slots:
    void setSelectedObjectIds(const QStringList& objectIds, const QString& activeObjectId = {});
    void selectSingle(const QString& objectId);
    void toggle(const QString& objectId);
    void add(const QString& objectId);
    void remove(const QString& objectId);
    void clear();
    void setTextRange(int start, int end);
    void clearTextRange();

signals:
    void selectionChanged();
    void textRangeChanged(int start, int end);

private:
    void emitIfChanged(const QSet<QString>& oldSelection, const QString& oldActive);

    QSet<QString> m_selectedObjectIds;
    QString m_activeObjectId;
    int m_textRangeStart = -1;
    int m_textRangeEnd = -1;
};

} // namespace vt
