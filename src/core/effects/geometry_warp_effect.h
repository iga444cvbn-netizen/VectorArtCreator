#pragma once

#include "core/effects/effect.h"

namespace vt {

class GeometryWarpEffect final : public Effect {
public:
    enum class Mode { Bend, Sag, WaveWarp, Bulge, Pinch, NoiseWarp, Melt, Smear };
    GeometryWarpEffect(QString typeId, QString name, Mode mode);
    [[nodiscard]] QString typeId() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] EffectDomain domain() const override;
    [[nodiscard]] std::unique_ptr<Effect> clone() const override;
    void apply(VectorGeometry& geometry, const EffectContext& context) const override;
    [[nodiscard]] QVector<EffectParameter> parameterDefinitions() const override;
    [[nodiscard]] bool setParameter(const QString& id, double value) override;
    [[nodiscard]] QJsonObject parametersToJson() const override;
    [[nodiscard]] bool parametersFromJson(const QJsonObject& object, QString* error) override;

private:
    QPointF warp(const QPointF& point, const EffectContext& context) const;
    QString m_typeId;
    QString m_name;
    Mode m_mode;
    qreal m_amount = 0.25;
    qreal m_frequency = 1.0;
    qreal m_centerX = 0.5;
    qreal m_centerY = 0.5;
    qreal m_radius = 1.0;
    qreal m_falloff = 1.0;
    quint32 m_seed = 1;
};

} // namespace vt
