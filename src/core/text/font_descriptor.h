#pragma once

#include <QFont>
#include <QColor>
#include <QJsonObject>
#include <QString>

namespace vt {

struct FontDescriptor {
    QString family;
    QString styleName;
    int weight = static_cast<int>(QFont::Normal);

    // Reserved for a future private embedded-font resource implementation.
    QString fingerprint;
    QString embeddedResourceId;
    QString embeddingPermission;

    [[nodiscard]] QFont toQFont(qreal pointSize) const;
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static FontDescriptor fromJson(const QJsonObject& object);

    [[nodiscard]] bool operator==(const FontDescriptor& other) const;
};

struct TypographyProperties {
    qreal fontSize = 72.0;
    qreal tracking = 0.0;

    [[nodiscard]] QJsonObject toJson(const QColor& fill) const;
    [[nodiscard]] static TypographyProperties fromJson(const QJsonObject& object, QColor* fill);
};

} // namespace vt
