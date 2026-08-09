#pragma once

#include "core/document/document.h"
#include "core/scene/scene_geometry.h"

namespace vt {

class SceneEvaluator {
public:
    // The page is passed by value by asynchronous callers. This makes the
    // worker independent from the mutable editor document.
    [[nodiscard]] static SceneGeometry evaluate(const Page& page);

private:
    [[nodiscard]] static VectorGeometry evaluateObject(const TextObject& object,
                                                        QString* warning,
                                                        QString* error);
};

} // namespace vt
