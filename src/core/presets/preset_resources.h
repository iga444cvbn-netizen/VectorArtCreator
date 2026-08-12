#pragma once

namespace vt {

// Ensures qrc resources linked through the core static library are registered
// even when no executable directly references the generated qrc object.
void ensurePresetResources();

} // namespace vt
