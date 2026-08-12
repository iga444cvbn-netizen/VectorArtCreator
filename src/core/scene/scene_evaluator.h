#pragma once

#include "core/document/document.h"
#include "core/evaluation/work_control.h"
#include "core/scene/scene_geometry.h"

#include <QByteArray>

namespace vt {

class SceneEvaluator {
public:
    // The page is passed by value by asynchronous callers. This makes the
    // worker independent from the mutable editor document.
    [[nodiscard]] static SceneGeometry evaluate(const Page& page,
                                                quint64 spatialRevision = 0,
                                                const WorkControl& work = WorkControl::withBudget());
    [[nodiscard]] static ObjectFrame evaluateObjectFrame(
        const TextObject& object,
        quint64 spatialRevision = 0,
        const WorkControl& work = WorkControl::withBudget());
    [[nodiscard]] static QByteArray shapingCacheKey(const TextObject& object);
    static void invalidateFontCaches();
    [[nodiscard]] static quint64 fontCacheEpoch();

};

} // namespace vt
