#pragma once

#include "core/export/export_backend.h"
#include "core/scene/scene_geometry.h"

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
                                   QString* error) const;

private:
    [[nodiscard]] static QString pathData(const QPainterPath& path);
    [[nodiscard]] static QString number(qreal value);
};

} // namespace vt
