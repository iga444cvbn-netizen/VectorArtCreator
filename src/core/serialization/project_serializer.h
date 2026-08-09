#pragma once

#include "core/document/document.h"

#include <QJsonDocument>
#include <QString>

namespace vt {

class ProjectSerializer {
public:
    [[nodiscard]] static QJsonDocument toJson(const Document& document);
    [[nodiscard]] static QJsonObject textObjectToJson(const TextObject& object);
    [[nodiscard]] static bool textObjectFromJson(const QJsonObject& json,
                                                 TextObject* object,
                                                 QString* error = nullptr);
    [[nodiscard]] static bool fromJson(const QJsonDocument& json, Document* document, QString* error);
    [[nodiscard]] static bool saveToFile(const Document& document, const QString& filePath, QString* error);
    [[nodiscard]] static bool loadFromFile(const QString& filePath, Document* document, QString* error);
};

} // namespace vt
