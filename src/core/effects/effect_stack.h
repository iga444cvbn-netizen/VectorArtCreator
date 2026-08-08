#pragma once

#include "core/effects/effect.h"

#include <QJsonArray>

#include <memory>
#include <vector>

namespace vt {

class EffectStack {
public:
    EffectStack() = default;
    EffectStack(const EffectStack& other);
    EffectStack& operator=(const EffectStack& other);
    EffectStack(EffectStack&&) noexcept = default;
    EffectStack& operator=(EffectStack&&) noexcept = default;

    [[nodiscard]] int size() const;
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] Effect* at(int index);
    [[nodiscard]] const Effect* at(int index) const;

    void append(std::unique_ptr<Effect> effect);
    [[nodiscard]] std::unique_ptr<Effect> takeAt(int index);
    void removeAt(int index);
    void move(int from, int to);
    void clear();

    void apply(VectorGeometry& geometry) const;

    [[nodiscard]] QJsonArray toJson() const;
    [[nodiscard]] static EffectStack fromJson(const QJsonArray& array, QString* error);

private:
    std::vector<std::unique_ptr<Effect>> m_effects;
};

} // namespace vt
