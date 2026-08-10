#pragma once

#include "core/presets/preset_catalog.h"

#include <QWidget>

class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace vt {

class StyleGallery final : public QWidget {
    Q_OBJECT
public:
    explicit StyleGallery(QWidget* parent = nullptr);
    void setEntries(const QVector<PresetCatalogEntry>& entries);
    void setTargetAvailable(bool available);

signals:
    void applyPresetRequested(const QString& presetId);
    void duplicateBuiltInRequested(const QString& presetId);
    void deleteUserPresetRequested(const QString& presetId);

private:
    void rebuild();
    void updateActions();
    [[nodiscard]] QStringList favorites() const;
    void setFavorites(const QStringList& ids);
    void markRecent(const QString& id);

    QVector<PresetCatalogEntry> m_entries;
    QLineEdit* m_search = nullptr;
    QComboBox* m_filter = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_apply = nullptr;
    QPushButton* m_favorite = nullptr;
    QPushButton* m_duplicate = nullptr;
    QPushButton* m_delete = nullptr;
    bool m_targetAvailable = false;
};

} // namespace vt
