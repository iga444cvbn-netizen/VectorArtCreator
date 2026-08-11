#pragma once

#include "core/document/document.h"

#include <QString>

namespace vt::test {

[[nodiscard]] bool compareEffects(const Effect& expected, const Effect& actual,
                                  QString* difference = nullptr,
                                  const QString& path = QStringLiteral("effect"));
[[nodiscard]] bool compareTextObjects(const TextObject& expected, const TextObject& actual,
                                      QString* difference = nullptr,
                                      const QString& path = QStringLiteral("object"));
[[nodiscard]] bool compareLayers(const Layer& expected, const Layer& actual,
                                 QString* difference = nullptr,
                                 const QString& path = QStringLiteral("layer"));
[[nodiscard]] bool comparePages(const Page& expected, const Page& actual,
                                QString* difference = nullptr,
                                const QString& path = QStringLiteral("page"));

} // namespace vt::test
