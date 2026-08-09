#pragma once

#include "core/effects/effect_stack.h"

#include <QJsonDocument>
#include <QString>

namespace vt {

struct Preset {
    static constexpr int CurrentFormatVersion = 2;

    int formatVersion = CurrentFormatVersion;
    // Stable storage identity. This is deliberately separate from the
    // human-readable, Unicode display name.
    QString id;
    QString name;
    EffectStack effects;

    Preset() = default;
    Preset(const Preset& other) = default;
    Preset& operator=(const Preset& other) = default;
    Preset(Preset&&) noexcept = default;
    Preset& operator=(Preset&&) noexcept = default;

    [[nodiscard]] QJsonDocument toJson() const;
    [[nodiscard]] static bool fromJson(const QJsonDocument& json, Preset* preset, QString* error);
};

} // namespace vt
