#include "core/effects/effect_stack.h"

#include <QLineF>
#include <QSet>

#include <cmath>
#include <utility>

namespace vt {

namespace {

qreal distanceToSegment(const QPointF& point, const QPointF& start, const QPointF& end)
{
    const QPointF segment = end - start;
    const qreal lengthSquared = QPointF::dotProduct(segment, segment);
    if (qFuzzyIsNull(lengthSquared)) {
        return QLineF(point, start).length();
    }
    const qreal t = qBound<qreal>(0.0, QPointF::dotProduct(point - start, segment) / lengthSquared, 1.0);
    return QLineF(point, start + segment * t).length();
}

qreal maskInfluence(const Effect& effect, const QPainterPath& path, const QPointF& anchor)
{
    qreal influence = 1.0;
    for (const EffectMaskStroke& stroke : effect.maskStrokes) {
        if (stroke.points.isEmpty() || stroke.radius <= 0.0) {
            continue;
        }
        qreal distance = QLineF(anchor, stroke.points.front()).length();
        for (int index = 1; index < stroke.points.size(); ++index) {
            distance = qMin(distance,
                            distanceToSegment(anchor,
                                              stroke.points.at(index - 1),
                                              stroke.points.at(index)));
        }
        // The anchor is only a layout convenience.  Use actual contour
        // proximity so a brush crossing a large glyph influences that glyph.
        const QRectF bounds = path.boundingRect();
        if (!bounds.isEmpty()) {
            const QRectF expanded = bounds.adjusted(-stroke.radius, -stroke.radius,
                                                    stroke.radius, stroke.radius);
            if (expanded.contains(stroke.points.front())) {
                distance = 0.0;
            }
            for (int index = 1; index < stroke.points.size(); ++index) {
                const QLineF line(stroke.points.at(index - 1), stroke.points.at(index));
                if (expanded.intersects(QRectF(line.p1(), line.p2()).normalized())) {
                    distance = 0.0;
                    break;
                }
            }
        }
        if (distance >= stroke.radius) {
            continue;
        }
        const qreal innerRadius = stroke.radius * (0.1 + 0.85 * stroke.hardness);
        const qreal normalized = innerRadius >= stroke.radius
            ? 1.0
            : qBound<qreal>(0.0,
                            (stroke.radius - distance) / (stroke.radius - innerRadius),
                            1.0);
        const qreal falloff = normalized * normalized * (3.0 - 2.0 * normalized);
        const qreal amount = qBound<qreal>(0.0, stroke.opacity, 1.0) * falloff;
        influence += stroke.restore ? amount : -amount;
    }
    const qreal bounded = qBound<qreal>(0.0, influence, 1.0);
    return effect.maskInverted ? 1.0 - bounded : bounded;
}

QPainterPath blendPath(const QPainterPath& before, const QPainterPath& after, qreal amount)
{
    if (before.elementCount() != after.elementCount()) {
        return amount >= 0.5 ? after : before;
    }
    QPainterPath result = before;
    for (int index = 0; index < before.elementCount(); ++index) {
        const QPainterPath::Element left = before.elementAt(index);
        const QPainterPath::Element right = after.elementAt(index);
        result.setElementPositionAt(index,
                                    left.x + (right.x - left.x) * amount,
                                    left.y + (right.y - left.y) * amount);
    }
    return result;
}

void blendPiece(GeometryPiece* destination,
                const GeometryPiece& before,
                const GeometryPiece& after,
                qreal amount)
{
    if (!destination) {
        return;
    }
    destination->path = blendPath(before.path, after.path, amount);
    destination->anchor = before.anchor + (after.anchor - before.anchor) * amount;
}

void applyMaskedEffect(const Effect& effect,
                       VectorGeometry* geometry,
                       const EffectContext& context,
                       const QVector<int>& selectedIndices)
{
    if (!geometry) {
        return;
    }
    VectorGeometry before = *geometry;
    VectorGeometry after = *geometry;
    effect.apply(after, context);
    for (int index : selectedIndices) {
        if (index < 0 || index >= geometry->pieces.size() || index >= after.pieces.size()) {
            continue;
        }
        const qreal influence = maskInfluence(effect, before.pieces.at(index).path,
                                              before.pieces.at(index).anchor);
        blendPiece(&geometry->pieces[index], before.pieces.at(index), after.pieces.at(index), influence);
    }
}

}

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

Effect* EffectStack::byInstanceId(const QString& instanceId)
{
    const int index = indexByInstanceId(instanceId);
    return at(index);
}

const Effect* EffectStack::byInstanceId(const QString& instanceId) const
{
    const int index = indexByInstanceId(instanceId);
    return at(index);
}

int EffectStack::indexByInstanceId(const QString& instanceId) const
{
    if (instanceId.isEmpty()) {
        return -1;
    }
    for (int index = 0; index < size(); ++index) {
        const Effect* effect = at(index);
        if (effect && effect->instanceId == instanceId) {
            return index;
        }
    }
    return -1;
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

bool EffectStack::hasUniqueInstanceIds() const
{
    QSet<QString> ids;
    for (const auto& effect : m_effects) {
        if (!effect || effect->instanceId.isEmpty() || ids.contains(effect->instanceId)) return false;
        ids.insert(effect->instanceId);
    }
    return true;
}

void EffectStack::apply(VectorGeometry& geometry, qreal stackStrength) const
{
    geometry.recomputeBounds();
    const EffectContext context{geometry.referenceBounds, geometry.referenceHeight,
                                qBound<qreal>(0.0, stackStrength, 2.0)};
    for (const auto& effect : m_effects) {
        if (!effect || !effect->enabled) {
            continue;
        }
        if (effect->scope.kind == EffectScopeKind::WholeObject && effect->maskStrokes.isEmpty()) {
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
        if (effect->maskStrokes.isEmpty()) {
            effect->apply(scopedGeometry, context);
            for (int index = 0; index < selectedIndices.size()
                 && index < scopedGeometry.pieces.size(); ++index) {
                geometry.pieces[selectedIndices.at(index)] = scopedGeometry.pieces.at(index);
            }
            continue;
        }
        applyMaskedEffect(*effect, &scopedGeometry, context,
                          [&selectedIndices] {
                              QVector<int> local;
                              local.reserve(selectedIndices.size());
                              for (int index = 0; index < selectedIndices.size(); ++index) {
                                  local.push_back(index);
                              }
                              return local;
                          }());
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
    if (!stack.hasUniqueInstanceIds()) {
        if (error) *error = QStringLiteral("Effect stack contains duplicate or missing instance IDs.");
        return {};
    }
    return stack;
}

} // namespace vt
