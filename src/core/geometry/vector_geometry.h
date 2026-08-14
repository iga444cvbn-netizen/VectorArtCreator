#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QTransform>
#include <QVector>
#include <QString>

namespace vt {

struct GeometryPiece {
    QPainterPath path;
    int sourceGlyphIndex = -1;
    int sourceClusterStart = -1;
    int sourceClusterLength = 1;
    int sourceLineIndex = 0;
    QPointF anchor;
    // Immutable source-layout anchor. Effects may move anchor, but never this
    // reference used by later effect normalization/order semantics.
    QPointF originalAnchor;
    // Source-layout data retained until the optional path-layout stage. It is
    // derived metadata, not persistent document state; effects and deformation
    // operate on the transformed path/anchor below it.
    QPointF layoutOrigin;
    qreal layoutAdvance = 0.0;
    // Stable reference metadata for the current effect stage. The source
    // anchor above remains immutable; path layout replaces this reference with
    // the post-path anchor so ordered effects normalize against the geometry
    // they actually receive rather than against pre-path text coordinates.
    QPointF effectReferenceAnchor;
    bool hasEffectReferenceAnchor = false;
    // Transient normalized position along the actual path traversal. This is
    // derived by PathLayoutEngine for progression-dependent effects and is
    // never persistent document state.
    qreal effectReferenceProgress = 0.0;
    bool hasEffectReferenceProgress = false;
    qreal opacityMultiplier = 1.0;
    int generationDepth = 0;
    QString generatorEffectId;
};

class VectorGeometry {
public:
    QVector<GeometryPiece> pieces;
    // Immutable base-local normalization bounds; current geometry bounds are
    // tracked separately in bounds.
    QRectF referenceBounds;
    QRectF bounds;
    qreal referenceHeight = 1.0;

    void setReferenceBounds(const QRectF& value);
    void recomputeBounds();
    void translatePiece(int index, const QPointF& delta);
    void transformPiece(int index, const QTransform& transform);
    void transformAll(const QTransform& transform);

    [[nodiscard]] QPainterPath combinedPath() const;
    [[nodiscard]] bool hasVisibleGeometry() const;
};

} // namespace vt
