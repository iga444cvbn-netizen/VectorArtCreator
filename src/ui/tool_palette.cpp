#include "ui/tool_palette.h"

#include <QButtonGroup>
#include <QList>
#include <QPair>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

namespace vt {

ToolPalette::ToolPalette(QWidget* parent)
    : QWidget(parent)
    , m_group(new QButtonGroup(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 8, 4, 8);
    layout->setSpacing(4);
    m_group->setExclusive(true);

    const QList<QPair<QString, EditorTool>> tools = {
        {QStringLiteral("Select\nV"), EditorTool::Select},
        {QStringLiteral("Move\nM"), EditorTool::Move},
        {QStringLiteral("Text\nT"), EditorTool::Text},
        {QStringLiteral("Push"), EditorTool::Push},
        {QStringLiteral("Pull"), EditorTool::Pull},
        {QStringLiteral("Inflate"), EditorTool::Inflate},
        {QStringLiteral("Pinch"), EditorTool::Pinch},
        {QStringLiteral("Smooth"), EditorTool::Smooth},
    };
    for (int index = 0; index < tools.size(); ++index) {
        auto* button = new QToolButton(this);
        button->setText(tools.at(index).first);
        button->setToolTip(tools.at(index).first);
        button->setCheckable(true);
        button->setMinimumSize(58, 48);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        m_group->addButton(button, static_cast<int>(tools.at(index).second));
        layout->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, tool = tools.at(index).second] {
            emit toolSelected(tool);
        });
        if (index == 0) {
            button->setChecked(true);
        }
    }
    layout->addStretch(1);
}

} // namespace vt
