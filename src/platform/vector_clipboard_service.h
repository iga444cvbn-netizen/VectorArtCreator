#pragma once

#include "core/export/vector_export_payload.h"

#include <QString>

namespace vt {

// Platform boundary for production clipboard formats. The editor's private
// object clipboard deliberately remains separate from this service.
class VectorClipboardService final {
public:
    [[nodiscard]] static bool copyForOffice(const VectorExportPayload& payload, QString* error);
    [[nodiscard]] static bool isAvailable();
};

} // namespace vt
