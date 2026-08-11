#pragma once

#include "platform/vector_clipboard_service.h"

#include <QString>

namespace vt {

class WindowsVectorClipboardService final {
public:
    [[nodiscard]] static ClipboardPublicationResult copyForOffice(const VectorExportPayload& payload);
};

} // namespace vt
