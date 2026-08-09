#pragma once

#include "core/deformation/manual_deformation.h"

#include <optional>

namespace vt {

// Select is a canvas interaction mode, not a DeformationStroke mode. Keeping
// it outside BrushMode prevents an inactive canvas click from becoming a
// persistent deformation operation by accident.
enum class EditorTool {
    Select,
    Push,
    Pull,
    Inflate,
    Pinch,
    Smooth,
};

class DeformationToolState {
public:
    [[nodiscard]] EditorTool tool() const { return m_tool; }

    void setTool(EditorTool tool)
    {
        if (m_tool != EditorTool::Smooth && tool == EditorTool::Smooth) {
            m_targetBeforeSmooth = m_target;
        } else if (m_tool == EditorTool::Smooth && tool != EditorTool::Smooth) {
            m_target = m_targetBeforeSmooth;
        }
        m_tool = tool;
    }

    [[nodiscard]] bool acceptsCanvasStroke() const
    {
        return m_tool != EditorTool::Select;
    }

    [[nodiscard]] bool targetSelectionEnabled() const
    {
        return acceptsCanvasStroke() && m_tool != EditorTool::Smooth;
    }

    [[nodiscard]] BrushTarget target() const
    {
        return m_tool == EditorTool::Smooth ? BrushTarget::Shape : m_target;
    }

    void setTarget(BrushTarget target)
    {
        m_target = target;
    }

    [[nodiscard]] std::optional<BrushMode> brushMode() const
    {
        switch (m_tool) {
        case EditorTool::Select:
            return std::nullopt;
        case EditorTool::Push:
            return BrushMode::Push;
        case EditorTool::Pull:
            return BrushMode::Pull;
        case EditorTool::Inflate:
            return BrushMode::Inflate;
        case EditorTool::Pinch:
            return BrushMode::Pinch;
        case EditorTool::Smooth:
            return BrushMode::Smooth;
        }
        return std::nullopt;
    }

private:
    EditorTool m_tool = EditorTool::Select;
    BrushTarget m_target = BrushTarget::Shape;
    BrushTarget m_targetBeforeSmooth = BrushTarget::Shape;
};

} // namespace vt
