#pragma once

#include "core/presets/preset.h"

#include <QDir>
#include <QStringList>
#include <QVector>

namespace vt {

struct PresetInfo {
    QString id;
    QString name;
};

class PresetManager {
public:
    explicit PresetManager(QString directoryPath);

    [[nodiscard]] QString directoryPath() const;
    [[nodiscard]] QVector<PresetInfo> listPresets(QString* error = nullptr) const;
    [[nodiscard]] QStringList listPresetNames(QString* error = nullptr) const;
    [[nodiscard]] bool savePreset(Preset preset, QString* error = nullptr) const;
    [[nodiscard]] bool loadPreset(const QString& name, Preset* preset, QString* error = nullptr) const;
    [[nodiscard]] bool loadPresetById(const QString& id, Preset* preset, QString* error = nullptr) const;
    [[nodiscard]] bool deletePreset(const QString& name, QString* error = nullptr) const;
    [[nodiscard]] bool deletePresetById(const QString& id, QString* error = nullptr) const;

private:
    struct PresetRecord {
        PresetInfo info;
        QString filePath;
    };

    [[nodiscard]] QString filePathForId(const QString& id) const;
    [[nodiscard]] QString legacyFilePathForName(const QString& name) const;
    [[nodiscard]] QVector<PresetRecord> readPresetRecords(QString* error) const;
    [[nodiscard]] bool readPresetFile(const QString& filePath, Preset* preset, QString* error) const;
    [[nodiscard]] static bool isStorageId(const QString& id);
    [[nodiscard]] bool ensureDirectory(QString* error) const;

    QString m_directoryPath;
};

} // namespace vt
