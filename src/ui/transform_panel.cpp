#include "ui/transform_panel.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace vt {

namespace {

QDoubleSpinBox* makeTransformSpin(QWidget* parent, double minimum, double maximum, double step)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setSingleStep(step);
    spin->setDecimals(2);
    return spin;
}

QDoubleSpinBox* makeScaleSpin(QWidget* parent)
{
    auto* spin = makeTransformSpin(parent, -20.0, 20.0, 0.001);
    spin->setDecimals(4);
    return spin;
}

}

TransformPanel::TransformPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* group = new QGroupBox(QStringLiteral("Object transform"), this);
    auto* form = new QFormLayout(group);
    form->setContentsMargins(8, 8, 8, 8);

    m_x = makeTransformSpin(group, -100000.0, 100000.0, 1.0);
    m_y = makeTransformSpin(group, -100000.0, 100000.0, 1.0);
    m_rotation = makeTransformSpin(group, -360.0, 360.0, 1.0);
    m_rotation->setSuffix(QStringLiteral("°"));
    m_scaleX = makeScaleSpin(group);
    m_scaleX->setObjectName(QStringLiteral("transformScaleX"));
    m_scaleY = makeScaleSpin(group);
    m_scaleY->setObjectName(QStringLiteral("transformScaleY"));
    form->addRow(QStringLiteral("X"), m_x);
    form->addRow(QStringLiteral("Y"), m_y);
    form->addRow(QStringLiteral("Rotation"), m_rotation);
    form->addRow(QStringLiteral("Scale X"), m_scaleX);
    form->addRow(QStringLiteral("Scale Y"), m_scaleY);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(group);

    connect(m_x, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { emitTransform(); });
    connect(m_y, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { emitTransform(); });
    connect(m_rotation, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { emitTransform(); });
    connect(m_scaleX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { emitTransform(); });
    connect(m_scaleY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { emitTransform(); });
}

void TransformPanel::refresh(const TextObject* object)
{
    const bool enabled = object != nullptr;
    setEnabled(enabled);
    if (!object) {
        return;
    }
    m_transform = object->transform;
    const QSignalBlocker xBlocker(m_x);
    const QSignalBlocker yBlocker(m_y);
    const QSignalBlocker rotationBlocker(m_rotation);
    const QSignalBlocker scaleXBlocker(m_scaleX);
    const QSignalBlocker scaleYBlocker(m_scaleY);
    m_x->setValue(object->transform.position.x());
    m_y->setValue(object->transform.position.y());
    m_rotation->setValue(object->transform.rotation);
    m_scaleX->setValue(object->transform.scale.x());
    m_scaleY->setValue(object->transform.scale.y());
}

void TransformPanel::emitTransform()
{
    m_transform.position = QPointF(m_x->value(), m_y->value());
    m_transform.rotation = m_rotation->value();
    m_transform.scale = QPointF(m_scaleX->value(), m_scaleY->value());
    m_transform.normalizeScale();
    // QDoubleSpinBox otherwise continues to show 0.00 even though the model
    // correctly clamps it to MinimumScale.  Reflect the committed domain
    // immediately, including negative mirrored values.
    {
        const QSignalBlocker xBlocker(m_scaleX);
        const QSignalBlocker yBlocker(m_scaleY);
        m_scaleX->setValue(m_transform.scale.x());
        m_scaleY->setValue(m_transform.scale.y());
    }
    emit transformChanged(m_transform);
}

} // namespace vt
