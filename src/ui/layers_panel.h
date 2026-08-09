#pragma once

#include "core/document/document.h"

#include <QWidget>

class QListWidget;

namespace vt {

class LayersPanel final : public QWidget {
    Q_OBJECT

public:
    explicit LayersPanel(QWidget* parent = nullptr);
    void refresh(const Page& page, const QString& activeLayerId);

signals:
    void layerSelected(const QString& layerId);
    void addLayerRequested();
    void removeLayerRequested();
    void renameLayerRequested(const QString& name);
    void visibilityToggled(bool visible);
    void lockToggled(bool locked);

private:
    QListWidget* m_list = nullptr;
};

} // namespace vt
