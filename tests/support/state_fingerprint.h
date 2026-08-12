#pragma once

#include "core/document/document.h"

#include <QString>

namespace vt::test {

// Canonical, persistent-only representation.  It intentionally omits dates,
// widgets, scene caches, selection hover state and async generations.
[[nodiscard]] QString semanticFingerprint(const Document& document);
[[nodiscard]] bool semanticallyEqual(const Document& left, const Document& right,
                                     QString* difference = nullptr);

} // namespace vt::test
