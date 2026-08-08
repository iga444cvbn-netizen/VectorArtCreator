#pragma once

#include "core/effects/effect.h"

namespace vt {

class StretchEffect final : public Effect {
public:
    qreal horizontal = 1.0;
    qreal vertical = 1.0;

    [[nodiscard]] QString typeId() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] EffectDomain domain() const override;
    [[nodiscard]] std::unique_ptr<Effect> clone() const override;
    [[nodiscard]] QVector<EffectParameter> parameterDefinitions() const override;
    [[nodiscard]] bool setParameter(const QString& id, double value) override;
    [[nodiscard]] QJsonObject parametersToJson() const override;
    [[nodiscard]] bool parametersFromJson(const QJsonObject& object, QString* error) override;

    void apply(VectorGeometry& geometry, const EffectContext& context) const override;
};

} // namespace vt
