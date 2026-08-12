#include "core/effects/effect_registry.h"

#include "core/effects/glyph_jitter_effect.h"
#include "core/effects/procedural_effect.h"
#include "core/effects/stretch_effect.h"
#include "core/effects/wave_effect.h"

#include <QSet>

namespace vt {

namespace {

EffectDescriptor procedural(const char* id, const char* name, ProceduralEffect::Mode mode,
                            const char* category = "Expressive")
{
    EffectDescriptor descriptor;
    descriptor.typeId = QString::fromLatin1(id);
    descriptor.displayName = QString::fromLatin1(name);
    descriptor.category = QString::fromLatin1(category);
    descriptor.shortDescription = QStringLiteral("Applies a deterministic per-glyph expressive transformation.");
    descriptor.searchTags = {descriptor.category.toLower(), QStringLiteral("typography"), QStringLiteral("creative")};
    descriptor.domain = EffectDomain::GlyphTransform;
    descriptor.basicParameterIds = {QStringLiteral("strength"), QStringLiteral("frequency")};
    descriptor.iconName = QStringLiteral("effect-glyph");
    descriptor.factory = [id = descriptor.typeId, name = descriptor.displayName, mode] {
        return std::make_unique<ProceduralEffect>(id, name, mode);
    };
    return descriptor;
}

} // namespace

const EffectRegistry& EffectRegistry::instance()
{
    static const EffectRegistry registry;
    return registry;
}

EffectRegistry::EffectRegistry()
{
    m_descriptors = {
        {QStringLiteral("wave"), QStringLiteral("Wave"), QStringLiteral("Motion"),
         QStringLiteral("Moves glyphs along a sine wave."), {QStringLiteral("wave"), QStringLiteral("sine"), QStringLiteral("motion")},
         EffectDomain::GlyphTransform, true, true, true, true, true,
         {QStringLiteral("amplitude"), QStringLiteral("frequency")}, {0.0, 2.0}, QStringLiteral("effect-wave"),
         [] { return std::make_unique<WaveEffect>(); }},
        {QStringLiteral("glyphJitter"), QStringLiteral("Glyph Jitter"), QStringLiteral("Texture"),
         QStringLiteral("Adds stable random offsets and rotations."), {QStringLiteral("jitter"), QStringLiteral("random"), QStringLiteral("texture")},
         EffectDomain::GlyphTransform, true, true, true, true, true,
         {QStringLiteral("amount"), QStringLiteral("seed")}, {0.0, 2.0}, QStringLiteral("effect-jitter"),
         [] { return std::make_unique<GlyphJitterEffect>(); }},
        {QStringLiteral("stretch"), QStringLiteral("Global Stretch"), QStringLiteral("Shape"),
         QStringLiteral("Scales the complete outline around its centre."), {QStringLiteral("stretch"), QStringLiteral("scale"), QStringLiteral("shape")},
         EffectDomain::Geometry, true, true, true, true, true,
         {QStringLiteral("horizontal"), QStringLiteral("vertical")}, {0.0, 2.0}, QStringLiteral("effect-stretch"),
         [] { return std::make_unique<StretchEffect>(); }},
        procedural("bounce", "Bounce", ProceduralEffect::Mode::Bounce),
        procedural("staircase", "Staircase", ProceduralEffect::Mode::Staircase),
        procedural("randomOffset", "Random Offset", ProceduralEffect::Mode::RandomOffset),
        procedural("randomRotation", "Random Rotation", ProceduralEffect::Mode::RandomRotation),
        procedural("randomScale", "Random Scale", ProceduralEffect::Mode::RandomScale),
        procedural("horizontalSpread", "Horizontal Spread", ProceduralEffect::Mode::HorizontalSpread),
        procedural("verticalSpread", "Vertical Spread", ProceduralEffect::Mode::VerticalSpread),
        procedural("arc", "Arc", ProceduralEffect::Mode::Arc),
        procedural("zigzag", "Zigzag", ProceduralEffect::Mode::Zigzag),
        procedural("sineRotation", "Sine Rotation", ProceduralEffect::Mode::SineRotation),
        procedural("crescendo", "Crescendo", ProceduralEffect::Mode::Crescendo),
        procedural("shrink", "Shrink", ProceduralEffect::Mode::Shrink),
        procedural("skew", "Skew", ProceduralEffect::Mode::Skew),
        procedural("compression", "Compression", ProceduralEffect::Mode::Compression),
        procedural("expandCenter", "Expand from Center", ProceduralEffect::Mode::ExpandCenter),
        procedural("squeezeCenter", "Squeeze to Center", ProceduralEffect::Mode::SqueezeCenter),
        procedural("baselineDrift", "Baseline Drift", ProceduralEffect::Mode::BaselineDrift),
        procedural("alternatingTilt", "Alternating Tilt", ProceduralEffect::Mode::AlternatingTilt),
    };
}

const QVector<EffectDescriptor>& EffectRegistry::descriptors() const { return m_descriptors; }

const EffectDescriptor* EffectRegistry::descriptor(const QString& typeId) const
{
    for (const EffectDescriptor& entry : m_descriptors) {
        if (entry.typeId == typeId) return &entry;
    }
    return nullptr;
}

std::unique_ptr<Effect> EffectRegistry::create(const QString& typeId) const
{
    const EffectDescriptor* entry = descriptor(typeId);
    return entry && entry->factory ? entry->factory() : nullptr;
}

bool EffectRegistry::validate(QString* error) const
{
    QSet<QString> ids;
    for (const EffectDescriptor& entry : m_descriptors) {
        if (entry.typeId.isEmpty() || !entry.factory || ids.contains(entry.typeId)) {
            if (error) *error = QStringLiteral("Effect registry has a missing or duplicate type ID.");
            return false;
        }
        ids.insert(entry.typeId);
        const std::unique_ptr<Effect> effect = entry.factory();
        if (!effect || effect->typeId() != entry.typeId || effect->domain() != entry.domain) {
            if (error) *error = QStringLiteral("Effect factory does not match descriptor '%1'.").arg(entry.typeId);
            return false;
        }
    }
    return true;
}

} // namespace vt
