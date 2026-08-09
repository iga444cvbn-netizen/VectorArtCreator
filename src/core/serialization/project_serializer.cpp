#include "core/serialization/project_serializer.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonParseError>
#include <QSaveFile>

#include <utility>

namespace vt {

namespace {

QJsonObject textObjectToJson(const TextObject& textObject)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("text"));
    object.insert(QStringLiteral("id"), textObject.id);
    object.insert(QStringLiteral("sourceText"), textObject.sourceText);
    object.insert(QStringLiteral("font"), textObject.font.toJson());
    object.insert(QStringLiteral("typography"), textObject.typography.toJson(textObject.fill));
    object.insert(QStringLiteral("effects"), textObject.effects.toJson());
    object.insert(QStringLiteral("deformation"), textObject.deformation.toJson());
    object.insert(QStringLiteral("futureData"), textObject.futureData);
    return object;
}

bool textObjectFromJson(const QJsonObject& object,
                        TextObject* textObject,
                        int formatVersion,
                        QString* error)
{
    if (object.value(QStringLiteral("type")).toString() != QStringLiteral("text")) {
        if (error) {
            *error = QStringLiteral("Project contains an unsupported object type.");
        }
        return false;
    }

    TextObject result;
    result.id = object.value(QStringLiteral("id")).toString(result.id);
    result.sourceText = object.value(QStringLiteral("sourceText")).toString();
    result.font = FontDescriptor::fromJson(object.value(QStringLiteral("font")).toObject());
    result.typography = TypographyProperties::fromJson(
        object.value(QStringLiteral("typography")).toObject(), &result.fill, formatVersion);
    result.futureData = object.value(QStringLiteral("futureData")).toObject();

    const QJsonValue effectValue = object.value(QStringLiteral("effects"));
    if (!effectValue.isArray()) {
        if (error) {
            *error = QStringLiteral("Text object is missing its effect stack array.");
        }
        return false;
    }
    QString effectError;
    result.effects = EffectStack::fromJson(effectValue.toArray(), &effectError);
    if (!effectError.isEmpty()) {
        if (error) {
            *error = effectError;
        }
        return false;
    }

    const QJsonValue deformationValue = object.value(QStringLiteral("deformation"));
    if (!deformationValue.isUndefined()) {
        if (!deformationValue.isObject()) {
            if (error) {
                *error = QStringLiteral("Text object deformation data is not an object.");
            }
            return false;
        }
        QString deformationError;
        if (!ManualDeformation::fromJson(
                deformationValue.toObject(), &result.deformation, &deformationError)) {
            if (error) {
                *error = deformationError;
            }
            return false;
        }
    }

    *textObject = std::move(result);
    return true;
}

} // namespace

QJsonDocument ProjectSerializer::toJson(const Document& document)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("vectorTypographyProject"));
    root.insert(QStringLiteral("formatVersion"), Document::CurrentFormatVersion);

    QJsonObject metadata = document.metadata;
    metadata.insert(QStringLiteral("title"), document.title);
    metadata.insert(QStringLiteral("createdAt"), document.createdAt.toString(Qt::ISODateWithMs));
    metadata.insert(QStringLiteral("modifiedAt"), document.modifiedAt.toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("metadata"), metadata);
    root.insert(QStringLiteral("resources"), document.resources);

    QJsonArray objects;
    for (const auto& object : document.objects) {
        if (object) {
            objects.append(textObjectToJson(*object));
        }
    }
    root.insert(QStringLiteral("objects"), objects);
    return QJsonDocument(root);
}

bool ProjectSerializer::fromJson(const QJsonDocument& json, Document* document, QString* error)
{
    if (!document || !json.isObject()) {
        if (error) {
            *error = QStringLiteral("Project JSON must contain an object at its root.");
        }
        return false;
    }

    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("vectorTypographyProject")) {
        if (error) {
            *error = QStringLiteral("The file is not a Vector Typography project.");
        }
        return false;
    }

    const int version = root.value(QStringLiteral("formatVersion")).toInt(-1);
    if (version < 1 || version > Document::CurrentFormatVersion) {
        if (error) {
            *error = QStringLiteral("Unsupported project format version %1.").arg(version);
        }
        return false;
    }

    const QJsonValue objectsValue = root.value(QStringLiteral("objects"));
    if (!objectsValue.isArray() || objectsValue.toArray().isEmpty()) {
        if (error) {
            *error = QStringLiteral("Project does not contain a text object.");
        }
        return false;
    }

    Document result;
    // Loading an older project migrates it into the current in-memory schema;
    // the next save writes the current format version.
    result.formatVersion = Document::CurrentFormatVersion;
    result.objects.clear();
    const QJsonObject metadata = root.value(QStringLiteral("metadata")).toObject();
    result.metadata = metadata;
    result.title = metadata.value(QStringLiteral("title")).toString(result.title);
    result.createdAt = QDateTime::fromString(
        metadata.value(QStringLiteral("createdAt")).toString(), Qt::ISODateWithMs);
    result.modifiedAt = QDateTime::fromString(
        metadata.value(QStringLiteral("modifiedAt")).toString(), Qt::ISODateWithMs);
    if (!result.createdAt.isValid()) {
        result.createdAt = QDateTime::currentDateTimeUtc();
    }
    if (!result.modifiedAt.isValid()) {
        result.modifiedAt = result.createdAt;
    }
    result.resources = root.value(QStringLiteral("resources")).toObject();

    const QJsonArray objects = objectsValue.toArray();
    for (int index = 0; index < objects.size(); ++index) {
        if (!objects.at(index).isObject()) {
            if (error) {
                *error = QStringLiteral("Project object %1 is not an object.").arg(index);
            }
            return false;
        }
        auto object = std::make_unique<TextObject>();
        if (!textObjectFromJson(objects.at(index).toObject(), object.get(), version, error)) {
            return false;
        }
        result.objects.push_back(std::move(object));
    }

    if (result.objects.empty()) {
        if (error) {
            *error = QStringLiteral("Project does not contain a usable text object.");
        }
        return false;
    }

    *document = std::move(result);
    return true;
}

bool ProjectSerializer::saveToFile(const Document& document, const QString& filePath, QString* error)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot open project for writing: %1").arg(file.errorString());
        }
        return false;
    }

    const QByteArray data = toJson(document).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        if (error) {
            *error = QStringLiteral("Could not write the complete project file: %1").arg(file.errorString());
        }
        return false;
    }
    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("Could not commit project file: %1").arg(file.errorString());
        }
        return false;
    }
    return true;
}

bool ProjectSerializer::loadFromFile(const QString& filePath, Document* document, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot open project: %1").arg(file.errorString());
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("Project JSON is invalid: %1").arg(parseError.errorString());
        }
        return false;
    }
    return fromJson(json, document, error);
}

} // namespace vt
