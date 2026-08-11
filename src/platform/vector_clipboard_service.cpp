#include "platform/vector_clipboard_service.h"

#ifdef Q_OS_WIN
#include "platform/windows/windows_vector_clipboard_service.h"
#endif

namespace vt {

bool VectorClipboardService::copyForOffice(const VectorExportPayload& payload, QString* error)
{
#ifdef Q_OS_WIN
    return WindowsVectorClipboardService::copyForOffice(payload, error);
#else
    Q_UNUSED(payload);
    if (error) *error = QStringLiteral("Copy for Word is currently available on Windows only.");
    return false;
#endif
}

bool VectorClipboardService::isAvailable()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

} // namespace vt
