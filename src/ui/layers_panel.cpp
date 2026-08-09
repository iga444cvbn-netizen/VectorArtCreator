#include "ui/layers_panel.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace vt {

LayersPanel::LayersPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(m_list);

    auto* buttons = new QHBoxLayout();
    auto* add = new QPushButton(QStringLiteral("+"), this);
    auto* remove = new QPushButton(QStringLiteral("−"), this);
    auto* rename = new QPushButton(QStringLiteral("Rename"), this);
    auto* visible = new QPushButton(QStringLiteral("Visible"), this);
    auto* lock = new QPushButton(QStringLiteral("Lock"), this);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addWidget(rename);
    buttons->addWidget(visible);
    buttons->addWidget(lock);
    root->addLayout(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) {
            emit layerSelected(m_list->item(row)->data(Qt::UserRole).toString());
        }
    });
    connect(add, &QPushButton::clicked, this, &LayersPanel::addLayerRequested);
    connect(remove, &QPushButton::clicked, this, &LayersPanel::removeLayerRequested);
    connect(rename, &QPushButton::clicked, this, [this] {
        QListWidgetItem* item = m_list->currentItem();
        if (!item) {
            return;
        }
        bool accepted = false;
        const QString name = QInputDialog::getText(this,
                                                   QStringLiteral("Rename layer"),
                                                   QStringLiteral("Name"),
                                                   QLineEdit::Normal,
                                                   item->text(),
                                                   &accepted);
        if (accepted) {
            emit renameLayerRequested(name);
        }
    });
    connect(visible, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem* item = m_list->currentItem()) {
            const bool next = item->data(Qt::UserRole + 1).toBool();
            emit visibilityToggled(!next);
        }
    });
    connect(lock, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem* item = m_list->currentItem()) {
            const bool next = item->data(Qt::UserRole + 2).toBool();
            emit lockToggled(!next);
        }
    });
}

void LayersPanel::refresh(const Page& page, const QString& activeLayerId)
{
    const QSignalBlocker blocker(m_list);
    m_list->clear();
    for (const auto& layer : page.layers) {
        if (!layer) {
            continue;
        }
        auto* item = new QListWidgetItem(
            QStringLiteral("%1%2%3").arg(layer->visible ? QStringLiteral("◉ ") : QStringLiteral("○ "),
                                        layer->locked ? QStringLiteral("🔒 ") : QString(),
                                        layer->name),
            m_list);
        item->setData(Qt::UserRole, layer->id);
        item->setData(Qt::UserRole + 1, layer->visible);
        item->setData(Qt::UserRole + 2, layer->locked);
        if (layer->id == activeLayerId) {
            m_list->setCurrentItem(item);
        }
    }
}

} // namespace vt
