#include "ui/editor_controller.h"

#include "core/effects/effect.h"
#include "core/serialization/project_serializer.h"

#include <QFontDatabase>
#include <QStandardPaths>

#include <utility>

namespace vt {

namespace {

constexpr int TextMergeId = 100;
constexpr int TypographyMergeBase = 200;
constexpr int EffectParameterMergeBase = 1000;

QString defaultPresetDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/presets");
}

class SnapshotCommand final : public QUndoCommand {
public:
    SnapshotCommand(std::function<void(const Document&)> apply,
                    Document before,
                    Document after,
                    QString description,
                    int mergeId)
        : QUndoCommand(std::move(description))
        , m_apply(std::move(apply))
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_mergeId(mergeId)
    {
    }

    void undo() override
    {
        m_apply(m_before);
    }

    void redo() override
    {
        m_apply(m_after);
    }

    [[nodiscard]] int id() const override
    {
        return m_mergeId;
    }

    bool mergeWith(const QUndoCommand* command) override
    {
        if (m_mergeId == 0) {
            return false;
        }
        const auto* other = dynamic_cast<const SnapshotCommand*>(command);
        if (!other || other->m_mergeId != m_mergeId) {
            return false;
        }
        m_after = other->m_after;
        return true;
    }

private:
    std::function<void(const Document&)> m_apply;
    Document m_before;
    Document m_after;
    int m_mergeId = 0;
};

} // namespace

EditorController::EditorController(QObject* parent)
    : QObject(parent)
    , m_presetManager(defaultPresetDirectory())
{
    rebuildScene();
}

Document& EditorController::document()
{
    return m_document;
}

const Document& EditorController::document() const
{
    return m_document;
}

const VectorGeometry& EditorController::geometry() const
{
    return m_geometry;
}

QUndoStack* EditorController::undoStack()
{
    return &m_undoStack;
}

bool EditorController::isModified() const
{
    return m_modified;
}

QStringList EditorController::fontFamilies() const
{
    return QFontDatabase::families();
}

QStringList EditorController::fontStyles(const QString& family) const
{
    if (family.trimmed().isEmpty()) {
        return {};
    }
    return QFontDatabase::styles(family);
}

QStringList EditorController::presetNames(QString* error) const
{
    return m_presetManager.listPresetNames(error);
}

QString EditorController::presetDirectory() const
{
    return m_presetManager.directoryPath();
}

void EditorController::refreshFonts()
{
    m_textEngine.clearCache();
    m_baseGeometry.reset();
    m_baseGeometryKey.clear();
    rebuildScene();
    emit fontsChanged(fontFamilies());
    emit statusMessageChanged(QStringLiteral("System font list refreshed."));
}

void EditorController::newDocument()
{
    m_document = Document();
    m_undoStack.clear();
    m_textEngine.clearCache();
    m_baseGeometry.reset();
    m_baseGeometryKey.clear();
    m_modified = false;
    rebuildScene();
    emit documentChanged();
    emit statusMessageChanged(QStringLiteral("New project created."));
}

void EditorController::setText(const QString& text)
{
    pushMutation(QStringLiteral("Change text"), [text](Document& document) {
        document.primaryTextObject().sourceText = text;
    }, TextMergeId);
}

void EditorController::setFontFamily(const QString& family)
{
    pushMutation(QStringLiteral("Change font family"), [family](Document& document) {
        document.primaryTextObject().font.family = family;
    }, TypographyMergeBase + 1);
}

void EditorController::setFontStyle(const QString& styleName)
{
    pushMutation(QStringLiteral("Change font style"), [styleName](Document& document) {
        document.primaryTextObject().font.styleName = styleName;
    }, TypographyMergeBase + 2);
}

void EditorController::setFontWeight(int weight)
{
    pushMutation(QStringLiteral("Change font weight"), [weight](Document& document) {
        document.primaryTextObject().font.weight = weight;
    }, TypographyMergeBase + 3);
}

void EditorController::setFontSize(qreal pointSize)
{
    pushMutation(QStringLiteral("Change font size"), [pointSize](Document& document) {
        document.primaryTextObject().typography.fontSize = qBound<qreal>(1.0, pointSize, 2000.0);
    }, TypographyMergeBase + 4);
}

void EditorController::setTracking(qreal tracking)
{
    pushMutation(QStringLiteral("Change tracking"), [tracking](Document& document) {
        document.primaryTextObject().typography.tracking = qBound<qreal>(-500.0, tracking, 500.0);
    }, TypographyMergeBase + 5);
}

void EditorController::setFillColor(const QColor& color)
{
    if (!color.isValid()) {
        return;
    }
    pushMutation(QStringLiteral("Change fill color"), [color](Document& document) {
        document.primaryTextObject().fill = color;
    });
}

void EditorController::addEffect(const QString& typeId)
{
    std::unique_ptr<Effect> effect = createEffect(typeId);
    if (!effect) {
        publishError(QStringLiteral("Cannot add unknown effect '%1'.").arg(typeId));
        return;
    }
    pushMutation(QStringLiteral("Add %1 effect").arg(effect->displayName()),
                 [typeId](Document& document) {
                     document.primaryTextObject().effects.append(createEffect(typeId));
                 });
}

void EditorController::removeEffect(int index)
{
    pushMutation(QStringLiteral("Remove effect"), [index](Document& document) {
        document.primaryTextObject().effects.removeAt(index);
    });
}

