#pragma once

#include "core/effects/effect.h"

#include <functional>

namespace vt {

// Metadata is deliberately independent from widgets.  New effect pickers,
// preset validation and serialization all consume this single catalog.
struct EffectDescriptor {
    QString typeId;
    QString displayName;
    QString category;
    QString shortDescription;
    QStringList searchTags;
    EffectDomain domain = EffectDomain::GlyphTransform;
    bool supportsMask = true;
    bool supportsTextRange = true;
    bool deterministic = true;
    bool preservesPieceCount = true;
    bool preservesPathTopology = true;
    QStringList basicParameterIds;
    QPair<double, double> recommendedMasterStrengthRange = {0.0, 2.0};
    QString iconName;
    std::function<std::unique_ptr<Effect>()> factory;
};

class EffectRegistry final {
public:
    [[nodiscard]] static const EffectRegistry& instance();
    [[nodiscard]] const QVector<EffectDescriptor>& descriptors() const;
    [[nodiscard]] const EffectDescriptor* descriptor(const QString& typeId) const;
    [[nodiscard]] std::unique_ptr<Effect> create(const QString& typeId) const;
    [[nodiscard]] bool validate(QString* error = nullptr) const;

private:
    EffectRegistry();
    QVector<EffectDescriptor> m_descriptors;
};

} // namespace vt
