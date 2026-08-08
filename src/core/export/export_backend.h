#pragma once

#include "core/document/document.h"
#include "core/geometry/vector_geometry.h"

#include <QString>

namespace vt {

class IExportBackend {
public:
    virtual ~IExportBackend() = default;
    [[nodiscard]] virtual QString formatId() const = 0;
    [[nodiscard]] virtual bool exportGeometry(const Document& document,
                                               const VectorGeometry& geometry,
                                               const QString& filePath,
                                               QString* error) const = 0;
};

} // namespace vt
