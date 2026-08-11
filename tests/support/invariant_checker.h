#pragma once

#include "core/document/document.h"
#include "core/scene/scene_geometry.h"

#include <QString>
#include <QStringList>

namespace vt::test {

struct InvariantReport {
    QStringList failures;
    [[nodiscard]] bool ok() const { return failures.isEmpty(); }
    [[nodiscard]] QString summary() const { return failures.join(QLatin1Char('\n')); }
};

[[nodiscard]] InvariantReport checkInvariants(const Document& document,
                                              const SceneGeometry* scene = nullptr,
                                              const QStringList& selectedObjectIds = {},
                                              const QString& activeObjectId = {},
                                              const QString& editingObjectId = {});

} // namespace vt::test
