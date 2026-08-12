#pragma once

#include "core/export/vector_export_payload.h"

#include <QString>

namespace vt {

class WindowsVectorClipboardService final {
public:
    [[nodiscard]] static bool copyForOffice(const VectorExportPayload& payload, QString* error);
};

} // namespace vt
