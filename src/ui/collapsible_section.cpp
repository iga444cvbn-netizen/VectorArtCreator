#include "ui/collapsible_section.h"

#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

namespace vt {

CollapsibleSection::CollapsibleSection(const QString& settingsKey,
                                       const QString& title,
                                       QWidget* content,
                                       bool defaultExpanded,
                                       QWidget* parent)
    : QWidget(parent)
    , m_settingsKey(settingsKey)
    , m_header(new QToolButton(this))
    , m_content(content)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 4);
    layout->setSpacing(0);

    m_header->setText(title);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setAutoRaise(true);
    m_header->setCursor(Qt::PointingHandCursor);
    m_header->setStyleSheet(QStringLiteral(
        "QToolButton { text-align: left; padding: 6px 4px; font-weight: 600; "
        "border: 0; border-bottom: 1px solid #404650; }"
        "QToolButton:hover { color: #8fc7ff; }"));
    layout->addWidget(m_header);

    if (m_content) {
        m_content->setParent(this);
        layout->addWidget(m_content);
    }

    const QSettings settings;
    const bool expanded = settings.value(
        QStringLiteral("inspector/%1/expanded").arg(m_settingsKey), defaultExpanded).toBool();
    connect(m_header, &QToolButton::clicked, this, [this] { setExpanded(!m_expanded); });
    setExpanded(expanded);
}

void CollapsibleSection::setExpanded(bool expanded)
{
    if (m_expanded == expanded && m_header->arrowType() != (expanded ? Qt::DownArrow : Qt::RightArrow)) {
        // Continue below so a newly constructed section always gets the right arrow.
    } else if (m_expanded == expanded && m_content && m_content->isVisible() == expanded) {
        m_header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        return;
    }
    m_expanded = expanded;
    m_header->setArrowType(m_expanded ? Qt::DownArrow : Qt::RightArrow);
    if (m_content) {
        m_content->setVisible(m_expanded);
    }
    QSettings settings;
    settings.setValue(QStringLiteral("inspector/%1/expanded").arg(m_settingsKey), m_expanded);
    emit expandedChanged(m_expanded);
}

} // namespace vt
