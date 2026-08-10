#include "core/presets/preset_resources.h"

#include <QtCore/qglobal.h>

namespace {

void initializePresetResources()
{
    // Q_INIT_RESOURCE must be expanded in global namespace so it resolves the
    // symbol emitted by rcc, not a vt-namespaced variant.
    Q_INIT_RESOURCE(resources);
}

} // namespace

namespace vt {

void ensurePresetResources()
{
    static const bool initialized = [] {
        initializePresetResources();
        return true;
    }();
    Q_UNUSED(initialized);
}

} // namespace vt
