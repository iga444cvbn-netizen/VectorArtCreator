#include "tests/support/state_fingerprint.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace vt::test {
namespace {

QJsonObject semanticJson(const Document& document)
{
    // Do not call ProjectSerializer here. A serializer omission would otherwise
    // make before/after fingerprints agree while losing persistent document data.
    QJsonArray pages;
    for (const auto& page : document.pages) {
        if (!page) {
            pages.append(QJsonValue::Null);
            continue;
        }
        QJsonArray layers;
        for (const auto& layer : page->layers) {
            if (!layer) {
                layers.append(QJsonValue::Null);
                continue;
            }
            QJsonArray objects;
            for (const auto& object : layer->objects) {
                if (!object) {
                    objects.append(QJsonValue::Null);
                    continue;
                }
                QJsonArray effects;
                for (int index = 0; index < object->effects.size(); ++index) {
                    const Effect* effect = object->effects.at(index);
                    if (!effect) {
                        effects.append(QJsonValue::Null);
                        continue;
                    }
                    QJsonArray masks;
                    for (const EffectMaskStroke& mask : effect->maskStrokes) {
                        QJsonArray points;
                        for (const QPointF& point : mask.points) {
                            points.append(QJsonArray{point.x(), point.y()});
                        }
                        masks.append(QJsonObject{{QStringLiteral("points"), points},
                                                  {QStringLiteral("radius"), mask.radius},
                                                  {QStringLiteral("opacity"), mask.opacity},
                                                  {QStringLiteral("hardness"), mask.hardness},
                                                  {QStringLiteral("restore"), mask.restore}});
                    }
                    effects.append(QJsonObject{{QStringLiteral("typeId"), effect->typeId()},
                                               {QStringLiteral("instanceId"), effect->instanceId},
                                               {QStringLiteral("enabled"), effect->enabled},
                                               {QStringLiteral("masterStrength"), effect->masterStrength},
                                               {QStringLiteral("scopeKind"), static_cast<int>(effect->scope.kind)},
                                               {QStringLiteral("scopeStart"), effect->scope.start},
                                               {QStringLiteral("scopeEnd"), effect->scope.end},
                                               {QStringLiteral("mask"), masks},
                                               {QStringLiteral("maskInverted"), effect->maskInverted},
                                               {QStringLiteral("parameters"), effect->parametersToJson()}});
                }
                objects.append(QJsonObject{{QStringLiteral("id"), object->id},
                                           {QStringLiteral("sourceText"), object->sourceText},
                                           {QStringLiteral("font"), object->font.toJson()},
                                           {QStringLiteral("typography"), object->typography.toJson(object->fill)},
                                           {QStringLiteral("effects"), effects},
                                           {QStringLiteral("effectStackStrength"), object->effectStackStrength},
                                           {QStringLiteral("deformation"), object->deformation.toJson()},
                                           {QStringLiteral("transform"), object->transform.toJson()},
                                           {QStringLiteral("visible"), object->visible},
                                           {QStringLiteral("futureData"), object->futureData}});
            }
            layers.append(QJsonObject{{QStringLiteral("id"), layer->id},
                                      {QStringLiteral("name"), layer->name},
                                      {QStringLiteral("visible"), layer->visible},
                                      {QStringLiteral("locked"), layer->locked},
                                      {QStringLiteral("objects"), objects}});
        }
        pages.append(QJsonObject{{QStringLiteral("id"), page->id},
                                 {QStringLiteral("name"), page->name},
                                 {QStringLiteral("width"), page->size.width()},
                                 {QStringLiteral("height"), page->size.height()},
                                 {QStringLiteral("background"), page->background.name(QColor::HexArgb)},
                                 {QStringLiteral("layers"), layers}});
    }
    QJsonObject persistentMetadata = document.metadata;
    // ProjectSerializer materializes these derived/transient values into its
    // metadata object on load. Model them once here instead of treating that
    // round-trip representation detail as document semantics.
    persistentMetadata.remove(QStringLiteral("title"));
    persistentMetadata.remove(QStringLiteral("createdAt"));
    persistentMetadata.remove(QStringLiteral("modifiedAt"));
    return QJsonObject{{QStringLiteral("formatVersion"), document.formatVersion},
                       {QStringLiteral("title"), document.title},
                       {QStringLiteral("pages"), pages},
                       {QStringLiteral("currentPageId"), document.currentPageId},
                       {QStringLiteral("activeLayerId"), document.activeLayerId},
                       {QStringLiteral("metadata"), persistentMetadata},
                       {QStringLiteral("resources"), document.resources}};
}

QString compactValue(const QJsonValue& value)
{
    QByteArray encoded;
    if (value.isObject()) {
        encoded = QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    } else if (value.isArray()) {
        encoded = QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    } else if (value.isString()) {
        encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
        if (encoded.size() >= 2) encoded = encoded.mid(1, encoded.size() - 2);
    } else if (value.isDouble()) {
        encoded = QByteArray::number(value.toDouble(), 'g', 17);
    } else if (value.isBool()) {
        encoded = value.toBool() ? QByteArrayLiteral("true") : QByteArrayLiteral("false");
    } else {
        encoded = value.isNull() ? QByteArrayLiteral("null") : QByteArrayLiteral("undefined");
    }
    constexpr int maximumDiagnosticLength = 240;
    if (encoded.size() > maximumDiagnosticLength) {
        encoded = encoded.left(maximumDiagnosticLength - 3) + QByteArrayLiteral("...");
    }
    return QString::fromUtf8(encoded);
}

bool firstJsonDifference(const QJsonValue& expected, const QJsonValue& actual,
                         const QString& path, QString* difference)
{
    if (expected.type() != actual.type()) {
        if (difference) {
            *difference = QStringLiteral("%1 type/value: expected %2, actual %3")
                              .arg(path, compactValue(expected), compactValue(actual));
        }
        return true;
    }
    if (expected.isObject()) {
        const QJsonObject left = expected.toObject();
        const QJsonObject right = actual.toObject();
        QStringList keys = left.keys();
        for (const QString& key : right.keys()) {
            if (!keys.contains(key)) keys.push_back(key);
        }
        keys.sort();
        for (const QString& key : keys) {
            const QString childPath = path + QLatin1Char('.') + key;
            if (!left.contains(key) || !right.contains(key)) {
                if (difference) {
                    *difference = QStringLiteral("%1 presence: expected %2, actual %3")
                                      .arg(childPath,
                                           left.contains(key) ? QStringLiteral("present") : QStringLiteral("missing"),
                                           right.contains(key) ? QStringLiteral("present") : QStringLiteral("missing"));
                }
                return true;
            }
            if (firstJsonDifference(left.value(key), right.value(key), childPath, difference)) return true;
        }
        return false;
    }
    if (expected.isArray()) {
        const QJsonArray left = expected.toArray();
        const QJsonArray right = actual.toArray();
        if (left.size() != right.size()) {
            if (difference) {
                *difference = QStringLiteral("%1 length: expected %2, actual %3")
                                  .arg(path).arg(left.size()).arg(right.size());
            }
            return true;
        }
        for (int index = 0; index < left.size(); ++index) {
            if (firstJsonDifference(left.at(index), right.at(index),
                                    path + QStringLiteral("[%1]").arg(index), difference)) return true;
        }
        return false;
    }
    if (expected != actual) {
        if (difference) {
            *difference = QStringLiteral("%1: expected %2, actual %3")
                              .arg(path, compactValue(expected), compactValue(actual));
        }
        return true;
    }
    return false;
}

} // namespace

QString semanticFingerprint(const Document& document)
{
    const QJsonObject json = semanticJson(document);
    // The hand-built representation preserves page/layer/effect ordering and
    // omits only declared transient state (timestamps and active object).
    return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

bool semanticallyEqual(const Document& left, const Document& right, QString* difference)
{
    const QJsonObject leftValue = semanticJson(left);
    const QJsonObject rightValue = semanticJson(right);
    if (leftValue == rightValue) return true;
    if (difference) {
        QString firstDifference;
        static_cast<void>(firstJsonDifference(leftValue, rightValue,
                                              QStringLiteral("$"), &firstDifference));
        *difference = QStringLiteral("semantic fingerprints differ; first difference: %1")
                          .arg(firstDifference);
    }
    return false;
}

} // namespace vt::test
