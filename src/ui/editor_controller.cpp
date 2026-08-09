#include "ui/editor_controller.h"

#include "core/effects/effect.h"
#include "core/serialization/project_serializer.h"

#include <QFontDatabase>
#include <QStandardPaths>

#include <cmath>
#include <utility>

namespace vt {

namespace {

QString defaultPresetDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/presets");
}

bool nearlyEqual(double left, double right)
{
    return std::abs(left - right) < 1.0e-12;
}

bool effectStacksEqual(const EffectStack& left, const EffectStack& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (int index = 0; index < left.size(); ++index) {
        const Effect* leftEffect = left.at(index);
        const Effect* rightEffect = right.at(index);
        if (!leftEffect || !rightEffect
            || leftEffect->typeId() != rightEffect->typeId()
            || leftEffect->enabled != rightEffect->enabled) {
            return false;
        }

        const QVector<EffectParameter> leftParameters = leftEffect->parameterDefinitions();
        const QVector<EffectParameter> rightParameters = rightEffect->parameterDefinitions();
        if (leftParameters.size() != rightParameters.size()) {
            return false;
        }
        for (int parameterIndex = 0; parameterIndex < leftParameters.size(); ++parameterIndex) {
            if (leftParameters[parameterIndex].id != rightParameters[parameterIndex].id
                || !nearlyEqual(leftParameters[parameterIndex].value,
                                rightParameters[parameterIndex].value)) {
                return false;
            }
        }
    }
    return true;
}

std::optional<EffectParameter> findEffectParameter(const Effect& effect, const QString& id)
{
    const QVector<EffectParameter> parameters = effect.parameterDefinitions();
    for (const EffectParameter& parameter : parameters) {
        if (parameter.id == id) {
            return parameter;
        }
    }
    return std::nullopt;
}

} // namespace

EditorController::EditorController(QObject* parent)
    : QObject(parent)
    , m_presetManager(defaultPresetDirectory())
{
    m_undoStack.setClean();
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
    return !m_undoStack.isClean();
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
    m_undoStack.setClean();
    m_textEngine.clearCache();
    m_baseGeometry.reset();
    m_baseGeometryKey.clear();
    rebuildScene();
    emit documentChanged();
    emit statusMessageChanged(QStringLiteral("New project created."));
}

void EditorController::setText(const QString& text)
{
    const TextObject& object = m_document.primaryTextObject();
    if (object.sourceText == text) {
        return;
    }
    m_undoStack.push(new SetTextCommand(
        m_document,
        object.sourceText,
        text,
        [this] { onCommandChanged(); }));
}

void EditorController::setFontFamily(const QString& family)
{
    const TextObject& object = m_document.primaryTextObject();
    if (object.font.family == family) {
        return;
    }
    m_undoStack.push(new SetFontFamilyCommand(
        m_document,
        object.font.family,
        family,
        [this] { onCommandChanged(); }));
}

void EditorController::setFontStyle(const QString& styleName)
{
    const TextObject& object = m_document.primaryTextObject();
    if (object.font.styleName == styleName) {
        return;
    }
    m_undoStack.push(new SetFontStyleCommand(
        m_document,
        object.font.styleName,
        styleName,
        [this] { onCommandChanged(); }));
}

void EditorController::setFontWeight(int weight)
{
    const TextObject& object = m_document.primaryTextObject();
    if (object.font.weight == weight) {
        return;
    }
    m_undoStack.push(new SetFontWeightCommand(
        m_document,
        object.font.weight,
        weight,
        [this] { onCommandChanged(); }));
}

void EditorController::setFontSize(qreal pointSize)
{
    const qreal boundedSize = qBound<qreal>(1.0, pointSize, 2000.0);
    const qreal oldSize = m_document.primaryTextObject().typography.fontSize;
    if (nearlyEqual(oldSize, boundedSize)) {
        return;
    }
    m_undoStack.push(new SetFontSizeCommand(
        m_document,
        oldSize,
        boundedSize,
        [this] { onCommandChanged(); }));
}

void EditorController::setTracking(qreal trackingEm)
{
    const qreal boundedTracking = qBound<qreal>(-1.0, trackingEm, 1.0);
    const qreal oldTracking = m_document.primaryTextObject().typography.trackingEm;
    if (nearlyEqual(oldTracking, boundedTracking)) {
        return;
    }
    m_undoStack.push(new SetTrackingCommand(
        m_document,
        oldTracking,
        boundedTracking,
        [this] { onCommandChanged(); }));
}

