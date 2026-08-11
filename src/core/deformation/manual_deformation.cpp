#include "core/deformation/manual_deformation.h"

#include "core/deformation/deformation_evaluator.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QLineF>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace vt {

namespace {

constexpr int MaximumSerializedSamples = 4096;

bool finitePoint(const QPointF& point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

QJsonArray pointToJson(const QPointF& point)
{
    return {point.x(), point.y()};
}

bool pointFromJson(const QJsonValue& value, QPointF* point)
{
    if (!point || !value.isArray()) {
        return false;
    }
    const QJsonArray array = value.toArray();
    if (array.size() != 2) {
        return false;
    }
    const QPointF parsed(array.at(0).toDouble(std::numeric_limits<double>::quiet_NaN()),
                         array.at(1).toDouble(std::numeric_limits<double>::quiet_NaN()));
    if (!finitePoint(parsed)) {
        return false;
    }
    *point = parsed;
    return true;
}

bool finiteValue(qreal value)
{
    return std::isfinite(value);
}

} // namespace

QString brushModeToString(BrushMode mode)
{
    switch (mode) {
    case BrushMode::Push:
        return QStringLiteral("push");
    case BrushMode::Pull:
        return QStringLiteral("pull");
    case BrushMode::Inflate:
        return QStringLiteral("inflate");
    case BrushMode::Pinch:
        return QStringLiteral("pinch");
    case BrushMode::Smooth:
        return QStringLiteral("smooth");
    }
    return QStringLiteral("push");
}

bool brushModeFromString(const QString& value, BrushMode* mode)
{
    if (!mode) {
        return false;
    }
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("push")) {
        *mode = BrushMode::Push;
    } else if (normalized == QStringLiteral("pull")) {
        *mode = BrushMode::Pull;
    } else if (normalized == QStringLiteral("inflate")) {
        *mode = BrushMode::Inflate;
    } else if (normalized == QStringLiteral("pinch")) {
        *mode = BrushMode::Pinch;
    } else if (normalized == QStringLiteral("smooth")) {
        *mode = BrushMode::Smooth;
    } else {
        return false;
    }
    return true;
}

QString brushTargetToString(BrushTarget target)
{
    return target == BrushTarget::Glyphs ? QStringLiteral("glyphs") : QStringLiteral("shape");
}

bool brushTargetFromString(const QString& value, BrushTarget* target)
{
    if (!target) {
        return false;
    }
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("glyphs")) {
        *target = BrushTarget::Glyphs;
    } else if (normalized == QStringLiteral("shape")) {
        *target = BrushTarget::Shape;
    } else {
        return false;
    }
    return true;
}

QVector<BrushSample> resampleBrushStroke(const QVector<QPointF>& positions,
                                         qreal spacing,
                                         qreal pressure,
                                         int maxSamples)
{
    QVector<QPointF> cleanPositions;
    cleanPositions.reserve(positions.size());
    for (const QPointF& position : positions) {
        if (!finitePoint(position)) {
            continue;
        }
        if (!cleanPositions.isEmpty() && cleanPositions.last() == position) {
            continue;
        }
        cleanPositions.push_back(position);
    }
    if (cleanPositions.isEmpty()) {
        return {};
    }

    const int boundedMaximum = qBound(1, maxSamples, MaximumSerializedSamples);
    if (cleanPositions.size() == 1 || boundedMaximum == 1) {
        return {{cleanPositions.first(), QPointF(), qBound<qreal>(0.0, pressure, 4.0)}};
    }

    qreal totalLength = 0.0;
    for (int index = 1; index < cleanPositions.size(); ++index) {
        totalLength += QLineF(cleanPositions[index - 1], cleanPositions[index]).length();
    }

    qreal effectiveSpacing = qMax<qreal>(0.01, spacing);
    if (totalLength > 0.0) {
        effectiveSpacing = qMax(effectiveSpacing, totalLength / (boundedMaximum - 1));
    }

    const qreal boundedPressure = qBound<qreal>(0.0, pressure, 4.0);
    QVector<BrushSample> samples;
    samples.reserve(qMin(boundedMaximum, static_cast<int>(totalLength / effectiveSpacing) + 2));
    samples.push_back({cleanPositions.first(), QPointF(), boundedPressure});

    QPointF segmentStart = cleanPositions.first();
    QPointF lastOutput = segmentStart;
    qreal distanceSinceOutput = 0.0;

    for (int index = 1; index < cleanPositions.size(); ++index) {
        QPointF segmentEnd = cleanPositions[index];
        qreal segmentLength = QLineF(segmentStart, segmentEnd).length();
        if (segmentLength <= 0.0) {
            segmentStart = segmentEnd;
            continue;
        }

        while (samples.size() < boundedMaximum
               && distanceSinceOutput + segmentLength >= effectiveSpacing) {
            const qreal distanceToSample = effectiveSpacing - distanceSinceOutput;
            const qreal t = qBound<qreal>(0.0, distanceToSample / segmentLength, 1.0);
            const QPointF samplePosition = segmentStart + (segmentEnd - segmentStart) * t;
            samples.push_back({samplePosition, samplePosition - lastOutput, boundedPressure});
            lastOutput = samplePosition;
            segmentStart = samplePosition;
            segmentLength = QLineF(segmentStart, segmentEnd).length();
            distanceSinceOutput = 0.0;
            if (segmentLength <= 0.0) {
                break;
            }
        }

        distanceSinceOutput += segmentLength;
        segmentStart = segmentEnd;
    }

    if (samples.size() < boundedMaximum && lastOutput != cleanPositions.last()) {
        samples.push_back({cleanPositions.last(), cleanPositions.last() - lastOutput, boundedPressure});
    } else if (samples.size() == boundedMaximum && samples.last().position != cleanPositions.last()) {
        const QPointF previousPosition = samples.size() > 1
            ? samples.at(samples.size() - 2).position
            : cleanPositions.last();
        samples.last().delta = cleanPositions.last() - previousPosition;
        samples.last().position = cleanPositions.last();
    }
    return samples;
}

