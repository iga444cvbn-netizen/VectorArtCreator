#pragma once

#include "core/export/export_backend.h"

namespace vt {

class SvgExporter final : public IExportBackend {
public:
    [[nodiscard]] QString formatId() const override;
    [[nodiscard]] bool exportGeometry(const Document& document,
                                      const VectorGeometry& geometry,
                                      const QString& filePath,
                                      QString* error) const override;

private:
    [[nodiscard]] static QString pathData(const QPainterPath& path);
    [[nodiscard]] static QString number(qreal value);
};

} // namespace vt
