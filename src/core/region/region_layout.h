#pragma once

#include "core/geometry/vector_geometry.h"
#include "core/region/typography_region.h"
#include "core/text/text_engine.h"

namespace vt {

// Region layout is a transactional typography stage.  It consumes already
// shaped glyphs and changes only derived VectorGeometry; source text and
// shaping metadata remain untouched.
class RegionLayoutEngine final {
public:
    [[nodiscard]] static bool apply(VectorGeometry* geometry,
                                    const ShapedText& shaped,
                                    const QString& sourceText,
                                    const TypographyRegion& region,
                                    const RegionTypographyProperties& settings,
                                    qreal lineSpacing,
                                    QString* error = nullptr,
                                    const WorkControl& work = WorkControl::unlimited());
};

} // namespace vt