void EditorController::setFillColor(const QColor& color)
{
    if (!color.isValid()) {
        return;
    }
    const QColor oldColor = m_document.primaryTextObject().fill;
    if (oldColor == color) {
        return;
    }
    m_undoStack.push(new SetFillColorCommand(
        m_document,
        oldColor,
        color,
        [this] { onCommandChanged(); }));
}

void EditorController::addEffect(const QString& typeId)
{
    std::unique_ptr<Effect> effect = createEffect(typeId);
    if (!effect) {
        publishError(QStringLiteral("Cannot add unknown effect '%1'.").arg(typeId));
        return;
    }
    const QString description = QStringLiteral("Add %1 effect").arg(effect->displayName());
    const int index = m_document.primaryTextObject().effects.size();
    m_undoStack.push(new AddEffectCommand(
        m_document,
        index,
        std::move(effect),
        [this] { onCommandChanged(); },
        description));
}

void EditorController::removeEffect(int index)
{
    const Effect* effect = m_document.primaryTextObject().effects.at(index);
    if (!effect) {
        return;
    }
    m_undoStack.push(new RemoveEffectCommand(
        m_document,
        index,
        effect->clone(),
        [this] { onCommandChanged(); }));
}

void EditorController::moveEffect(int from, int to)
{
    const EffectStack& effects = m_document.primaryTextObject().effects;
    if (from < 0 || from >= effects.size() || to < 0 || to >= effects.size() || from == to) {
        return;
    }
    m_undoStack.push(new ReorderEffectCommand(
        m_document,
        from,
        to,
        [this] { onCommandChanged(); }));
}

void EditorController::setEffectEnabled(int index, bool enabled)
{
    Effect* effect = m_document.primaryTextObject().effects.at(index);
    if (!effect || effect->enabled == enabled) {
        return;
    }
    m_undoStack.push(new SetEffectEnabledCommand(
        m_document,
        index,
        effect->enabled,
        enabled,
        [this] { onCommandChanged(); }));
}

void EditorController::setEffectParameter(int index, const QString& parameterId, double value)
{
    Effect* effect = m_document.primaryTextObject().effects.at(index);
    if (!effect) {
        return;
    }
    const std::optional<EffectParameter> oldParameter = findEffectParameter(*effect, parameterId);
    if (!oldParameter.has_value()) {
        return;
    }

    std::unique_ptr<Effect> candidate = effect->clone();
    if (!candidate || !candidate->setParameter(parameterId, value)) {
        return;
    }
    const std::optional<EffectParameter> newParameter = findEffectParameter(*candidate, parameterId);
    if (!newParameter.has_value() || nearlyEqual(oldParameter->value, newParameter->value)) {
        return;
    }

    m_undoStack.push(new SetEffectParameterCommand(
        m_document,
        index,
        parameterId,
        oldParameter->value,
        newParameter->value,
        [this] { onCommandChanged(); }));
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

    const EffectStack before = m_document.primaryTextObject().effects;
    if (!effectStacksEqual(before, preset.effects)) {
        m_undoStack.push(new ApplyPresetCommand(
            m_document,
            before,
            preset.effects,
            [this] { onCommandChanged(); },
            QStringLiteral("Apply preset '%1'").arg(name)));
    }
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
    m_undoStack.setClean();
    emit documentChanged();
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
    m_undoStack.setClean();
    m_textEngine.clearCache();
    m_baseGeometry.reset();
    m_baseGeometryKey.clear();
    rebuildScene();
    emit documentChanged();
    emit statusMessageChanged(QStringLiteral("Project opened: %1").arg(filePath));
    return true;
}

bool EditorController::exportSvg(const QString& filePath, QString* error) const
{
    return m_svgExporter.exportGeometry(m_document, m_geometry, filePath, error);
}

void EditorController::onCommandChanged()
{
    rebuildScene();
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
        + QString::number(object.typography.trackingEm, 'g', 16);
}

void EditorController::publishError(const QString& message)
{
    emit statusMessageChanged(QStringLiteral("Error: %1").arg(message));
}

} // namespace vt
