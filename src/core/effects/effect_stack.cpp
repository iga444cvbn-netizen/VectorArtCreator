#include "core/effects/effect_stack.h"

#include <utility>

namespace vt {

EffectStack::EffectStack(const EffectStack& other)
{
    m_effects.reserve(other.m_effects.size());
    for (const auto& effect : other.m_effects) {
        if (effect) {
            m_effects.push_back(effect->clone());
        }
    }
}

EffectStack& EffectStack::operator=(const EffectStack& other)
{
    if (this == &other) {
        return *this;
    }
    EffectStack copy(other);
    *this = std::move(copy);
    return *this;
}

int EffectStack::size() const
{
    return static_cast<int>(m_effects.size());
}

bool EffectStack::isEmpty() const
{
    return m_effects.empty();
}

Effect* EffectStack::at(int index)
{
    if (index < 0 || index >= size()) {
        return nullptr;
    }
    return m_effects[static_cast<size_t>(index)].get();
}

const Effect* EffectStack::at(int index) const
{
    if (index < 0 || index >= size()) {
        return nullptr;
    }
    return m_effects[static_cast<size_t>(index)].get();
}

void EffectStack::append(std::unique_ptr<Effect> effect)
{
    if (effect) {
        m_effects.push_back(std::move(effect));
    }
}

void EffectStack::insert(int index, std::unique_ptr<Effect> effect)
{
    if (!effect || index < 0 || index > size()) {
        return;
    }
    m_effects.insert(m_effects.begin() + index, std::move(effect));
}

std::unique_ptr<Effect> EffectStack::takeAt(int index)
{
    if (index < 0 || index >= size()) {
        return nullptr;
    }
    auto iterator = m_effects.begin() + index;
    std::unique_ptr<Effect> result = std::move(*iterator);
    m_effects.erase(iterator);
    return result;
}

void EffectStack::removeAt(int index)
{
    if (index < 0 || index >= size()) {
        return;
    }
    m_effects.erase(m_effects.begin() + index);
}

void EffectStack::move(int from, int to)
{
    if (from < 0 || from >= size() || to < 0 || to >= size() || from == to) {
        return;
    }
    auto item = std::move(m_effects[static_cast<size_t>(from)]);
    m_effects.erase(m_effects.begin() + from);
    m_effects.insert(m_effects.begin() + to, std::move(item));
}

void EffectStack::clear()
{
    m_effects.clear();
}

void EffectStack::apply(VectorGeometry& geometry) const
{
    geometry.recomputeBounds();
    const EffectContext context{geometry.referenceBounds, geometry.referenceHeight};
    for (const auto& effect : m_effects) {
        if (!effect || !effect->enabled) {
            continue;
        }
        if (effect->scope.kind == EffectScopeKind::WholeObject) {
            effect->apply(geometry, context);
            continue;
        }

        QVector<int> selectedIndices;
        VectorGeometry scopedGeometry;
        scopedGeometry.referenceBounds = geometry.referenceBounds;
        scopedGeometry.referenceHeight = geometry.referenceHeight;
        for (int index = 0; index < geometry.pieces.size(); ++index) {
            const GeometryPiece& piece = geometry.pieces.at(index);
            if (effect->scope.includes(piece.sourceClusterStart, piece.sourceClusterLength)) {
                selectedIndices.push_back(index);
                scopedGeometry.pieces.push_back(piece);
            }
        }
        if (scopedGeometry.pieces.isEmpty()) {
            continue;
        }
        scopedGeometry.recomputeBounds();
        effect->apply(scopedGeometry, context);
        for (int index = 0; index < selectedIndices.size()
             && index < scopedGeometry.pieces.size(); ++index) {
            geometry.pieces[selectedIndices.at(index)] = scopedGeometry.pieces.at(index);
        }
    }
    geometry.recomputeBounds();
}

QJsonArray EffectStack::toJson() const
{
    QJsonArray array;
    for (const auto& effect : m_effects) {
        if (!effect) {
            continue;
        }
        QJsonObject object;
        object.insert(QStringLiteral("type"), effect->typeId());
        object.insert(QStringLiteral("id"), effect->instanceId);
        object.insert(QStringLiteral("enabled"), effect->enabled);
        object.insert(QStringLiteral("masterStrength"), effect->masterStrength);
        object.insert(QStringLiteral("scope"), effect->scope.toJson());
        QJsonArray mask;
        for (const EffectMaskStroke& stroke : effect->maskStrokes) {
            mask.append(stroke.toJson());
        }
        object.insert(QStringLiteral("mask"), mask);
        object.insert(QStringLiteral("maskInverted"), effect->maskInverted);
        object.insert(QStringLiteral("parameters"), effect->parametersToJson());
        array.append(object);
    }
    return array;
}

EffectStack EffectStack::fromJson(const QJsonArray& array, QString* error)
{
    EffectStack stack;
    for (int index = 0; index < array.size(); ++index) {
        const QJsonValue value = array.at(index);
        if (!value.isObject()) {
            if (error) {
                *error = QStringLiteral("Effect entry %1 is not an object.").arg(index);
            }
            return {};
        }
        std::unique_ptr<Effect> effect = effectFromJson(value.toObject(), error);
        if (!effect) {
            return {};
        }
        stack.append(std::move(effect));
    }
    return stack;
}

} // namespace vt