void ManualDeformation::apply(VectorGeometry& geometry) const
{
    DeformationEvaluator::apply(*this, geometry);
}

QJsonObject ManualDeformation::toJson() const
{
    QJsonArray serializedStrokes;
    for (const DeformationStroke& stroke : strokes) {
        QJsonObject serializedStroke;
        serializedStroke.insert(QStringLiteral("mode"), brushModeToString(stroke.mode));
        serializedStroke.insert(QStringLiteral("target"), brushTargetToString(stroke.target));
        serializedStroke.insert(QStringLiteral("radius"), stroke.radius);
        serializedStroke.insert(QStringLiteral("strength"), stroke.strength);
        serializedStroke.insert(QStringLiteral("hardness"), stroke.hardness);
        serializedStroke.insert(QStringLiteral("coordinateSpace"),
                                stroke.coordinateSpace == DeformationCoordinateSpace::ObjectLocal
                                    ? QStringLiteral("objectLocal")
                                    : QStringLiteral("legacyPageAmbiguous"));

        QJsonArray serializedSamples;
        for (const BrushSample& sample : stroke.samples) {
            QJsonObject serializedSample;
            serializedSample.insert(QStringLiteral("position"), pointToJson(sample.position));
            serializedSample.insert(QStringLiteral("delta"), pointToJson(sample.delta));
            serializedSample.insert(QStringLiteral("pressure"), sample.pressure);
            serializedSamples.append(serializedSample);
        }
        serializedStroke.insert(QStringLiteral("samples"), serializedSamples);
        serializedStrokes.append(serializedStroke);
    }

    QJsonObject result;
    result.insert(QStringLiteral("enabled"), enabled);
    result.insert(QStringLiteral("strength"), strength);
    result.insert(QStringLiteral("strokes"), serializedStrokes);
    return result;
}

