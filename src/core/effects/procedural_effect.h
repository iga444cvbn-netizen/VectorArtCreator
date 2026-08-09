#pragma once

#include "core/effects/effect.h"

namespace vt {

// A compact deterministic family for the Phase 3 effect browser. Each
// instance is still a normal nondestructive Effect, so it composes in the
// existing stack and serializes through the same interface.
class ProceduralEffect final : public Effect {
public:
    enum class Mode {
        Bounce,
        Staircase,
        RandomOffset,
        RandomRotation,
        RandomScale,
        HorizontalSpread,
        VerticalSpread,
        Arc,
        Zigzag,
        SineRotation,
        Crescendo,
        Shrink,
        Skew,
        Compression,
        ExpandCenter,
        SqueezeCenter,
        BaselineDrift,
        AlternatingTilt,
    };

    ProceduralEffect(QString typeId, QString displayName, Mode mode);

    [[nodiscard]] QString typeId() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] EffectDomain domain() const override;
    [[nodiscard]] std::unique_ptr<Effect> clone() const override;
    [[nodiscard]] QVector<EffectParameter> parameterDefinitions() const override;
    [[nodiscard]] bool setParameter(const QString& id, double value) override;
    [[nodiscard]] QJsonObject parametersToJson() const override;
    [[nodiscard]] bool parametersFromJson(const QJsonObject& object, QString* error) override;
    void apply(VectorGeometry& geometry, const EffectContext& context) const override;

private:
    QString m_typeId;
    QString m_displayName;
    Mode m_mode;
    qreal m_strength = 0.5;
    qreal m_frequency = 1.0;
    quint32 m_seed = 1337;
};

} // namespace vt
