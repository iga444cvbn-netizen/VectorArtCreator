#include "platform/vector_clipboard_service.h"

#include <QStringList>

#ifdef Q_OS_WIN
#include "platform/windows/windows_vector_clipboard_service.h"
#endif

namespace vt {

ClipboardPublicationResult ClipboardPublicationResult::fromFormats(
    bool vectorPublished, bool svgPublished, bool pngPublished, bool textPublished,
    const QString& failureMessage)
{
    if (!vectorPublished) {
        return {ClipboardPublicationStatus::Failure,
                failureMessage.isEmpty()
                    ? QStringLiteral("The vector clipboard format could not be published.")
                    : failureMessage};
    }
    if (svgPublished && pngPublished && textPublished) {
        return {ClipboardPublicationStatus::Complete, {}};
    }
    QStringList missing;
    if (!svgPublished) missing << QStringLiteral("SVG");
    if (!pngPublished) missing << QStringLiteral("PNG");
    if (!textPublished) missing << QStringLiteral("Unicode text");
    return {ClipboardPublicationStatus::Partial,
            QStringLiteral("Vector artwork was copied, but these fallback formats failed: %1.")
                .arg(missing.join(QStringLiteral(", ")))};
}

ClipboardPublicationResult VectorClipboardService::copyForOfficeResult(
    const VectorExportPayload& payload)
{
#ifdef Q_OS_WIN
    return WindowsVectorClipboardService::copyForOffice(payload);
#else
    Q_UNUSED(payload);
    return {ClipboardPublicationStatus::Failure,
            QStringLiteral("Copy for Word is currently available on Windows only.")};
#endif
}

bool VectorClipboardService::copyForOffice(const VectorExportPayload& payload, QString* error)
{
    const ClipboardPublicationResult result = copyForOfficeResult(payload);
    if (error) *error = result.message;
    return result.succeeded();
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
