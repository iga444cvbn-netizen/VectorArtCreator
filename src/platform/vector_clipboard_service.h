#pragma once

#include "core/evaluation/work_control.h"
#include "core/export/vector_export_payload.h"

#include <QString>

namespace vt {

enum class ClipboardPublicationStatus {
    Complete,
    Partial,
    Cancelled,
    Failure,
};

struct ClipboardPublicationResult {
    ClipboardPublicationStatus status = ClipboardPublicationStatus::Failure;
    QString message;

    [[nodiscard]] bool succeeded() const {
        return status == ClipboardPublicationStatus::Complete
            || status == ClipboardPublicationStatus::Partial;
    }
    [[nodiscard]] bool cancelled() const { return status == ClipboardPublicationStatus::Cancelled; }
    [[nodiscard]] bool complete() const { return status == ClipboardPublicationStatus::Complete; }
    [[nodiscard]] static ClipboardPublicationResult fromFormats(bool vectorPublished,
                                                                bool svgPublished,
                                                                bool pngPublished,
                                                                bool textPublished,
                                                                const QString& failureMessage = {});
};

// Platform boundary for production clipboard formats. The editor's private
// object clipboard deliberately remains separate from this service.
class VectorClipboardService final {
public:
    [[nodiscard]] static ClipboardPublicationResult copyForOfficeResult(
        const VectorExportPayload& payload,
        const WorkControl& work = WorkControl::withBudget());
    [[nodiscard]] static bool copyForOffice(const VectorExportPayload& payload, QString* error);
    [[nodiscard]] static bool isAvailable();
};

} // namespace vt
