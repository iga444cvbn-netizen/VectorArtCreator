#pragma once

#include "ui/deformation_tool_state.h"

#include <QWidget>

class QButtonGroup;

namespace vt {

class ToolPalette final : public QWidget {
    Q_OBJECT

public:
    explicit ToolPalette(QWidget* parent = nullptr);

signals:
    void toolSelected(EditorTool tool);

private:
    QButtonGroup* m_group = nullptr;
};

} // namespace vt
