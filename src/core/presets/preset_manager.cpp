#include "core/presets/preset_manager.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>

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

QString PresetManager::filePathForName(const QString& name) const
{
    QString safeName = name.trimmed();
    safeName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9 _.-]")), QStringLiteral("_"));
    safeName.replace(QStringLiteral(".."), QStringLiteral("_"));
    if (safeName.isEmpty()) {
        safeName = QStringLiteral("preset");
    }
    return QDir(m_directoryPath).filePath(safeName + QStringLiteral(".json"));
}

QStringList PresetManager::listPresetNames(QString* error) const
{
    if (!ensureDirectory(error)) {
        return {};
    }

    QStringList names;
    const QDir directory(m_directoryPath);
    const QStringList files = directory.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString& fileName : files) {
        QFile file(directory.filePath(fileName));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error) {
                *error = QStringLiteral("Cannot read preset '%1': %2").arg(fileName, file.errorString());
            }
            return {};
        }
        QJsonParseError parseError;
        const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            if (error) {
                *error = QStringLiteral("Preset '%1' is invalid: %2").arg(fileName, parseError.errorString());
            }
            return {};
        }
        Preset preset;
        QString parseMessage;
        if (!Preset::fromJson(json, &preset, &parseMessage)) {
            if (error) {
                *error = QStringLiteral("Preset '%1' is invalid: %2").arg(fileName, parseMessage);
            }
            return {};
        }
        names.push_back(preset.name);
    }
    return names;
}

bool PresetManager::savePreset(const Preset& preset, QString* error) const
{
    if (preset.name.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("Preset name cannot be empty.");
        }
        return false;
    }
    if (!ensureDirectory(error)) {
        return false;
    }

    QSaveFile file(filePathForName(preset.name));
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

bool PresetManager::loadPreset(const QString& name, Preset* preset, QString* error) const
{
    QFile file(filePathForName(name));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot open preset '%1': %2").arg(name, file.errorString());
        }
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("Preset JSON is invalid: %1").arg(parseError.errorString());
        }
        return false;
    }
    return Preset::fromJson(json, preset, error);
}

bool PresetManager::deletePreset(const QString& name, QString* error) const
{
    const QString filePath = filePathForName(name);
    if (!QFileInfo::exists(filePath)) {
        if (error) {
            *error = QStringLiteral("Preset '%1' does not exist.").arg(name);
        }
        return false;
    }
    if (!QFile::remove(filePath)) {
        if (error) {
            *error = QStringLiteral("Could not delete preset '%1'.").arg(name);
        }
        return false;
    }
    return true;
}

} // namespace vt
