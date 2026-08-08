#include "core/document/document.h"

#include <QFont>

#include <stdexcept>
#include <utility>

namespace vt {

TextObject::TextObject()
{
    const QFont defaultFont;
    font.family = defaultFont.family();
    font.styleName = defaultFont.styleName();
    font.weight = defaultFont.weight();
}

TextObject::TextObject(const TextObject& other)
    : id(other.id)
    , sourceText(other.sourceText)
    , font(other.font)
    , typography(other.typography)
    , fill(other.fill)
    , effects(other.effects)
    , futureData(other.futureData)
{
}

TextObject& TextObject::operator=(const TextObject& other)
{
    if (this == &other) {
        return *this;
    }
    id = other.id;
    sourceText = other.sourceText;
    font = other.font;
    typography = other.typography;
    fill = other.fill;
    effects = other.effects;
    futureData = other.futureData;
    return *this;
}

Document::Document()
    : createdAt(QDateTime::currentDateTimeUtc())
    , modifiedAt(createdAt)
{
    objects.push_back(std::make_unique<TextObject>());
}

Document::Document(const Document& other)
    : formatVersion(other.formatVersion)
    , title(other.title)
    , createdAt(other.createdAt)
    , modifiedAt(other.modifiedAt)
    , metadata(other.metadata)
    , resources(other.resources)
{
    objects.reserve(other.objects.size());
    for (const auto& object : other.objects) {
        if (object) {
            objects.push_back(std::make_unique<TextObject>(*object));
        }
    }
    if (objects.empty()) {
        objects.push_back(std::make_unique<TextObject>());
    }
}

Document& Document::operator=(const Document& other)
{
    if (this == &other) {
        return *this;
    }

    Document copy(other);
    *this = std::move(copy);
    return *this;
}

TextObject& Document::primaryTextObject()
{
    if (objects.empty()) {
        objects.push_back(std::make_unique<TextObject>());
    }
    return *objects.front();
}

const TextObject& Document::primaryTextObject() const
{
    if (objects.empty() || !objects.front()) {
        throw std::runtime_error("Document has no primary text object");
    }
    return *objects.front();
}

void Document::touchModified()
{
    modifiedAt = QDateTime::currentDateTimeUtc();
}

} // namespace vt
