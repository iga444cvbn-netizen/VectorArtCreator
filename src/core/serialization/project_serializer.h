#pragma once

#include "core/document/document.h"

#include <QJsonDocument>
#include <QString>

namespace vt {

// Conservative schema-entry limits. Runtime geometry stages additionally share
// WorkControl; these counts prevent a valid-looking hierarchy from allocating an
// aggregate workload before evaluation gets a chance to cooperate.
struct ProjectResourceLimits {
    qint64 maximumProjectInputBytes = 64LL * 1024LL * 1024LL;
    qint64 maximumClipboardInputBytes = 16LL * 1024LL * 1024LL;
    int maximumPages = 128;
    int maximumLayersPerPage = 128;
    qint64 maximumLayers = 1024;
    int maximumObjectsPerLayer = 1024;
    qint64 maximumObjects = 4096;
    int maximumEffectsPerObject = 128;
    qint64 maximumEffects = 16384;
    int maximumSourceUtf16PerObject = 65'536;
    qint64 maximumSourceUtf16 = 1'048'576;
    int maximumMaskStrokesPerEffect = 1024;
    qint64 maximumMaskStrokes = 32'768;
    int maximumMaskPointsPerStroke = 4096;
    qint64 maximumMaskPoints = 1'048'576;
    int maximumDeformationStrokesPerObject = 4096;
    qint64 maximumDeformationStrokes = 32'768;
    int maximumDeformationSamplesPerStroke = 4096;
    qint64 maximumDeformationSamples = 1'048'576;
    qint64 maximumEstimatedWork = 8'000'000;
};

class ProjectSerializer {
public:
    [[nodiscard]] static ProjectResourceLimits resourceLimits();
    [[nodiscard]] static bool validateProjectInputSize(qint64 bytes, QString* error = nullptr);
    [[nodiscard]] static bool validateClipboardInputSize(qint64 bytes, QString* error = nullptr);
    // Saturating object-work estimate shared by schema admission and boundary
    // tests. Inputs are semantic counts, so negative values are treated as 0.
    [[nodiscard]] static qint64 saturatedEstimatedObjectWork(
        qint64 sourceUnits, qint64 nestedUnits);
    [[nodiscard]] static QJsonDocument toJson(const Document& document);
    [[nodiscard]] static QJsonObject textObjectToJson(const TextObject& object);
    [[nodiscard]] static bool textObjectFromJson(const QJsonObject& json,
                                                 TextObject* object,
                                                 QString* error = nullptr);
    [[nodiscard]] static bool fromJson(const QJsonDocument& json, Document* document, QString* error);
    [[nodiscard]] static bool validateResourceBudget(
        const QJsonDocument& json,
        const ProjectResourceLimits& limits,
        QString* error = nullptr);
    [[nodiscard]] static bool saveToFile(const Document& document, const QString& filePath, QString* error);
    [[nodiscard]] static bool loadFromFile(const QString& filePath, Document* document, QString* error);
};

} // namespace vt
