#pragma once

#include "core/presets/preset.h"

#include <QDir>
#include <QStringList>

namespace vt {

class PresetManager {
public:
    explicit PresetManager(QString directoryPath);

    [[nodiscard]] QString directoryPath() const;
    [[nodiscard]] QStringList listPresetNames(QString* error = nullptr) const;
    [[nodiscard]] bool savePreset(const Preset& preset, QString* error = nullptr) const;
    [[nodiscard]] bool loadPreset(const QString& name, Preset* preset, QString* error = nullptr) const;
    [[nodiscard]] bool deletePreset(const QString& name, QString* error = nullptr) const;

private:
    [[nodiscard]] QString filePathForName(const QString& name) const;
    [[nodiscard]] bool ensureDirectory(QString* error) const;

    QString m_directoryPath;
};

} // namespace vt
