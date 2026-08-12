#include "tests/support/semantic_geometry.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QVariant>

#include <cmath>
#include <limits>
#include <utility>

namespace vt::test {
namespace {

qint64 quantize(qreal value, qreal quantum)
{
    if (std::isnan(value)) return std::numeric_limits<qint64>::min();
    if (value == std::numeric_limits<qreal>::infinity()) return std::numeric_limits<qint64>::max();
    if (value == -std::numeric_limits<qreal>::infinity()) return std::numeric_limits<qint64>::min() + 1;
    const long double scaled = static_cast<long double>(value) / quantum;
    if (scaled >= static_cast<long double>(std::numeric_limits<qint64>::max() - 1)) {
        return std::numeric_limits<qint64>::max() - 1;
    }
    if (scaled <= static_cast<long double>(std::numeric_limits<qint64>::min() + 2)) {
        return std::numeric_limits<qint64>::min() + 2;
    }
    return qRound64(static_cast<qreal>(scaled));
}

QuantizedPoint point(const QPointF& value, qreal quantum)
{
    return {quantize(value.x(), quantum), quantize(value.y(), quantum)};
}

QuantizedRect rect(const QRectF& value, qreal quantum)
{
    return {quantize(value.x(), quantum), quantize(value.y(), quantum),
            quantize(value.width(), quantum), quantize(value.height(), quantum)};
}

QuantizedTransform transform(const QTransform& value, qreal quantum)
{
    return {quantize(value.m11(), quantum), quantize(value.m12(), quantum),
            quantize(value.m13(), quantum), quantize(value.m21(), quantum),
            quantize(value.m22(), quantum), quantize(value.m23(), quantum),
            quantize(value.m31(), quantum), quantize(value.m32(), quantum),
            quantize(value.m33(), quantum)};
}

QString scalar(qint64 value, qreal quantum)
{
    if (value == std::numeric_limits<qint64>::min()) return QStringLiteral("NaN");
    if (value == std::numeric_limits<qint64>::max()) return QStringLiteral("+Inf");
    if (value == std::numeric_limits<qint64>::min() + 1) return QStringLiteral("-Inf");
    return QString::number(static_cast<qreal>(value) * quantum, 'g', 12);
}

bool mismatch(bool condition, const QString& path, const QString& expected,
              const QString& actual, QString* difference)
{
    if (!condition) return false;
    if (difference) {
        *difference = QStringLiteral("expected %1 = %2\nactual = %3")
                          .arg(path, expected, actual);
    }
    return true;
}

bool comparePoint(const QuantizedPoint& expected, const QuantizedPoint& actual,
                  qreal quantum, const QString& path, QString* difference)
{
    if (mismatch(expected.x != actual.x, path + QStringLiteral(".x"),
                 scalar(expected.x, quantum), scalar(actual.x, quantum), difference)) return false;
    if (mismatch(expected.y != actual.y, path + QStringLiteral(".y"),
                 scalar(expected.y, quantum), scalar(actual.y, quantum), difference)) return false;
    return true;
}

bool compareRect(const QuantizedRect& expected, const QuantizedRect& actual,
                 qreal quantum, const QString& path, QString* difference)
{
    const qint64 expectedValues[] = {expected.x, expected.y, expected.width, expected.height};
    const qint64 actualValues[] = {actual.x, actual.y, actual.width, actual.height};
    const char* names[] = {"x", "y", "width", "height"};
    for (int index = 0; index < 4; ++index) {
        if (mismatch(expectedValues[index] != actualValues[index],
                     path + QLatin1Char('.') + QString::fromLatin1(names[index]),
                     scalar(expectedValues[index], quantum), scalar(actualValues[index], quantum), difference)) {
            return false;
        }
    }
    return true;
}

bool compareTransform(const QuantizedTransform& expected, const QuantizedTransform& actual,
                      qreal quantum, const QString& path, QString* difference)
{
    const qint64 expectedValues[] = {expected.m11, expected.m12, expected.m13,
                                    expected.m21, expected.m22, expected.m23,
                                    expected.m31, expected.m32, expected.m33};
    const qint64 actualValues[] = {actual.m11, actual.m12, actual.m13,
                                  actual.m21, actual.m22, actual.m23,
                                  actual.m31, actual.m32, actual.m33};
    const char* names[] = {"m11", "m12", "m13", "m21", "m22", "m23", "m31", "m32", "m33"};
    for (int index = 0; index < 9; ++index) {
        if (mismatch(expectedValues[index] != actualValues[index],
                     path + QLatin1Char('.') + QString::fromLatin1(names[index]),
                     scalar(expectedValues[index], quantum), scalar(actualValues[index], quantum), difference)) {
            return false;
        }
    }
    return true;
}

template <typename T>
bool compareValue(const T& expected, const T& actual, const QString& path, QString* difference)
{
    if (expected == actual) return true;
    if (difference) {
        *difference = QStringLiteral("expected %1 = %2\nactual = %3")
                          .arg(path, QVariant::fromValue(expected).toString(),
                               QVariant::fromValue(actual).toString());
    }
    return false;
}

} // namespace

GeometrySignature geometrySignature(const VectorGeometry& geometry, qreal quantum)
{
    GeometrySignature signature;
    signature.quantum = quantum;
    signature.referenceBounds = rect(geometry.referenceBounds, quantum);
    signature.bounds = rect(geometry.bounds, quantum);
    signature.referenceHeight = quantize(geometry.referenceHeight, quantum);
    signature.pieces.reserve(geometry.pieces.size());
    for (const GeometryPiece& piece : geometry.pieces) {
        GeometryPieceSignature item;
        item.sourceGlyphIndex = piece.sourceGlyphIndex;
        item.sourceClusterStart = piece.sourceClusterStart;
        item.sourceClusterLength = piece.sourceClusterLength;
        item.sourceLineIndex = piece.sourceLineIndex;
        item.anchor = point(piece.anchor, quantum);
        item.originalAnchor = point(piece.originalAnchor, quantum);
        item.opacity = quantize(piece.opacityMultiplier, quantum);
        item.generationDepth = piece.generationDepth;
        item.generatorEffectId = piece.generatorEffectId;
        item.path.reserve(piece.path.elementCount());
        for (int index = 0; index < piece.path.elementCount(); ++index) {
            const QPainterPath::Element element = piece.path.elementAt(index);
            item.path.push_back({static_cast<int>(element.type), point(QPointF(element.x, element.y), quantum)});
        }
        signature.pieces.push_back(std::move(item));
    }
    return signature;
}

SceneObjectSignature sceneObjectSignature(const SceneObjectGeometry& object, qreal quantum)
{
    SceneObjectSignature signature;
    signature.quantum = quantum;
    signature.objectId = object.objectId;
    signature.pageId = object.pageId;
    signature.layerId = object.layerId;
    signature.sourceText = object.sourceText;
    signature.transform = {point(object.transform.position, quantum),
                           quantize(object.transform.rotation, quantum),
                           point(object.transform.scale, quantum),
                           point(object.transform.pivotLocal, quantum),
                           object.transform.hasPivot};
    signature.frame = {rect(object.frame.baseLocalBounds, quantum),
                       rect(object.frame.currentLocalBounds, quantum),
                       rect(object.frame.pageAabb(), quantum),
                       point(object.frame.pivotLocal, quantum),
                       transform(object.frame.localToPage, quantum),
                       transform(object.frame.pageToLocal, quantum)};
    signature.geometry = geometrySignature(object.geometry, quantum);
    signature.fill = object.fill.rgba();
    signature.visible = object.visible;
    signature.locked = object.locked;
    return signature;
}

bool compareGeometry(const GeometrySignature& expected, const GeometrySignature& actual,
                     QString* difference)
{
    if (mismatch(expected.quantum != actual.quantum, QStringLiteral("quantum"),
                 QString::number(expected.quantum), QString::number(actual.quantum), difference)) return false;
    const qreal quantum = expected.quantum;
    if (!compareRect(expected.referenceBounds, actual.referenceBounds, quantum,
                     QStringLiteral("referenceBounds"), difference)) return false;
    if (!compareRect(expected.bounds, actual.bounds, quantum, QStringLiteral("bounds"), difference)) return false;
    if (mismatch(expected.referenceHeight != actual.referenceHeight, QStringLiteral("referenceHeight"),
                 scalar(expected.referenceHeight, quantum), scalar(actual.referenceHeight, quantum), difference)) return false;
    if (mismatch(expected.pieces.size() != actual.pieces.size(), QStringLiteral("pieceCount"),
                 QString::number(expected.pieces.size()), QString::number(actual.pieces.size()), difference)) return false;
    for (int pieceIndex = 0; pieceIndex < expected.pieces.size(); ++pieceIndex) {
        const auto& left = expected.pieces.at(pieceIndex);
        const auto& right = actual.pieces.at(pieceIndex);
        const QString prefix = QStringLiteral("piece[%1]").arg(pieceIndex);
        if (!compareValue(left.sourceGlyphIndex, right.sourceGlyphIndex,
                          prefix + QStringLiteral(".sourceGlyphIndex"), difference)) return false;
        if (!compareValue(left.sourceClusterStart, right.sourceClusterStart,
                          prefix + QStringLiteral(".sourceClusterStart"), difference)) return false;
        if (!compareValue(left.sourceClusterLength, right.sourceClusterLength,
                          prefix + QStringLiteral(".sourceClusterLength"), difference)) return false;
        if (!compareValue(left.sourceLineIndex, right.sourceLineIndex,
                          prefix + QStringLiteral(".sourceLineIndex"), difference)) return false;
        if (!comparePoint(left.anchor, right.anchor, quantum, prefix + QStringLiteral(".anchor"), difference)) return false;
        if (!comparePoint(left.originalAnchor, right.originalAnchor, quantum,
                          prefix + QStringLiteral(".originalAnchor"), difference)) return false;
        if (mismatch(left.opacity != right.opacity, prefix + QStringLiteral(".opacity"),
                     scalar(left.opacity, quantum), scalar(right.opacity, quantum), difference)) return false;
        if (!compareValue(left.generationDepth, right.generationDepth,
                          prefix + QStringLiteral(".generationDepth"), difference)) return false;
        if (!compareValue(left.generatorEffectId, right.generatorEffectId,
                          prefix + QStringLiteral(".generatorEffectId"), difference)) return false;
        if (mismatch(left.path.size() != right.path.size(), prefix + QStringLiteral(".pathElementCount"),
                     QString::number(left.path.size()), QString::number(right.path.size()), difference)) return false;
        for (int elementIndex = 0; elementIndex < left.path.size(); ++elementIndex) {
            const QString elementPrefix = prefix + QStringLiteral(".path[%1]").arg(elementIndex);
            if (!compareValue(left.path.at(elementIndex).type, right.path.at(elementIndex).type,
                              elementPrefix + QStringLiteral(".type"), difference)) return false;
            if (!comparePoint(left.path.at(elementIndex).position, right.path.at(elementIndex).position,
                              quantum, elementPrefix + QStringLiteral(".position"), difference)) return false;
        }
    }
    return true;
}

bool compareSceneObject(const SceneObjectSignature& expected, const SceneObjectSignature& actual,
                        QString* difference)
{
    if (mismatch(expected.quantum != actual.quantum, QStringLiteral("scene.quantum"),
                 QString::number(expected.quantum), QString::number(actual.quantum), difference)) return false;
    const qreal quantum = expected.quantum;
#define COMPARE_SCENE_VALUE(field) \
    if (!compareValue(expected.field, actual.field, QStringLiteral(#field), difference)) return false
    COMPARE_SCENE_VALUE(objectId);
    COMPARE_SCENE_VALUE(pageId);
    COMPARE_SCENE_VALUE(layerId);
    COMPARE_SCENE_VALUE(sourceText);
    COMPARE_SCENE_VALUE(fill);
    COMPARE_SCENE_VALUE(visible);
    COMPARE_SCENE_VALUE(locked);
#undef COMPARE_SCENE_VALUE
    if (!comparePoint(expected.transform.position, actual.transform.position, quantum,
                      QStringLiteral("transform.position"), difference)) return false;
    if (mismatch(expected.transform.rotation != actual.transform.rotation,
                 QStringLiteral("transform.rotation"), scalar(expected.transform.rotation, quantum),
                 scalar(actual.transform.rotation, quantum), difference)) return false;
    if (!comparePoint(expected.transform.scale, actual.transform.scale, quantum,
                      QStringLiteral("transform.scale"), difference)) return false;
    if (!comparePoint(expected.transform.pivotLocal, actual.transform.pivotLocal, quantum,
                      QStringLiteral("transform.pivotLocal"), difference)) return false;
    if (!compareValue(expected.transform.hasPivot, actual.transform.hasPivot,
                      QStringLiteral("transform.hasPivot"), difference)) return false;
    if (!compareRect(expected.frame.baseLocalBounds, actual.frame.baseLocalBounds, quantum,
                     QStringLiteral("frame.baseLocalBounds"), difference)) return false;
    if (!compareRect(expected.frame.currentLocalBounds, actual.frame.currentLocalBounds, quantum,
                     QStringLiteral("frame.currentLocalBounds"), difference)) return false;
    if (!compareRect(expected.frame.pageAabb, actual.frame.pageAabb, quantum,
                     QStringLiteral("frame.pageAabb"), difference)) return false;
    if (!comparePoint(expected.frame.pivotLocal, actual.frame.pivotLocal, quantum,
                      QStringLiteral("frame.pivotLocal"), difference)) return false;
    if (!compareTransform(expected.frame.localToPage, actual.frame.localToPage, quantum,
                          QStringLiteral("frame.localToPage"), difference)) return false;
    if (!compareTransform(expected.frame.pageToLocal, actual.frame.pageToLocal, quantum,
                          QStringLiteral("frame.pageToLocal"), difference)) return false;
    return compareGeometry(expected.geometry, actual.geometry, difference);
}

QByteArray geometryDigest(const GeometrySignature& signature)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_4);
    stream << signature.quantum
           << signature.referenceBounds.x << signature.referenceBounds.y
           << signature.referenceBounds.width << signature.referenceBounds.height
           << signature.bounds.x << signature.bounds.y
           << signature.bounds.width << signature.bounds.height
           << signature.referenceHeight << qint32(signature.pieces.size());
    for (const auto& piece : signature.pieces) {
        stream << piece.sourceGlyphIndex << piece.sourceClusterStart << piece.sourceClusterLength
               << piece.sourceLineIndex << piece.anchor.x << piece.anchor.y
               << piece.originalAnchor.x << piece.originalAnchor.y << piece.opacity
               << piece.generationDepth << piece.generatorEffectId << qint32(piece.path.size());
        for (const auto& element : piece.path) {
            stream << element.type << element.position.x << element.position.y;
        }
    }
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

} // namespace vt::test
