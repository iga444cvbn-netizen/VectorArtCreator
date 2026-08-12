#pragma once

#include "platform/vector_clipboard_service.h"

#include <QString>

#include <functional>

namespace vt {

class WindowsVectorClipboardService final {
public:
    [[nodiscard]] static ClipboardPublicationResult copyForOffice(const VectorExportPayload& payload);
    [[nodiscard]] static ClipboardPublicationResult copyForOfficeWithOpenAttemptForTesting(
        const VectorExportPayload& payload,
        const std::function<bool()>& openAttempt);
};

} // namespace vt