void EditorController::moveEffect(int from, int to)
{
    pushMutation(QStringLiteral("Reorder effects"), [from, to](Document& document) {
        document.primaryTextObject().effects.move(from, to);
    });
}

void EditorController::setEffectEnabled(int index, bool enabled)
{
    pushMutation(QStringLiteral("Toggle effect"), [index, enabled](Document& document) {
        if (Effect* effect = document.primaryTextObject().effects.at(index)) {
            effect->enabled = enabled;
        }
    });
}

void EditorController::setEffectParameter(int index, const QString& parameterId, double value)
{
    const int mergeId = EffectParameterMergeBase
        + index * 2048
        + (qHash(parameterId) & 0x7ff);
    pushMutation(QStringLiteral("Change effect parameter"), [index, parameterId, value](Document& document) {
        if (Effect* effect = document.primaryTextObject().effects.at(index)) {
            effect->setParameter(parameterId, value);
        }
    }, mergeId);
}

bool EditorController::savePreset(const QString& name, QString* error)
{
    Preset preset;
    preset.name = name.trimmed();
    preset.effects = m_document.primaryTextObject().effects;
    const bool saved = m_presetManager.savePreset(preset, error);
    if (saved) {
        emit statusMessageChanged(QStringLiteral("Preset '%1' saved.").arg(preset.name));
    }
    return saved;
}

bool EditorController::applyPreset(const QString& name, QString* error)
{
    Preset preset;
    if (!m_presetManager.loadPreset(name, &preset, error)) {
        return false;
    }

    pushMutation(QStringLiteral("Apply preset '%1'").arg(name), [preset](Document& document) {
        document.primaryTextObject().effects = preset.effects;
    });
    emit statusMessageChanged(QStringLiteral("Preset '%1' applied.").arg(name));
    return true;
}

bool EditorController::deletePreset(const QString& name, QString* error)
{
    const bool deleted = m_presetManager.deletePreset(name, error);
    if (deleted) {
        emit statusMessageChanged(QStringLiteral("Preset '%1' deleted.").arg(name));
    }
    return deleted;
}

bool EditorController::saveProject(const QString& filePath, QString* error)
{
    if (!ProjectSerializer::saveToFile(m_document, filePath, error)) {
        return false;
    }
    m_modified = false;
    emit statusMessageChanged(QStringLiteral("Project saved: %1").arg(filePath));
    return true;
}

bool EditorController::openProject(const QString& filePath, QString* error)
{
    Document loaded;
    if (!ProjectSerializer::loadFromFile(filePath, &loaded, error)) {
        return false;
    }
    m_document = std::move(loaded);
    m_undoStack.clear();
    m_textEngine.clearCache();
    m_baseGeometry.reset();
    m_baseGeometryKey.clear();
    m_modified = false;
    rebuildScene();
    emit documentChanged();
    emit statusMessageChanged(QStringLiteral("Project opened: %1").arg(filePath));
    return true;
}

bool EditorController::exportSvg(const QString& filePath, QString* error) const
{
    return m_svgExporter.exportGeometry(m_document, m_geometry, filePath, error);
}

void EditorController::applySnapshot(const Document& snapshot)
{
    m_document = snapshot;
    rebuildScene();
    m_modified = true;
    emit documentChanged();
}

void EditorController::rebuildScene()
{
    const TextObject& textObject = m_document.primaryTextObject();
    const QString key = geometryCacheKey(textObject);
    ShapedText shaped;
    if (!m_baseGeometry.has_value() || m_baseGeometryKey != key) {
        shaped = m_textEngine.shape(textObject);
        m_baseGeometry = GlyphGeometryBuilder::build(shaped, textObject.typography.fontSize);
        m_baseGeometryKey = key;
    } else {
        shaped = m_textEngine.shape(textObject);
    }

    m_geometry = *m_baseGeometry;
    textObject.effects.apply(m_geometry);

    if (!shaped.error.isEmpty()) {
        publishError(shaped.error);
    } else if (!shaped.warning.isEmpty()) {
        emit statusMessageChanged(shaped.warning);
    } else {
        emit statusMessageChanged(QStringLiteral("Ready."));
    }
    emit sceneChanged();
}

QString EditorController::geometryCacheKey(const TextObject& object) const
{
    return object.sourceText
        + QChar(0x1f)
        + object.font.family
        + QChar(0x1f)
        + object.font.styleName
        + QChar(0x1f)
        + QString::number(object.font.weight)
        + QChar(0x1f)
        + QString::number(object.typography.fontSize, 'g', 16)
        + QChar(0x1f)
        + QString::number(object.typography.tracking, 'g', 16);
}

void EditorController::pushMutation(const QString& description,
                                     const std::function<void(Document&)>& mutation,
                                     int mergeId)
{
    const Document before = m_document;
    Document after = before;
    mutation(after);

    const QByteArray beforeJson = ProjectSerializer::toJson(before).toJson(QJsonDocument::Compact);
    const QByteArray afterJson = ProjectSerializer::toJson(after).toJson(QJsonDocument::Compact);
    if (beforeJson == afterJson) {
        return;
    }

    after.touchModified();
    auto apply = [this](const Document& snapshot) {
        applySnapshot(snapshot);
    };
    m_undoStack.push(new SnapshotCommand(std::move(apply), before, std::move(after), description, mergeId));
}

void EditorController::publishError(const QString& message)
{
    emit statusMessageChanged(QStringLiteral("Error: %1").arg(message));
}

} // namespace vt
