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
    // Additional letter spacing expressed as an em-relative value. For example,
    // 0.05 means five percent of the current font size.
    qreal trackingEm = 0.0;
    // Multiplier applied to Qt's natural line height for explicit newline
    // paragraphs. 1.0 preserves the font's normal leading.
    qreal lineSpacing = 1.0;

    [[nodiscard]] QJsonObject toJson(const QColor& fill) const;
    [[nodiscard]] static TypographyProperties fromJson(const QJsonObject& object,
                                                        QColor* fill,
                                                        int formatVersion = 2);
};

} // namespace vt
