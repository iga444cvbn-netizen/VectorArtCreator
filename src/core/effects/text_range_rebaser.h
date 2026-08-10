#pragma once

#include "core/effects/effect.h"

namespace vt {

class TextRangeRebaser final {
public:
    // Qt source offsets are UTF-16 indices.  Keeping the operation in that
    // coordinate space makes it match QTextCursor and shaping cluster scopes.
    [[nodiscard]] static EffectScope rebase(const EffectScope& scope,
                                             const QString& oldText,
                                             const QString& newText);
};

} // namespace vt
