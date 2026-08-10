#include "ui/style_gallery.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QVBoxLayout>

namespace vt {

StyleGallery::StyleGallery(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("styleGallerySearch"));
    m_search->setPlaceholderText(QStringLiteral("Search creative styles…"));
    layout->addWidget(m_search);
    m_filter = new QComboBox(this);
    m_filter->setObjectName(QStringLiteral("styleGalleryFilter"));
    m_filter->addItems({QStringLiteral("All Styles"), QStringLiteral("Built-in"),
                        QStringLiteral("My Presets"), QStringLiteral("Favorites"), QStringLiteral("Recent")});
    layout->addWidget(m_filter);
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("styleGalleryList"));
    m_list->setMinimumHeight(160);
    layout->addWidget(m_list);
    auto* row = new QHBoxLayout();
    m_apply = new QPushButton(QStringLiteral("Apply"), this);
    m_favorite = new QPushButton(QStringLiteral("★"), this);
    m_duplicate = new QPushButton(QStringLiteral("Duplicate"), this);
    m_delete = new QPushButton(QStringLiteral("Delete"), this);
    row->addWidget(m_apply); row->addWidget(m_favorite); row->addWidget(m_duplicate); row->addWidget(m_delete);
    layout->addLayout(row);
    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(m_filter, &QComboBox::currentIndexChanged, this, [this] { rebuild(); });
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        const auto* item = m_list->currentItem();
        const bool builtIn = item && item->data(Qt::UserRole + 1).toBool();
        m_apply->setEnabled(item); m_favorite->setEnabled(item);
        m_duplicate->setEnabled(item && builtIn); m_delete->setEnabled(item && !builtIn);
    });
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (!item) return;
        const QString id = item->data(Qt::UserRole).toString(); markRecent(id); emit applyPresetRequested(id);
    });
    connect(m_apply, &QPushButton::clicked, this, [this] {
        if (auto* item = m_list->currentItem()) { const QString id = item->data(Qt::UserRole).toString(); markRecent(id); emit applyPresetRequested(id); }
    });
    connect(m_favorite, &QPushButton::clicked, this, [this] {
        if (auto* item = m_list->currentItem()) { QStringList ids = favorites(); const QString id = item->data(Qt::UserRole).toString();
            if (ids.contains(id)) ids.removeAll(id); else ids.push_back(id); setFavorites(ids); rebuild(); }
    });
    connect(m_duplicate, &QPushButton::clicked, this, [this] { if (auto* item = m_list->currentItem()) emit duplicateBuiltInRequested(item->data(Qt::UserRole).toString()); });
    connect(m_delete, &QPushButton::clicked, this, [this] { if (auto* item = m_list->currentItem()) emit deleteUserPresetRequested(item->data(Qt::UserRole).toString()); });
    setTargetAvailable(false);
}

void StyleGallery::setEntries(const QVector<PresetCatalogEntry>& entries) { m_entries = entries; rebuild(); }
void StyleGallery::setTargetAvailable(bool available) { m_apply->setEnabled(available && m_list->currentItem()); }
QStringList StyleGallery::favorites() const { return QSettings().value(QStringLiteral("creativeStyles/favorites")).toStringList(); }
void StyleGallery::setFavorites(const QStringList& ids) { QSettings().setValue(QStringLiteral("creativeStyles/favorites"), ids); }
void StyleGallery::markRecent(const QString& id) { QStringList ids = QSettings().value(QStringLiteral("creativeStyles/recent")).toStringList(); ids.removeAll(id); ids.prepend(id); while (ids.size() > 12) ids.removeLast(); QSettings().setValue(QStringLiteral("creativeStyles/recent"), ids); }

void StyleGallery::rebuild()
{
    const QString selected = m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString();
    const QString query = m_search->text().trimmed(); const int filter = m_filter->currentIndex();
    const QStringList starred = favorites(); const QStringList recent = QSettings().value(QStringLiteral("creativeStyles/recent")).toStringList();
    m_list->clear();
    for (const PresetCatalogEntry& entry : m_entries) {
        const bool isFavorite = starred.contains(entry.preset.id);
        if ((filter == 1 && !entry.builtIn) || (filter == 2 && entry.builtIn) || (filter == 3 && !isFavorite) || (filter == 4 && !recent.contains(entry.preset.id))) continue;
        const QString haystack = entry.preset.name + QLatin1Char(' ') + entry.category + QLatin1Char(' ') + entry.description + QLatin1Char(' ') + entry.tags.join(QLatin1Char(' '));
        if (!query.isEmpty() && !haystack.contains(query, Qt::CaseInsensitive)) continue;
        auto* item = new QListWidgetItem(QStringLiteral("%1  %2\n%3").arg(isFavorite ? QStringLiteral("★") : QStringLiteral("•"), entry.preset.name, entry.description), m_list);
        item->setData(Qt::UserRole, entry.preset.id); item->setData(Qt::UserRole + 1, entry.builtIn);
        item->setToolTip(QStringLiteral("%1 • %2").arg(entry.builtIn ? QStringLiteral("Built-in") : QStringLiteral("My Preset"), entry.category));
        if (entry.preset.id == selected) m_list->setCurrentItem(item);
    }
    if (!m_list->currentItem() && m_list->count()) m_list->setCurrentRow(0);
}

} // namespace vt
