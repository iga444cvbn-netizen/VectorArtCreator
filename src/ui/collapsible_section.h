#pragma once

#include <QWidget>

class QToolButton;

namespace vt {

class CollapsibleSection final : public QWidget {
    Q_OBJECT

public:
    explicit CollapsibleSection(const QString& settingsKey,
                                const QString& title,
                                QWidget* content,
                                bool defaultExpanded = true,
                                QWidget* parent = nullptr);

    [[nodiscard]] bool isExpanded() const { return m_expanded; }
    void setExpanded(bool expanded);

signals:
    void expandedChanged(bool expanded);

private:
    QString m_settingsKey;
    QToolButton* m_header = nullptr;
    QWidget* m_content = nullptr;
    bool m_expanded = true;
};

} // namespace vt
