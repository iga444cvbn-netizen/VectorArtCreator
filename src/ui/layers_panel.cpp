#include "ui/layers_panel.h"

#include <QAction>
#include <QHBoxLayout>
#include <QAbstractItemView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace vt {

LayersPanel::LayersPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("layersTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(16);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(m_tree);

    auto* buttons = new QHBoxLayout();
    auto* add = new QPushButton(QStringLiteral("+"), this);
    auto* remove = new QPushButton(QStringLiteral("−"), this);
    auto* moveUp = new QPushButton(QStringLiteral("Up"), this);
    auto* moveDown = new QPushButton(QStringLiteral("Down"), this);
    auto* rename = new QPushButton(QStringLiteral("Rename"), this);
    auto* visible = new QPushButton(QStringLiteral("Visible"), this);
    auto* lock = new QPushButton(QStringLiteral("Lock"), this);
    visible->setObjectName(QStringLiteral("layerVisibleButton"));
    lock->setObjectName(QStringLiteral("layerLockButton"));
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addWidget(moveUp);
    buttons->addWidget(moveDown);
    buttons->addWidget(rename);
    buttons->addWidget(visible);
    buttons->addWidget(lock);
    root->addLayout(buttons);

    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                if (!current) {
                    return;
                }
                const QString objectId = current->data(0, Qt::UserRole + 1).toString();
                if (!objectId.isEmpty()) {
                    emit objectSelected(objectId);
                    return;
                }
                const QString layerId = current->data(0, Qt::UserRole).toString();
                if (!layerId.isEmpty()) {
                    emit layerSelected(layerId);
                }
            });
    connect(add, &QPushButton::clicked, this, &LayersPanel::addLayerRequested);
    connect(remove, &QPushButton::clicked, this, &LayersPanel::removeLayerRequested);
    connect(moveUp, &QPushButton::clicked, this, &LayersPanel::moveLayerUpRequested);
    connect(moveDown, &QPushButton::clicked, this, &LayersPanel::moveLayerDownRequested);
    connect(rename, &QPushButton::clicked, this, [this] {
        QTreeWidgetItem* item = m_tree->currentItem();
        if (!item || item->data(0, Qt::UserRole).toString().isEmpty()) {
            return;
        }
        bool accepted = false;
        const QString name = QInputDialog::getText(this,
                                                   QStringLiteral("Rename layer"),
                                                   QStringLiteral("Name"),
                                                   QLineEdit::Normal,
                                                   item->text(0),
                                                   &accepted);
        if (accepted) {
            emit renameLayerRequested(name);
        }
    });
    connect(visible, &QPushButton::clicked, this, [this] {
        if (QTreeWidgetItem* item = m_tree->currentItem()) {
            if (!item->data(0, Qt::UserRole + 1).toString().isEmpty()) item = item->parent();
            if (!item) return;
            const bool next = item->data(0, Qt::UserRole + 2).toBool();
            emit visibilityToggled(!next);
        }
    });
    connect(lock, &QPushButton::clicked, this, [this] {
        if (QTreeWidgetItem* item = m_tree->currentItem()) {
            if (!item->data(0, Qt::UserRole + 1).toString().isEmpty()) item = item->parent();
            if (!item) return;
            const bool next = item->data(0, Qt::UserRole + 3).toBool();
            emit lockToggled(!next);
        }
    });
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        QTreeWidgetItem* item = m_tree->itemAt(position);
        if (!item) {
            return;
        }
        const QString objectId = item->data(0, Qt::UserRole + 1).toString();
        if (objectId.isEmpty()) {
            return;
        }
        QTreeWidgetItem* sourceLayerItem = item->parent();
        if (!sourceLayerItem) {
            return;
        }
        const QString sourceLayerId = sourceLayerItem->data(0, Qt::UserRole).toString();
        const bool sourceEditable = sourceLayerItem->data(0, Qt::UserRole + 2).toBool()
            && !sourceLayerItem->data(0, Qt::UserRole + 3).toBool();
        m_tree->setCurrentItem(item);

        QMenu menu(this);
        QMenu* moveMenu = menu.addMenu(QStringLiteral("Move to Layer"));
        bool hasDestination = false;
        for (int index = 0; index < m_tree->topLevelItemCount(); ++index) {
            QTreeWidgetItem* target = m_tree->topLevelItem(index);
            if (!target) {
                continue;
            }
            const QString targetLayerId = target->data(0, Qt::UserRole).toString();
            if (targetLayerId.isEmpty() || targetLayerId == sourceLayerId) {
                continue;
            }
            QAction* action = moveMenu->addAction(target->text(0));
            const bool destinationEditable = target->data(0, Qt::UserRole + 2).toBool()
                && !target->data(0, Qt::UserRole + 3).toBool();
            action->setEnabled(sourceEditable && destinationEditable);
            hasDestination = hasDestination || action->isEnabled();
            connect(action, &QAction::triggered, this, [this, objectId, targetLayerId] {
                emit moveObjectRequested(objectId, targetLayerId);
            });
        }
        moveMenu->setEnabled(hasDestination);
        menu.exec(m_tree->viewport()->mapToGlobal(position));
    });
}

void LayersPanel::refresh(const Page& page,
                          const QString& activeLayerId,
                          const QString& activeObjectId)
{
    const QSignalBlocker blocker(m_tree);
    m_tree->clear();
    QTreeWidgetItem* activeItem = nullptr;
    // Painting walks the model from first to last, so the last layer is on
    // top.  Present that same top-to-bottom stack in the panel.
    for (auto iterator = page.layers.rbegin(); iterator != page.layers.rend(); ++iterator) {
        const auto& layer = *iterator;
        if (!layer) {
            continue;
        }
        auto* layerItem = new QTreeWidgetItem(m_tree);
        layerItem->setText(0, QStringLiteral("%1%2%3")
                                  .arg(layer->visible ? QStringLiteral("◉ ") : QStringLiteral("○ "))
                                  .arg(layer->locked ? QStringLiteral("🔒 ") : QString())
                                  .arg(layer->name));
        layerItem->setData(0, Qt::UserRole, layer->id);
        layerItem->setData(0, Qt::UserRole + 2, layer->visible);
        layerItem->setData(0, Qt::UserRole + 3, layer->locked);
        layerItem->setToolTip(0, layer->locked
                                     ? QStringLiteral("Locked layer")
                                     : QStringLiteral("Editable layer"));
        if (layer->id == activeLayerId) {
            activeItem = layerItem;
        }
        for (const auto& object : layer->objects) {
            if (!object) {
                continue;
            }
            auto* objectItem = new QTreeWidgetItem(layerItem);
            const QString preview = object->sourceText.simplified().left(28);
            objectItem->setText(0, preview.isEmpty() ? QStringLiteral("Text object") : preview);
            objectItem->setData(0, Qt::UserRole + 1, object->id);
            objectItem->setToolTip(0, object->sourceText);
            if (object->id == activeObjectId) {
                activeItem = objectItem;
            }
        }
        layerItem->setExpanded(true);
    }
    if (activeItem) {
        m_tree->setCurrentItem(activeItem);
    }
}

} // namespace vt
