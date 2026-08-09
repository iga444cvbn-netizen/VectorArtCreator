#pragma once

#include "core/geometry/vector_geometry.h"

#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QPair>
#include <QUuid>

#include <memory>

namespace vt {

enum class EffectDomain {
    Layout,
    GlyphTransform,
    Geometry,
    Generator,
    Mask,
    Deformation,
};

enum class EffectScopeKind {
    WholeObject,
    TextRange,
};

struct EffectScope {
    EffectScopeKind kind = EffectScopeKind::WholeObject;
    int start = 0;
    int end = 0;

    [[nodiscard]] bool includes(int clusterStart, int clusterLength) const;
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static EffectScope fromJson(const QJsonObject& object);
};

struct EffectMaskStroke {
    QVector<QPointF> points;
    qreal radius = 32.0;
    qreal opacity = 1.0;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static EffectMaskStroke fromJson(const QJsonObject& object);
};

struct EffectContext {
    QRectF referenceBounds;
    qreal referenceHeight = 1.0;
};

struct EffectParameter {
    QString id;
    QString label;
    double value = 0.0;
    double minimum = 0.0;
    double maximum = 1.0;
    double step = 0.01;
    bool integer = false;
};

class Effect {
public:
    virtual ~Effect() = default;

    [[nodiscard]] virtual QString typeId() const = 0;
    [[nodiscard]] virtual QString displayName() const = 0;
    [[nodiscard]] virtual EffectDomain domain() const = 0;
    [[nodiscard]] virtual std::unique_ptr<Effect> clone() const = 0;
    virtual void apply(VectorGeometry& geometry, const EffectContext& context) const = 0;
    [[nodiscard]] virtual QVector<EffectParameter> parameterDefinitions() const = 0;
    [[nodiscard]] virtual bool setParameter(const QString& id, double value) = 0;
    [[nodiscard]] virtual QJsonObject parametersToJson() const = 0;
    [[nodiscard]] virtual bool parametersFromJson(const QJsonObject& object, QString* error) = 0;

    QString instanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool enabled = true;
    double masterStrength = 1.0;
    EffectScope scope;
    QVector<EffectMaskStroke> maskStrokes;
    bool maskInverted = false;
};

[[nodiscard]] std::unique_ptr<Effect> createEffect(const QString& typeId);
[[nodiscard]] QVector<QPair<QString, QString>> availableEffectTypes();
[[nodiscard]] std::unique_ptr<Effect> effectFromJson(const QJsonObject& object, QString* error);

} // namespace vt
