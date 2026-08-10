#include "core/presets/preset_manager.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <utility>

namespace vt {

PresetManager::PresetManager(QString directoryPath)
    : m_directoryPath(std::move(directoryPath))
{
}

QString PresetManager::directoryPath() const
{
    return m_directoryPath;
}

bool PresetManager::ensureDirectory(QString* error) const
{
    if (QDir(m_directoryPath).exists()) {
        return true;
    }
    if (!QDir().mkpath(m_directoryPath)) {
        if (error) {
            *error = QStringLiteral("Cannot create preset directory: %1").arg(m_directoryPath);
        }
        return false;
    }
    return true;
}

bool PresetManager::isStorageId(const QString& id)
{
    static const QRegularExpression uuidPattern(
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$"));
    return uuidPattern.match(id).hasMatch();
}

QString PresetManager::filePathForId(const QString& id) const
{
    return QDir(m_directoryPath).filePath(id + QStringLiteral(".json"));
}

QString PresetManager::legacyFilePathForName(const QString& name) const
{
    // This mapping is retained only so v1 ASCII preset files remain readable.
    // New files never use display names as storage paths.
    QString safeName = name.trimmed();
    safeName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9 _.-]")), QStringLiteral("_"));
    safeName.replace(QStringLiteral(".."), QStringLiteral("_"));
    if (safeName.isEmpty()) {
        safeName = QStringLiteral("preset");
    }
    return QDir(m_directoryPath).filePath(safeName + QStringLiteral(".json"));
}

bool PresetManager::readPresetFile(const QString& filePath, Preset* preset, QString* error) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot read preset '%1': %2")
                         .arg(QFileInfo(filePath).fileName(), file.errorString());
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("Preset '%1' is invalid: %2")
                         .arg(QFileInfo(filePath).fileName(), parseError.errorString());
        }
        return false;
    }

    QString parseMessage;
    if (!Preset::fromJson(json, preset, &parseMessage)) {
        if (error) {
            *error = QStringLiteral("Preset '%1' is invalid: %2")
                         .arg(QFileInfo(filePath).fileName(), parseMessage);
        }
        return false;
    }
    return true;
}

QVector<PresetManager::PresetRecord> PresetManager::readPresetRecords(QString* error) const
{
    if (!ensureDirectory(error)) {
        return {};
    }

    QVector<PresetRecord> records;
    const QDir directory(m_directoryPath);
    const QStringList files = directory.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString& fileName : files) {
        const QString filePath = directory.filePath(fileName);
        Preset preset;
        QString entryError;
        if (!readPresetFile(filePath, &preset, &entryError)) {
            // A corrupt user file is isolated; valid presets remain usable.
            if (error) {
                *error += error->isEmpty() ? entryError : QStringLiteral("\n") + entryError;
            }
            continue;
        }

        PresetInfo info;
        info.id = isStorageId(preset.id) ? preset.id : QString();
        info.name = preset.name;
        records.push_back({info, filePath});
    }
    return records;
}

QVector<PresetInfo> PresetManager::listPresets(QString* error) const
{
    QVector<PresetInfo> result;
    const QVector<PresetRecord> records = readPresetRecords(error);
    result.reserve(records.size());
    for (const PresetRecord& record : records) {
        result.push_back(record.info);
    }
    return result;
}

QStringList PresetManager::listPresetNames(QString* error) const
{
    QStringList names;
    const QVector<PresetInfo> presets = listPresets(error);
    names.reserve(presets.size());
    for (const PresetInfo& preset : presets) {
        names.push_back(preset.name);
    }
    return names;
}

bool PresetManager::savePreset(Preset preset, QString* error) const
{
    preset.name = preset.name.trimmed();
    if (preset.name.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Preset name cannot be empty.");
        }
        return false;
    }
    if (!ensureDirectory(error)) {
        return false;
    }

    const QVector<PresetRecord> existing = readPresetRecords(error);
    for (const PresetRecord& record : existing) {
        if (record.info.name == preset.name
            && (!isStorageId(preset.id) || record.info.id != preset.id)) {
            if (error) {
                *error = QStringLiteral("A preset named '%1' already exists.").arg(preset.name);
            }
            return false;
        }
    }

    if (!isStorageId(preset.id)) {
        preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    QSaveFile file(filePathForId(preset.id));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot open preset for writing: %1").arg(file.errorString());
        }
        return false;
    }
    const QByteArray data = preset.toJson().toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit()) {
        if (error) {
            *error = QStringLiteral("Could not save preset: %1").arg(file.errorString());
        }
        return false;
    }
    return true;
}

bool PresetManager::loadPresetById(const QString& id, Preset* preset, QString* error) const
{
    if (!preset || !isStorageId(id)) {
        if (error) {
            *error = QStringLiteral("Preset storage ID is invalid.");
        }
        return false;
    }
    const QString filePath = filePathForId(id);
    if (!QFileInfo::exists(filePath)) {
        if (error) {
            *error = QStringLiteral("Preset '%1' does not exist.").arg(id);
        }
        return false;
    }
    if (!readPresetFile(filePath, preset, error)) {
        return false;
    }
    if (!preset->id.isEmpty() && preset->id.compare(id, Qt::CaseInsensitive) != 0) {
        if (error) {
            *error = QStringLiteral("Preset storage ID does not match its file.");
        }
        return false;
    }
    return true;
}

bool PresetManager::loadPreset(const QString& name, Preset* preset, QString* error) const
{
    const QVector<PresetRecord> records = readPresetRecords(error);
    const QString requestedName = name.trimmed();
    for (const PresetRecord& record : records) {
        if (record.info.name != requestedName) {
            continue;
        }
        return readPresetFile(record.filePath, preset, error);
    }
    if (error) {
        *error = QStringLiteral("Preset '%1' does not exist.").arg(name);
    }
    return false;
}

bool PresetManager::deletePresetById(const QString& id, QString* error) const
{
    if (!isStorageId(id)) {
        if (error) {
            *error = QStringLiteral("Preset storage ID is invalid.");
        }
        return false;
    }
    const QString filePath = filePathForId(id);
    if (!QFileInfo::exists(filePath)) {
        if (error) {
            *error = QStringLiteral("Preset '%1' does not exist.").arg(id);
        }
        return false;
    }
    if (!QFile::remove(filePath)) {
        if (error) {
            *error = QStringLiteral("Could not delete preset '%1'.").arg(id);
        }
        return false;
    }
    return true;
}

bool PresetManager::deletePreset(const QString& name, QString* error) const
{
    const QVector<PresetRecord> records = readPresetRecords(error);
    const QString requestedName = name.trimmed();
    for (const PresetRecord& record : records) {
        if (record.info.name != requestedName) {
            continue;
        }
        if (!QFile::remove(record.filePath)) {
            if (error) {
                *error = QStringLiteral("Could not delete preset '%1'.").arg(name);
            }
            return false;
        }
        return true;
    }
    if (error) {
        *error = QStringLiteral("Preset '%1' does not exist.").arg(name);
    }
    return false;
}

} // namespace vt
