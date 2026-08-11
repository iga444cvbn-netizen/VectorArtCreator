#include "tests/support/state_fingerprint.h"

#include "core/serialization/project_serializer.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace vt::test {
namespace {

QJsonObject semanticJson(const Document& document)
{
    QJsonObject json = ProjectSerializer::toJson(document).object();
    json.remove(QStringLiteral("activeObjectId"));
    QJsonObject metadata = json.value(QStringLiteral("metadata")).toObject();
    metadata.remove(QStringLiteral("createdAt"));
    metadata.remove(QStringLiteral("modifiedAt"));
    json.insert(QStringLiteral("metadata"), metadata);
    return json;
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
    // Serializer writes page/layer/effect ordering in semantic order, so a
    // compact JSON document is a stable comparison without byte-comparing a
    // persisted project file or transient metadata.
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
