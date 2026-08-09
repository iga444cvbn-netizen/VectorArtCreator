#include "ui/selection_model.h"

#include <algorithm>

namespace vt {

SelectionModel::SelectionModel(QObject* parent)
    : QObject(parent)
{
}

QStringList SelectionModel::selectedObjectIds() const
{
    QStringList result = m_selectedObjectIds.values();
    std::sort(result.begin(), result.end());
    return result;
}

QString SelectionModel::activeObjectId() const
{
    return m_activeObjectId;
}

bool SelectionModel::contains(const QString& objectId) const
{
    return m_selectedObjectIds.contains(objectId);
}

QPair<int, int> SelectionModel::textRange() const
{
    return {m_textRangeStart, m_textRangeEnd};
}

bool SelectionModel::hasTextRange() const
{
    return m_textRangeStart >= 0 && m_textRangeEnd > m_textRangeStart;
}

void SelectionModel::setSelectedObjectIds(const QStringList& objectIds,
                                          const QString& activeObjectId)
{
    const QSet<QString> oldSelection = m_selectedObjectIds;
    const QString oldActive = m_activeObjectId;
    m_selectedObjectIds.clear();
    for (const QString& objectId : objectIds) {
        m_selectedObjectIds.insert(objectId);
    }
    m_selectedObjectIds.remove(QString());
    m_activeObjectId = activeObjectId;
    if (m_activeObjectId.isEmpty() && !m_selectedObjectIds.isEmpty()) {
        m_activeObjectId = selectedObjectIds().first();
    }
    emitIfChanged(oldSelection, oldActive);
}

void SelectionModel::selectSingle(const QString& objectId)
{
    setSelectedObjectIds(objectId.isEmpty() ? QStringList() : QStringList{objectId}, objectId);
}

void SelectionModel::toggle(const QString& objectId)
{
    if (objectId.isEmpty()) {
        return;
    }
    const QSet<QString> oldSelection = m_selectedObjectIds;
    const QString oldActive = m_activeObjectId;
    if (m_selectedObjectIds.contains(objectId)) {
        m_selectedObjectIds.remove(objectId);
        if (m_activeObjectId == objectId) {
            m_activeObjectId = selectedObjectIds().value(0);
        }
    } else {
        m_selectedObjectIds.insert(objectId);
        m_activeObjectId = objectId;
    }
    emitIfChanged(oldSelection, oldActive);
}

void SelectionModel::add(const QString& objectId)
{
    if (objectId.isEmpty()) {
        return;
    }
    const QSet<QString> oldSelection = m_selectedObjectIds;
    const QString oldActive = m_activeObjectId;
    m_selectedObjectIds.insert(objectId);
    m_activeObjectId = objectId;
    emitIfChanged(oldSelection, oldActive);
}

void SelectionModel::remove(const QString& objectId)
{
    const QSet<QString> oldSelection = m_selectedObjectIds;
    const QString oldActive = m_activeObjectId;
    m_selectedObjectIds.remove(objectId);
    if (m_activeObjectId == objectId) {
        m_activeObjectId = selectedObjectIds().value(0);
    }
    emitIfChanged(oldSelection, oldActive);
}

void SelectionModel::clear()
{
    const QSet<QString> oldSelection = m_selectedObjectIds;
    const QString oldActive = m_activeObjectId;
    m_selectedObjectIds.clear();
    m_activeObjectId.clear();
    emitIfChanged(oldSelection, oldActive);
}

void SelectionModel::setTextRange(int start, int end)
{
    const int normalizedStart = qMax(0, qMin(start, end));
    const int normalizedEnd = qMax(normalizedStart, qMax(start, end));
    if (m_textRangeStart == normalizedStart && m_textRangeEnd == normalizedEnd) {
        return;
    }
    m_textRangeStart = normalizedStart;
    m_textRangeEnd = normalizedEnd;
    emit textRangeChanged(m_textRangeStart, m_textRangeEnd);
}

void SelectionModel::clearTextRange()
{
    if (!hasTextRange()) {
        return;
    }
    m_textRangeStart = -1;
    m_textRangeEnd = -1;
    emit textRangeChanged(-1, -1);
}

void SelectionModel::emitIfChanged(const QSet<QString>& oldSelection, const QString& oldActive)
{
    if (oldSelection != m_selectedObjectIds || oldActive != m_activeObjectId) {
        emit selectionChanged();
    }
}

} // namespace vt
