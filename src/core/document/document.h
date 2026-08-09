#pragma once

#include "core/effects/effect_stack.h"
#include "core/deformation/manual_deformation.h"
#include "core/text/font_descriptor.h"

#include <QColor>
#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <memory>
#include <vector>

namespace vt {

struct TextObject {
    QString id = QStringLiteral("primary-text");
    QString sourceText = QStringLiteral("Vector typography");
    FontDescriptor font;
    TypographyProperties typography;
    QColor fill = QColor(24, 24, 28);
    EffectStack effects;
    ManualDeformation deformation;

    // Reserved for forward-compatible object data such as masks or brush strokes.
    QJsonObject futureData;

    TextObject();
    TextObject(const TextObject& other);
    TextObject& operator=(const TextObject& other);
    TextObject(TextObject&&) noexcept = default;
    TextObject& operator=(TextObject&&) noexcept = default;
};

class Document {
public:
    static constexpr int CurrentFormatVersion = 3;

    int formatVersion = CurrentFormatVersion;
    QString title = QStringLiteral("Untitled Vector Typography Project");
    QDateTime createdAt;
    QDateTime modifiedAt;
    std::vector<std::unique_ptr<TextObject>> objects;

    // Reserved for future project resources and metadata.
    QJsonObject metadata;
    QJsonObject resources;

    Document();
    Document(const Document& other);
    Document& operator=(const Document& other);
    Document(Document&&) noexcept = default;
    Document& operator=(Document&&) noexcept = default;

    [[nodiscard]] TextObject& primaryTextObject();
    [[nodiscard]] const TextObject& primaryTextObject() const;
    void touchModified();
};

} // namespace vt
