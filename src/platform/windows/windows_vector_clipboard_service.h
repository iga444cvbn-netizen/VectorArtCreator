#pragma once

#include "platform/vector_clipboard_service.h"

#include <QString>

#include <cstddef>
#include <functional>

namespace vt {

#ifdef Q_OS_WIN
// Narrow Win32 seam used to verify allocation, lock, registration, transfer,
// and ownership failures without depending on the process clipboard. Handles
// intentionally remain opaque here so this header stays independent of
// windows.h.
class WindowsClipboardOperations {
public:
    virtual ~WindowsClipboardOperations() = default;

    [[nodiscard]] virtual bool openClipboard(void* owner) = 0;
    virtual void closeClipboard() = 0;
    [[nodiscard]] virtual bool emptyClipboard() = 0;
    virtual void retryDelay(unsigned milliseconds) = 0;

    [[nodiscard]] virtual void* allocateGlobal(std::size_t bytes) = 0;
    [[nodiscard]] virtual void* lockGlobal(void* handle) = 0;
    virtual void unlockGlobal(void* handle) = 0;
    virtual void freeGlobal(void* handle) = 0;
    [[nodiscard]] virtual void* publish(unsigned format, void* handle) = 0;
    [[nodiscard]] virtual unsigned registerFormat(const wchar_t* name) = 0;
    virtual void deleteEnhancedMetafile(void* handle) = 0;
};
#endif

class WindowsVectorClipboardService final {
public:
    [[nodiscard]] static ClipboardPublicationResult copyForOffice(
        const VectorExportPayload& payload,
        const WorkControl& work = WorkControl::withBudget());
    [[nodiscard]] static ClipboardPublicationResult copyForOfficeWithOpenAttemptForTesting(
        const VectorExportPayload& payload,
        const std::function<bool()>& openAttempt);
#ifdef Q_OS_WIN
    [[nodiscard]] static ClipboardPublicationResult copyForOfficeWithOperationsForTesting(
        const VectorExportPayload& payload,
        WindowsClipboardOperations& operations,
        const WorkControl& work = WorkControl::withBudget());
#endif
};

} // namespace vt
