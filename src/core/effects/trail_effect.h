#pragma once
#include "core/effects/effect.h"
namespace vt {
class TrailEffect final : public Effect {
public:
    TrailEffect(QString id, QString name);
    [[nodiscard]] QString typeId() const override; [[nodiscard]] QString displayName() const override;
    [[nodiscard]] EffectDomain domain() const override; [[nodiscard]] std::unique_ptr<Effect> clone() const override;
    void apply(VectorGeometry&, const EffectContext&) const override;
    [[nodiscard]] QVector<EffectParameter> parameterDefinitions() const override;
    [[nodiscard]] bool setParameter(const QString&, double) override;
    [[nodiscard]] QJsonObject parametersToJson() const override;
    [[nodiscard]] bool parametersFromJson(const QJsonObject&, QString*) override;
    [[nodiscard]] bool generatesGeometry() const override { return true; }
private: QString m_id,m_name; int m_count=3; qreal m_offsetX=0.08,m_offsetY=0.05,m_rotation=0,m_scale=0.96,m_decay=0.7;
}; }
