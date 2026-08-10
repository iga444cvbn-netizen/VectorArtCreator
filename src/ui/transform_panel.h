#pragma once

#include "core/document/document.h"

#include <QWidget>

class QDoubleSpinBox;

namespace vt {

class TransformPanel final : public QWidget {
    Q_OBJECT

public:
    explicit TransformPanel(QWidget* parent = nullptr);
    void refresh(const TextObject* object);

signals:
    void transformChanged(const ObjectTransform& transform);

private:
    void emitTransform();

    QDoubleSpinBox* m_x = nullptr;
    QDoubleSpinBox* m_y = nullptr;
    QDoubleSpinBox* m_rotation = nullptr;
    QDoubleSpinBox* m_scaleX = nullptr;
    QDoubleSpinBox* m_scaleY = nullptr;
    ObjectTransform m_transform;
};

} // namespace vt
