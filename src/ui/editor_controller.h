#pragma once

#include "core/document/document.h"
#include "core/export/svg_exporter.h"
#include "core/presets/preset_manager.h"
#include "core/text/text_engine.h"

#include <QColor>
#include <QObject>
#include <QStringList>
#include <QUndoStack>

#include <functional>
#include <memory>
#include <optional>

namespace vt {

class EditorController final : public QObject {
    Q_OBJECT

public:
    explicit EditorController(QObject* parent = nullptr);

    [[nodiscard]] Document& document();
    [[nodiscard]] const Document& document() const;
    [[nodiscard]] const VectorGeometry& geometry() const;
    [[nodiscard]] QUndoStack* undoStack();
    [[nodiscard]] bool isModified() const;
    [[nodiscard]] QStringList fontFamilies() const;
    [[nodiscard]] QStringList fontStyles(const QString& family) const;
    [[nodiscard]] QStringList presetNames(QString* error = nullptr) const;
    [[nodiscard]] QString presetDirectory() const;

    void refreshFonts();
    void newDocument();
    void setText(const QString& text);
    void setFontFamily(const QString& family);
    void setFontStyle(const QString& styleName);
    void setFontWeight(int weight);
    void setFontSize(qreal pointSize);
    void setTracking(qreal tracking);
    void setFillColor(const QColor& color);

    void addEffect(const QString& typeId);
    void removeEffect(int index);
    void moveEffect(int from, int to);
    void setEffectEnabled(int index, bool enabled);
    void setEffectParameter(int index, const QString& parameterId, double value);

    [[nodiscard]] bool savePreset(const QString& name, QString* error = nullptr);
    [[nodiscard]] bool applyPreset(const QString& name, QString* error = nullptr);
    [[nodiscard]] bool deletePreset(const QString& name, QString* error = nullptr);

    [[nodiscard]] bool saveProject(const QString& filePath, QString* error = nullptr);
    [[nodiscard]] bool openProject(const QString& filePath, QString* error = nullptr);
    [[nodiscard]] bool exportSvg(const QString& filePath, QString* error = nullptr) const;

signals:
    void documentChanged();
    void sceneChanged();
    void statusMessageChanged(const QString& message);
    void fontsChanged(const QStringList& families);

private:
    void applySnapshot(const Document& snapshot);
    void rebuildScene();
    [[nodiscard]] QString geometryCacheKey(const TextObject& object) const;
    void pushMutation(const QString& description,
                      const std::function<void(Document&)>& mutation,
                      int mergeId = 0);
    void publishError(const QString& message);

    Document m_document;
    TextEngine m_textEngine;
    std::optional<VectorGeometry> m_baseGeometry;
    QString m_baseGeometryKey;
    VectorGeometry m_geometry;
    PresetManager m_presetManager;
    SvgExporter m_svgExporter;
    QUndoStack m_undoStack;
    bool m_modified = false;
};

} // namespace vt
