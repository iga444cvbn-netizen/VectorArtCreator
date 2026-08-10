#pragma once

#include "core/presets/preset_manager.h"

namespace vt {

struct PresetCatalogEntry {
    Preset preset;
    QString category;
    QString description;
    QStringList tags;
    bool builtIn = false;
};

class PresetCatalog final {
public:
    explicit PresetCatalog(const PresetManager& userPresets);
    [[nodiscard]] QVector<PresetCatalogEntry> entries(QString* diagnostics = nullptr) const;
    [[nodiscard]] bool presetById(const QString& id, PresetCatalogEntry* entry,
                                  QString* error = nullptr) const;
    [[nodiscard]] bool duplicateBuiltIn(const QString& id, Preset* copy,
                                        QString* error = nullptr) const;

private:
    [[nodiscard]] QVector<PresetCatalogEntry> builtinEntries(QString* error) const;
    const PresetManager& m_userPresets;
};

} // namespace vt
