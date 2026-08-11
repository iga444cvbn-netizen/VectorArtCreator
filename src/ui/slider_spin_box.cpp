#include "ui/slider_spin_box.h"

#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSlider>

#include <cmath>

namespace vt {

namespace {

constexpr int SliderSteps = 1000;

}

SliderSpinBox::SliderSpinBox(QWidget* parent)
    : QWidget(parent)
    , m_slider(new QSlider(Qt::Horizontal, this))
    , m_spinBox(new QDoubleSpinBox(this))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(m_slider, 1);
    layout->addWidget(m_spinBox, 0);

    m_slider->setRange(0, SliderSteps);
    m_slider->setTracking(true);
    m_spinBox->setRange(0.0, 1.0);
    m_spinBox->setDecimals(2);
    m_spinBox->setKeyboardTracking(true);
    m_spinBox->setMinimumWidth(82);

    m_slider->installEventFilter(this);
    connect(m_slider, &QSlider::sliderPressed, this, [this] {
        if (m_interactionActive) return;
        m_interactionActive = true;
        emit interactionStarted();
    });
    connect(m_slider, &QSlider::sliderReleased, this, &SliderSpinBox::finishActiveInteraction);
    connect(m_slider, &QSlider::valueChanged, this, &SliderSpinBox::updateSpinFromSlider);
    connect(m_spinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        updateSliderFromValue(value);
        emit valueChanged(value);
    });
}

bool SliderSpinBox::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_slider && m_interactionActive
        && (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide
            || event->type() == QEvent::UngrabMouse)) {
        finishActiveInteraction();
    }
    return QWidget::eventFilter(watched, event);
}

void SliderSpinBox::finishActiveInteraction()
{
    if (!m_interactionActive) return;
    m_interactionActive = false;
    emit interactionFinished();
}

void SliderSpinBox::setRange(double minimum, double maximum)
{
    if (maximum < minimum) {
        std::swap(minimum, maximum);
    }
    m_spinBox->setRange(minimum, maximum);
    setValue(qBound(minimum, value(), maximum));
}

void SliderSpinBox::setValue(double value)
{
    const double bounded = qBound(minimum(), value, maximum());
    const QSignalBlocker spinBlocker(m_spinBox);
    const QSignalBlocker sliderBlocker(m_slider);
    m_spinBox->setValue(bounded);
    m_slider->setValue(sliderValueFor(bounded));
}

void SliderSpinBox::setSingleStep(double step)
{
    m_spinBox->setSingleStep(step);
}

void SliderSpinBox::setDecimals(int decimals)
{
    m_spinBox->setDecimals(qMax(0, decimals));
}

void SliderSpinBox::setSuffix(const QString& suffix)
{
    m_spinBox->setSuffix(suffix);
}

void SliderSpinBox::setLogarithmic(bool logarithmic)
{
    m_logarithmic = logarithmic;
    updateSliderFromValue(value());
}

double SliderSpinBox::value() const
{
    return m_spinBox->value();
}

double SliderSpinBox::minimum() const
{
    return m_spinBox->minimum();
}

double SliderSpinBox::maximum() const
{
    return m_spinBox->maximum();
}

void SliderSpinBox::updateSliderFromValue(double value)
{
    const QSignalBlocker blocker(m_slider);
    m_slider->setValue(sliderValueFor(value));
}

void SliderSpinBox::updateSpinFromSlider(int sliderValue)
{
    const double value = valueForSlider(sliderValue);
    const QSignalBlocker blocker(m_spinBox);
    m_spinBox->setValue(value);
    emit valueChanged(value);
}

int SliderSpinBox::sliderValueFor(double value) const
{
    const double minimumValue = minimum();
    const double maximumValue = maximum();
    if (qFuzzyCompare(minimumValue, maximumValue)) {
        return 0;
    }

    double normalized = 0.0;
    if (m_logarithmic && minimumValue > 0.0 && maximumValue > minimumValue) {
        normalized = (std::log(qMax(minimumValue, value)) - std::log(minimumValue))
            / (std::log(maximumValue) - std::log(minimumValue));
    } else {
        normalized = (value - minimumValue) / (maximumValue - minimumValue);
    }
    return qBound(0, qRound(normalized * SliderSteps), SliderSteps);
}

double SliderSpinBox::valueForSlider(int sliderValue) const
{
    const double normalized = static_cast<double>(sliderValue) / SliderSteps;
    const double minimumValue = minimum();
    const double maximumValue = maximum();
    if (m_logarithmic && minimumValue > 0.0 && maximumValue > minimumValue) {
        return std::exp(std::log(minimumValue)
                        + normalized * (std::log(maximumValue) - std::log(minimumValue)));
    }
    return minimumValue + normalized * (maximumValue - minimumValue);
}

} // namespace vt
