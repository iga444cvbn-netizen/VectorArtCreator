#pragma once

#include "core/document/document.h"

#include <QWidget>

class QListWidget;
class QTreeWidget;

namespace vt {

class LayersPanel final : public QWidget {
    Q_OBJECT

public:
    explicit LayersPanel(QWidget* parent = nullptr);
    void refresh(const Page& page,
                 const QString& activeLayerId,
                 const QString& activeObjectId = {});

signals:
    void layerSelected(const QString& layerId);
    void objectSelected(const QString& objectId);
    void addLayerRequested();
    void removeLayerRequested();
    void moveLayerUpRequested();
    void moveLayerDownRequested();
    void renameLayerRequested(const QString& name);
    void visibilityToggled(bool visible);
    void lockToggled(bool locked);
    void moveObjectRequested(const QString& objectId, const QString& destinationLayerId);

private:
    QTreeWidget* m_tree = nullptr;
};

} // namespace vt
