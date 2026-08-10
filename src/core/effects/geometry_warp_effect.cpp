#include "core/effects/geometry_warp_effect.h"

#include <cmath>

namespace vt {

GeometryWarpEffect::GeometryWarpEffect(QString typeId, QString name, Mode mode)
    : m_typeId(std::move(typeId)), m_name(std::move(name)), m_mode(mode) {}
QString GeometryWarpEffect::typeId() const { return m_typeId; }
QString GeometryWarpEffect::displayName() const { return m_name; }
EffectDomain GeometryWarpEffect::domain() const { return EffectDomain::Geometry; }
std::unique_ptr<Effect> GeometryWarpEffect::clone() const { return std::make_unique<GeometryWarpEffect>(*this); }

QVector<EffectParameter> GeometryWarpEffect::parameterDefinitions() const {
    return {{QStringLiteral("amount"), QStringLiteral("Amount"), m_amount, -1.0, 1.0, 0.01, false},
            {QStringLiteral("frequency"), QStringLiteral("Frequency"), m_frequency, 0.1, 8.0, 0.1, false},
            {QStringLiteral("centerX"), QStringLiteral("Center X"), m_centerX, 0.0, 1.0, 0.01, false},
            {QStringLiteral("centerY"), QStringLiteral("Center Y"), m_centerY, 0.0, 1.0, 0.01, false},
            {QStringLiteral("radius"), QStringLiteral("Radius"), m_radius, 0.1, 2.0, 0.01, false},
            {QStringLiteral("falloff"), QStringLiteral("Falloff"), m_falloff, 0.1, 4.0, 0.01, false},
            {QStringLiteral("seed"), QStringLiteral("Seed"), double(m_seed), 0, 4294967295.0, 1, true}};
}
bool GeometryWarpEffect::setParameter(const QString& id, double value) {
    if (id == QStringLiteral("amount")) m_amount = qBound<qreal>(-1, value, 1);
    else if (id == QStringLiteral("frequency")) m_frequency = qBound<qreal>(0.1, value, 8);
    else if (id == QStringLiteral("centerX")) m_centerX = qBound<qreal>(0, value, 1);
    else if (id == QStringLiteral("centerY")) m_centerY = qBound<qreal>(0, value, 1);
    else if (id == QStringLiteral("radius")) m_radius = qBound<qreal>(0.1, value, 2);
    else if (id == QStringLiteral("falloff")) m_falloff = qBound<qreal>(0.1, value, 4);
    else if (id == QStringLiteral("seed")) m_seed = quint32(qBound(0.0, value, 4294967295.0));
    else return false;
    return true;
}
QJsonObject GeometryWarpEffect::parametersToJson() const { return {{"amount",m_amount},{"frequency",m_frequency},{"centerX",m_centerX},{"centerY",m_centerY},{"radius",m_radius},{"falloff",m_falloff},{"seed",double(m_seed)}}; }
bool GeometryWarpEffect::parametersFromJson(const QJsonObject& o, QString* error) {
    for (const auto& p : parameterDefinitions()) if (!setParameter(p.id, o.value(p.id).toDouble(p.value))) { if (error) *error=QStringLiteral("Invalid warp parameter."); return false; } return true;
}
QPointF GeometryWarpEffect::warp(const QPointF& p, const EffectContext& c) const {
    const QRectF r = c.referenceBounds; const qreal w=qMax<qreal>(1,r.width()), h=qMax<qreal>(1,r.height());
    const qreal u=(p.x()-r.left())/w, v=(p.y()-r.top())/h, strength=c.effectiveStrength(*this);
    constexpr qreal pi=3.14159265358979323846;
    if (m_mode==Mode::Bend || m_mode==Mode::Sag || m_mode==Mode::WaveWarp) {
        const qreal f=m_mode==Mode::WaveWarp ? std::sin(2*pi*(u*m_frequency)) : std::sin(pi*u);
        return p+QPointF(0,(m_mode==Mode::Sag ? 1 : -1)*f*m_amount*strength*h);
    }
    if (m_mode==Mode::Bulge || m_mode==Mode::Pinch) {
        const QPointF center(r.left()+m_centerX*w,r.top()+m_centerY*h); const QPointF d=p-center;
        const qreal distance=std::sqrt(d.x()*d.x()+d.y()*d.y); const qreal limit=qMax<qreal>(1,m_radius*h);
        const qreal t=qBound<qreal>(0,1-distance/limit,1); const qreal sign=m_mode==Mode::Bulge?1:-1;
        return center+d*(1+sign*m_amount*strength*std::pow(t,m_falloff)*0.6);
    }
    const qreal n=std::sin((u*12.9898+v*78.233+m_seed)*m_frequency)*0.5+std::sin((u-v)*17.0+m_seed)*0.5;
    if (m_mode==Mode::Melt) return p+QPointF(0,qMax<qreal>(0,n+0.4)*m_amount*strength*h);
    if (m_mode==Mode::Smear) return p+QPointF(n*m_amount*strength*h,0);
    return p+QPointF(n*m_amount*strength*h*0.45, std::cos(n*9)*m_amount*strength*h*0.45);
}
void GeometryWarpEffect::apply(VectorGeometry& geometry, const EffectContext& context) const {
    if (qFuzzyIsNull(context.effectiveStrength(*this))) return;
    for (GeometryPiece& piece : geometry.pieces) {
        for (int i=0;i<piece.path.elementCount();++i) { const auto e=piece.path.elementAt(i); const QPointF q=warp({e.x,e.y},context); piece.path.setElementPositionAt(i,q.x(),q.y()); }
        piece.anchor=warp(piece.anchor,context);
    }
    geometry.recomputeBounds();
}
} // namespace vt
