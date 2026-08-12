#pragma once

#include "core/export/export_backend.h"
#include "core/evaluation/work_control.h"
#include "core/scene/scene_geometry.h"
#include "core/export/vector_export_payload.h"

namespace vt {

class SvgExporter final : public IExportBackend {
public:
    [[nodiscard]] QString formatId() const override;
    [[nodiscard]] bool exportGeometry(const Document& document,
                                      const VectorGeometry& geometry,
                                      const QString& filePath,
                                      QString* error) const override;
    [[nodiscard]] bool exportScene(const Document& document,
                                   const SceneGeometry& scene,
                                   const QString& filePath,
                                   QString* error,
                                   const WorkControl& work = WorkControl::withBudget()) const;
    [[nodiscard]] bool exportPayload(const VectorExportPayload& payload,
                                     const QString& filePath,
                                     QString* error,
                                     const WorkControl& work = WorkControl::withBudget()) const;

private:
    [[nodiscard]] static QString pathData(const QPainterPath& path,
                                          const WorkControl& work);
    [[nodiscard]] static QString number(qreal value);
};

} // namespace vt
