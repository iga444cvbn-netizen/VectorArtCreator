#pragma once

#include "ui/deformation_tool_state.h"

#include <QHash>
#include <QKeySequence>
#include <QWidget>

class QButtonGroup;
class QToolButton;

namespace vt {

class ToolPalette final : public QWidget {
    Q_OBJECT

public:
    explicit ToolPalette(QWidget* parent = nullptr);

    void setActiveTool(EditorTool tool);
    void setToolShortcut(EditorTool tool, const QKeySequence& sequence);

signals:
    void toolSelected(EditorTool tool);

private:
    void updateToolTip(EditorTool tool);

    QButtonGroup* m_group = nullptr;
    QHash<int, QToolButton*> m_buttons;
};

} // namespace vt