bool ManualDeformation::fromJson(const QJsonObject& object,
                                 ManualDeformation* deformation,
                                 QString* error)
{
    if (!deformation) {
        if (error) {
            *error = QStringLiteral("Deformation output is null.");
        }
        return false;
    }

    ManualDeformation result;
    result.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    result.strength = object.value(QStringLiteral("strength")).toDouble(1.0);
    if (!finiteValue(result.strength) || result.strength < 0.0 || result.strength > 4.0) {
        if (error) {
            *error = QStringLiteral("Deformation strength is outside the supported range.");
        }
        return false;
    }

    const QJsonValue strokesValue = object.value(QStringLiteral("strokes"));
    if (strokesValue.isUndefined()) {
        *deformation = result;
        return true;
    }
    if (!strokesValue.isArray()) {
        if (error) {
            *error = QStringLiteral("Deformation strokes must be an array.");
        }
        return false;
    }

    const QJsonArray serializedStrokes = strokesValue.toArray();
    if (serializedStrokes.size() > 4096) {
        if (error) {
            *error = QStringLiteral("Deformation contains too many strokes.");
        }
        return false;
    }
    for (int strokeIndex = 0; strokeIndex < serializedStrokes.size(); ++strokeIndex) {
        const QJsonValue strokeValue = serializedStrokes.at(strokeIndex);
        if (!strokeValue.isObject()) {
            if (error) {
                *error = QStringLiteral("Deformation stroke %1 is not an object.").arg(strokeIndex);
            }
            return false;
        }

        const QJsonObject serializedStroke = strokeValue.toObject();
        DeformationStroke stroke;
        if (!brushModeFromString(serializedStroke.value(QStringLiteral("mode")).toString(), &stroke.mode)
            || !brushTargetFromString(serializedStroke.value(QStringLiteral("target")).toString(), &stroke.target)) {
            if (error) {
                *error = QStringLiteral("Deformation stroke %1 has an unknown mode or target.").arg(strokeIndex);
            }
            return false;
        }
        if (stroke.mode == BrushMode::Smooth) {
            // Smooth has always been shape-only. Normalize older or external
            // data at the serialization boundary as well as at evaluation.
            stroke.target = BrushTarget::Shape;
        }
        stroke.radius = serializedStroke.value(QStringLiteral("radius")).toDouble(stroke.radius);
        stroke.strength = serializedStroke.value(QStringLiteral("strength")).toDouble(stroke.strength);
        stroke.hardness = serializedStroke.value(QStringLiteral("hardness")).toDouble(stroke.hardness);
        const QString coordinateSpace = serializedStroke.value(QStringLiteral("coordinateSpace")).toString();
        stroke.coordinateSpace = coordinateSpace == QStringLiteral("objectLocal")
            ? DeformationCoordinateSpace::ObjectLocal
            : DeformationCoordinateSpace::LegacyPageAmbiguous;
        if (!finiteValue(stroke.radius) || stroke.radius < 0.01 || stroke.radius > 100000.0
            || !finiteValue(stroke.strength) || stroke.strength < 0.0 || stroke.strength > 4.0
            || !finiteValue(stroke.hardness) || stroke.hardness < 0.0 || stroke.hardness > 1.0) {
            if (error) {
                *error = QStringLiteral("Deformation stroke %1 has invalid settings.").arg(strokeIndex);
            }
            return false;
        }

        const QJsonValue samplesValue = serializedStroke.value(QStringLiteral("samples"));
        if (!samplesValue.isArray()) {
            if (error) {
                *error = QStringLiteral("Deformation stroke %1 is missing samples.").arg(strokeIndex);
            }
            return false;
        }
        const QJsonArray serializedSamples = samplesValue.toArray();
        if (serializedSamples.isEmpty() || serializedSamples.size() > MaximumSerializedSamples) {
            if (error) {
                *error = QStringLiteral("Deformation stroke %1 has an invalid sample count.").arg(strokeIndex);
            }
            return false;
        }
        stroke.samples.reserve(serializedSamples.size());
        for (int sampleIndex = 0; sampleIndex < serializedSamples.size(); ++sampleIndex) {
            const QJsonValue sampleValue = serializedSamples.at(sampleIndex);
            if (!sampleValue.isObject()) {
                if (error) {
                    *error = QStringLiteral("Deformation sample %1 in stroke %2 is not an object.")
                                 .arg(sampleIndex)
                                 .arg(strokeIndex);
                }
                return false;
            }
            const QJsonObject serializedSample = sampleValue.toObject();
            BrushSample sample;
            if (!pointFromJson(serializedSample.value(QStringLiteral("position")), &sample.position)
                || !pointFromJson(serializedSample.value(QStringLiteral("delta")), &sample.delta)) {
                if (error) {
                    *error = QStringLiteral("Deformation sample %1 in stroke %2 has invalid coordinates.")
                                 .arg(sampleIndex)
                                 .arg(strokeIndex);
                }
                return false;
            }
            sample.pressure = serializedSample.value(QStringLiteral("pressure")).toDouble(1.0);
            if (!finiteValue(sample.pressure) || sample.pressure < 0.0 || sample.pressure > 4.0) {
                if (error) {
                    *error = QStringLiteral("Deformation sample %1 in stroke %2 has invalid pressure.")
                                 .arg(sampleIndex)
                                 .arg(strokeIndex);
                }
                return false;
            }
            stroke.samples.push_back(sample);
        }
        result.strokes.push_back(std::move(stroke));
    }

    *deformation = std::move(result);
    return true;
}

} // namespace vt
