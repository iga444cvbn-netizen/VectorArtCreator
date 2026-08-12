#pragma once

#include "core/geometry/vector_geometry.h"
#include "core/scene/scene_geometry.h"

#include <QByteArray>
#include <QVector>

namespace vt::test {

struct QuantizedPoint {
    qint64 x = 0;
    qint64 y = 0;
    friend bool operator==(const QuantizedPoint&, const QuantizedPoint&) = default;
};

struct QuantizedRect {
    qint64 x = 0;
    qint64 y = 0;
    qint64 width = 0;
    qint64 height = 0;
    friend bool operator==(const QuantizedRect&, const QuantizedRect&) = default;
};

struct QuantizedTransform {
    qint64 m11 = 0;
    qint64 m12 = 0;
    qint64 m13 = 0;
    qint64 m21 = 0;
    qint64 m22 = 0;
    qint64 m23 = 0;
    qint64 m31 = 0;
    qint64 m32 = 0;
    qint64 m33 = 0;
    friend bool operator==(const QuantizedTransform&, const QuantizedTransform&) = default;
};

struct PathElementSignature {
    int type = 0;
    QuantizedPoint position;
    friend bool operator==(const PathElementSignature&, const PathElementSignature&) = default;
};

struct GeometryPieceSignature {
    int sourceGlyphIndex = -1;
    int sourceClusterStart = -1;
    int sourceClusterLength = 1;
    int sourceLineIndex = 0;
    QuantizedPoint anchor;
    QuantizedPoint originalAnchor;
    qint64 opacity = 0;
    int generationDepth = 0;
    QString generatorEffectId;
    QVector<PathElementSignature> path;
    friend bool operator==(const GeometryPieceSignature&, const GeometryPieceSignature&) = default;
};

struct GeometrySignature {
    qreal quantum = 1.0e-6;
    QuantizedRect referenceBounds;
    QuantizedRect bounds;
    qint64 referenceHeight = 0;
    QVector<GeometryPieceSignature> pieces;
    friend bool operator==(const GeometrySignature&, const GeometrySignature&) = default;
};

struct ObjectTransformSignature {
    QuantizedPoint position;
    qint64 rotation = 0;
    QuantizedPoint scale;
    QuantizedPoint pivotLocal;
    bool hasPivot = false;
    friend bool operator==(const ObjectTransformSignature&, const ObjectTransformSignature&) = default;
};

struct ObjectFrameSignature {
    quint64 spatialRevision = 0;
    QuantizedRect baseLocalBounds;
    QuantizedRect currentLocalBounds;
    QuantizedRect pageAabb;
    QuantizedPoint pivotLocal;
    QuantizedTransform localToPage;
    QuantizedTransform pageToLocal;
    friend bool operator==(const ObjectFrameSignature&, const ObjectFrameSignature&) = default;
};

struct SceneObjectSignature {
    qreal quantum = 1.0e-6;
    quint64 spatialRevision = 0;
    QString objectId;
    QString pageId;
    QString layerId;
    QString sourceText;
    ObjectTransformSignature transform;
    ObjectFrameSignature frame;
    GeometrySignature geometry;
    QRgb fill = 0;
    bool visible = true;
    bool locked = false;
    friend bool operator==(const SceneObjectSignature&, const SceneObjectSignature&) = default;
};

[[nodiscard]] GeometrySignature geometrySignature(const VectorGeometry& geometry,
                                                  qreal quantum = 1.0e-6);
[[nodiscard]] SceneObjectSignature sceneObjectSignature(const SceneObjectGeometry& object,
                                                        qreal quantum = 1.0e-6);

// Structured comparison reports the first semantic difference instead of only
// returning an opaque digest mismatch.
[[nodiscard]] bool compareGeometry(const GeometrySignature& expected,
                                   const GeometrySignature& actual,
                                   QString* difference = nullptr);
[[nodiscard]] bool compareSceneObject(const SceneObjectSignature& expected,
                                      const SceneObjectSignature& actual,
                                      QString* difference = nullptr);

// Useful for compact replay logs only. Tests should prefer the structured
// comparators above for their independent, field-level diagnostics.
[[nodiscard]] QByteArray geometryDigest(const GeometrySignature& signature);

} // namespace vt::test
