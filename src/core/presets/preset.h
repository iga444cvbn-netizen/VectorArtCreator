#pragma once

#include "core/effects/effect_stack.h"

#include <QJsonDocument>
#include <QString>

namespace vt {

struct Preset {
    static constexpr int CurrentFormatVersion = 1;

    int formatVersion = CurrentFormatVersion;
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
