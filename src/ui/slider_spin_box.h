#pragma once

#include <QWidget>

class QDoubleSpinBox;
class QSlider;

namespace vt {

// A compact creative-app control that keeps a precise numeric editor and a
// fast interactive slider in sync.  The slider uses a normalized 0..1000
// range, so very large document values do not create a huge QSlider range.
class SliderSpinBox final : public QWidget {
    Q_OBJECT

public:
    explicit SliderSpinBox(QWidget* parent = nullptr);

    void setRange(double minimum, double maximum);
    void setValue(double value);
    void setSingleStep(double step);
    void setDecimals(int decimals);
    void setSuffix(const QString& suffix);
    void setLogarithmic(bool logarithmic);

    [[nodiscard]] double value() const;
    [[nodiscard]] double minimum() const;
    [[nodiscard]] double maximum() const;
    [[nodiscard]] QDoubleSpinBox* spinBox() const { return m_spinBox; }

signals:
    void valueChanged(double value);
    void interactionStarted();
    void interactionFinished();

private:
    void updateSliderFromValue(double value);
    void updateSpinFromSlider(int sliderValue);
    [[nodiscard]] int sliderValueFor(double value) const;
    [[nodiscard]] double valueForSlider(int sliderValue) const;

    QSlider* m_slider = nullptr;
    QDoubleSpinBox* m_spinBox = nullptr;
    bool m_logarithmic = false;
};

} // namespace vt
