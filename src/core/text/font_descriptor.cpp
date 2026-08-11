#include "core/text/font_descriptor.h"

#include <QJsonValue>

namespace vt {

QFont FontDescriptor::toQFont(qreal pointSize) const
{
    QFont font = family.isEmpty() ? QFont() : QFont(family);
    font.setPointSizeF(pointSize);
    // An exact style and synthetic weight/italic flags are competing Qt font
    // requests.  Exact styles are authoritative; B/I clear styleName before
    // reaching this method and therefore use the traits path below.
    if (!styleName.isEmpty()) {
        font.setStyleName(styleName);
    } else {
        const int boundedWeight = qBound(0, weight, 1000);
        font.setWeight(static_cast<QFont::Weight>(boundedWeight));
        font.setItalic(italic);
    }
    font.setUnderline(underline);
    font.setStrikeOut(strikeOut);
    return font;
}

QJsonObject FontDescriptor::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("family"), family);
    object.insert(QStringLiteral("styleName"), styleName);
    object.insert(QStringLiteral("weight"), weight);
    object.insert(QStringLiteral("italic"), italic);
    object.insert(QStringLiteral("underline"), underline);
    object.insert(QStringLiteral("strikeOut"), strikeOut);
    object.insert(QStringLiteral("fingerprint"), fingerprint);
    object.insert(QStringLiteral("embeddedResourceId"), embeddedResourceId);
    object.insert(QStringLiteral("embeddingPermission"), embeddingPermission);
    return object;
}

FontDescriptor FontDescriptor::fromJson(const QJsonObject& object)
{
    FontDescriptor descriptor;
    descriptor.family = object.value(QStringLiteral("family")).toString();
    descriptor.styleName = object.value(QStringLiteral("styleName")).toString();
    descriptor.weight = object.value(QStringLiteral("weight"))
                            .toInt(static_cast<int>(QFont::Normal));
    descriptor.italic = object.value(QStringLiteral("italic")).toBool(false);
    descriptor.underline = object.value(QStringLiteral("underline")).toBool(false);
    descriptor.strikeOut = object.value(QStringLiteral("strikeOut")).toBool(false);
    descriptor.fingerprint = object.value(QStringLiteral("fingerprint")).toString();
    descriptor.embeddedResourceId = object.value(QStringLiteral("embeddedResourceId")).toString();
    descriptor.embeddingPermission = object.value(QStringLiteral("embeddingPermission")).toString();
    return descriptor;
}

bool FontDescriptor::operator==(const FontDescriptor& other) const
{
    return family == other.family
        && styleName == other.styleName
        && weight == other.weight
        && italic == other.italic
        && underline == other.underline
        && strikeOut == other.strikeOut
        && fingerprint == other.fingerprint
        && embeddedResourceId == other.embeddedResourceId
        && embeddingPermission == other.embeddingPermission;
}

QJsonObject TypographyProperties::toJson(const QColor& fill) const
{
    QJsonObject object;
    object.insert(QStringLiteral("fontSize"), fontSize);
    object.insert(QStringLiteral("trackingEm"), trackingEm);
    object.insert(QStringLiteral("lineSpacing"), lineSpacing);
    object.insert(QStringLiteral("trackingUnit"), QStringLiteral("em"));
    object.insert(QStringLiteral("fill"), fill.name(QColor::HexArgb));
    return object;
}

TypographyProperties TypographyProperties::fromJson(const QJsonObject& object,
                                                     QColor* fill,
                                                     int formatVersion)
{
    TypographyProperties properties;
    properties.fontSize = object.value(QStringLiteral("fontSize")).toDouble(properties.fontSize);
    if (object.contains(QStringLiteral("trackingEm"))) {
        properties.trackingEm = object.value(QStringLiteral("trackingEm")).toDouble(properties.trackingEm);
    } else if (object.contains(QStringLiteral("tracking"))) {
        // Version 1 stored absolute point spacing. Convert it once at load time
        // so the in-memory representation is portable and font-size-relative.
        const qreal absoluteSpacing = object.value(QStringLiteral("tracking")).toDouble();
        if (formatVersion <= 1 && !qFuzzyIsNull(properties.fontSize)) {
            properties.trackingEm = absoluteSpacing / properties.fontSize;
        }
    }
    properties.lineSpacing = qBound<qreal>(0.1,
                                           object.value(QStringLiteral("lineSpacing"))
                                               .toDouble(properties.lineSpacing),
                                           8.0);

    if (fill) {
        const QColor parsed(object.value(QStringLiteral("fill")).toString());
        if (parsed.isValid()) {
            *fill = parsed;
        }
    }
    return properties;
}

} // namespace vt
