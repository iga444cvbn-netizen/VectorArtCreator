#pragma once

#include "core/effects/effect_stack.h"
#include "core/deformation/manual_deformation.h"
#include "core/text/font_descriptor.h"

#include <QColor>
#include <QDateTime>
#include <QJsonObject>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <memory>
#include <vector>

namespace vt {

[[nodiscard]] QString createStableId(const QString& prefix);

struct ObjectTransform {
    QPointF position;
    qreal rotation = 0.0;
    QPointF scale = QPointF(1.0, 1.0);

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static ObjectTransform fromJson(const QJsonObject& object);
};

struct TextObject {
    QString id;
    QString sourceText;
    FontDescriptor font;
    TypographyProperties typography;
    QColor fill = QColor(24, 24, 28);
    EffectStack effects;
    ManualDeformation deformation;
    ObjectTransform transform;
    bool visible = true;

    // Reserved for forward-compatible object data such as masks or brush strokes.
    QJsonObject futureData;

    TextObject();
    TextObject(const TextObject& other);
    TextObject& operator=(const TextObject& other);
    TextObject(TextObject&&) noexcept = default;
    TextObject& operator=(TextObject&&) noexcept = default;
};

struct Layer {
    QString id;
    QString name = QStringLiteral("Layer 1");
    bool visible = true;
    bool locked = false;
    std::vector<std::unique_ptr<TextObject>> objects;

    Layer();
    Layer(const Layer& other);
    Layer& operator=(const Layer& other);
    Layer(Layer&&) noexcept = default;
    Layer& operator=(Layer&&) noexcept = default;

    [[nodiscard]] TextObject* objectById(const QString& objectId);
    [[nodiscard]] const TextObject* objectById(const QString& objectId) const;
};

struct Page {
    QString id;
    QString name = QStringLiteral("Page 1");
    QSizeF size = QSizeF(1200.0, 800.0);
    QColor background = QColor(242, 242, 246);
    std::vector<std::unique_ptr<Layer>> layers;

    Page();
    Page(const Page& other);
    Page& operator=(const Page& other);
    Page(Page&&) noexcept = default;
    Page& operator=(Page&&) noexcept = default;

    [[nodiscard]] Layer* layerById(const QString& layerId);
    [[nodiscard]] const Layer* layerById(const QString& layerId) const;
};

class Document {
public:
    static constexpr int CurrentFormatVersion = 5;

    int formatVersion = CurrentFormatVersion;
    QString title = QStringLiteral("Untitled Vector Typography Project");
    QDateTime createdAt;
    QDateTime modifiedAt;
    std::vector<std::unique_ptr<Page>> pages;
    QString currentPageId;
    QString activeLayerId;
    // Selection is normally UI state, but retaining the active editing
    // object here keeps the existing document commands reusable for any
    // selected text object without copying the document.
    QString activeObjectId;

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
    [[nodiscard]] Page* currentPage();
    [[nodiscard]] const Page* currentPage() const;
    [[nodiscard]] Page* pageById(const QString& pageId);
    [[nodiscard]] const Page* pageById(const QString& pageId) const;
    [[nodiscard]] Layer* activeLayer();
    [[nodiscard]] const Layer* activeLayer() const;
    [[nodiscard]] Layer* layerById(const QString& layerId);
    [[nodiscard]] const Layer* layerById(const QString& layerId) const;
    [[nodiscard]] TextObject* objectById(const QString& objectId);
    [[nodiscard]] const TextObject* objectById(const QString& objectId) const;
    [[nodiscard]] QVector<TextObject*> objectsOnCurrentPage();
    [[nodiscard]] QVector<const TextObject*> objectsOnCurrentPage() const;
    [[nodiscard]] bool hasObjects() const;
    void ensureDefaultScene(bool withTextObject = false);
    void touchModified();
};

} // namespace vt
