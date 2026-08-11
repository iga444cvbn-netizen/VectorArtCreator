#pragma once

#include <QString>

namespace vt::test {

[[nodiscard]] QString deterministicTestFamily(bool requireCyrillic = false);

} // namespace vt::test
