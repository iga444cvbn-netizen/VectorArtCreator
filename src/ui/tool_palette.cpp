#include "ui/tool_palette.h"

#include <QButtonGroup>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>

namespace vt {

namespace {

QString toolName(EditorTool tool)
{
    switch (tool) {
    case EditorTool::Select: return QStringLiteral("Select");
    case EditorTool::Move: return QStringLiteral("Move");
    case EditorTool::Text: return QStringLiteral("Text");
    case EditorTool::Push: return QStringLiteral("Push");
    case EditorTool::Pull: return QStringLiteral("Pull");
    case EditorTool::Inflate: return QStringLiteral("Inflate");
    case EditorTool::Pinch: return QStringLiteral("Pinch");
    case EditorTool::Smooth: return QStringLiteral("Smooth");
    case EditorTool::EffectMask: return QStringLiteral("Effect mask / eraser");
    }
    return QStringLiteral("Tool");
}

QIcon makeToolIcon(EditorTool tool)
{
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(220, 226, 236), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const QRectF r(5.0, 5.0, 18.0, 18.0);
    switch (tool) {
    case EditorTool::Select:
        painter.drawLine(7, 5, 11, 22);
        painter.drawLine(11, 18, 17, 24);
        painter.drawLine(11, 18, 19, 16);
        break;
    case EditorTool::Move:
        painter.drawLine(14, 4, 14, 24);
        painter.drawLine(4, 14, 24, 14);
        painter.drawLine(14, 4, 10, 8);
        painter.drawLine(14, 4, 18, 8);
        painter.drawLine(14, 24, 10, 20);
        painter.drawLine(14, 24, 18, 20);
        painter.drawLine(4, 14, 8, 10);
        painter.drawLine(4, 14, 8, 18);
        painter.drawLine(24, 14, 20, 10);
        painter.drawLine(24, 14, 20, 18);
        break;
    case EditorTool::Text:
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 18, QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("T"));
        break;
    case EditorTool::Push:
    case EditorTool::Pull:
        painter.drawLine(7, 14, 21, 14);
        painter.drawLine(tool == EditorTool::Push ? 21 : 7, 14,
                         tool == EditorTool::Push ? 16 : 12, 9);
        painter.drawLine(tool == EditorTool::Push ? 21 : 7, 14,
                         tool == EditorTool::Push ? 16 : 12, 19);
        painter.drawEllipse(QPointF(14, 14), 4, 4);
        break;
    case EditorTool::Inflate:
        painter.drawEllipse(r);
        painter.drawLine(14, 2, 14, 8);
        painter.drawLine(14, 20, 14, 26);
        painter.drawLine(2, 14, 8, 14);
        painter.drawLine(20, 14, 26, 14);
        break;
    case EditorTool::Pinch:
        painter.drawLine(5, 7, 14, 14);
        painter.drawLine(23, 7, 14, 14);
        painter.drawLine(5, 21, 14, 14);
        painter.drawLine(23, 21, 14, 14);
        break;
    case EditorTool::Smooth:
        painter.drawPath(QPainterPath(QPointF(4, 18)));
        painter.drawArc(r, 20 * 16, 140 * 16);
        break;
    case EditorTool::EffectMask:
        painter.drawEllipse(r);
        painter.drawLine(6, 22, 22, 6);
        break;
    }
    return QIcon(pixmap);
}

}

ToolPalette::ToolPalette(QWidget* parent)
    : QWidget(parent)
    , m_group(new QButtonGroup(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 8, 4, 8);
    layout->setSpacing(5);
    m_group->setExclusive(true);

    const QVector<EditorTool> tools = {
        EditorTool::Select,
        EditorTool::Move,
        EditorTool::Text,
        EditorTool::Push,
        EditorTool::Pull,
        EditorTool::Inflate,
        EditorTool::Pinch,
        EditorTool::Smooth,
        EditorTool::EffectMask,
    };
    for (EditorTool tool : tools) {
        auto* button = new QToolButton(this);
        button->setIcon(makeToolIcon(tool));
        button->setIconSize(QSize(24, 24));
        button->setCheckable(true);
        button->setAutoRaise(false);
        button->setMinimumSize(42, 42);
        button->setMaximumSize(46, 46);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setAccessibleName(toolName(tool));
        m_group->addButton(button, static_cast<int>(tool));
        m_buttons.insert(static_cast<int>(tool), button);
        layout->addWidget(button, 0, Qt::AlignHCenter);
        connect(button, &QToolButton::clicked, this, [this, tool] { emit toolSelected(tool); });
    }
    layout->addStretch(1);
    setActiveTool(EditorTool::Select);
}

void ToolPalette::setActiveTool(EditorTool tool)
{
    if (QToolButton* button = m_buttons.value(static_cast<int>(tool))) {
        const QSignalBlocker blocker(button);
        button->setChecked(true);
    }
    for (auto iterator = m_buttons.cbegin(); iterator != m_buttons.cend(); ++iterator) {
        updateToolTip(static_cast<EditorTool>(iterator.key()));
    }
}

void ToolPalette::setToolShortcut(EditorTool tool, const QKeySequence& sequence)
{
    if (!m_buttons.contains(static_cast<int>(tool))) {
        return;
    }
    updateToolTip(tool);
    m_buttons.value(static_cast<int>(tool))->setProperty("shortcutText", sequence.toString());
    updateToolTip(tool);
}

void ToolPalette::updateToolTip(EditorTool tool)
{
    QToolButton* button = m_buttons.value(static_cast<int>(tool));
    if (!button) {
        return;
    }
    const QString shortcut = button->property("shortcutText").toString();
    button->setToolTip(shortcut.isEmpty()
                           ? toolName(tool)
                           : QStringLiteral("%1 (%2)").arg(toolName(tool), shortcut));
}

} // namespace vt
