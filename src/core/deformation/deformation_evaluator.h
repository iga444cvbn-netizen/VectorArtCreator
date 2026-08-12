#pragma once

#include "core/deformation/manual_deformation.h"

namespace vt {

class DeformationEvaluator {
public:
    static void apply(const ManualDeformation& deformation,
                      VectorGeometry& geometry,
                      const WorkControl& work = WorkControl::unlimited());
};

} // namespace vt
