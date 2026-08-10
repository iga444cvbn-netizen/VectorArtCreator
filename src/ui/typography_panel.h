#pragma once

#include "core/document/document.h"
#include "ui/slider_spin_box.h"

#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QWidget>

namespace vt {

class TypographyPanel final : public QWidget {
    Q_OBJECT

public:
    explicit TypographyPanel(QWidget* parent = nullptr);

    void setFontFamilies(const QStringList& families);
    void setFontStyles(const QStringList& styles);
    void refresh(const TextObject* object);
    void refresh(const TextObject& object) { refresh(&object); }

signals:
    void textChangedByUser(const QString& text);
    void fontFamilyChanged(const QString& family);
    void fontStyleChanged(const QString& styleName);
    void fontWeightChanged(int weight);
    void fontItalicChanged(bool italic);
    void fontUnderlineChanged(bool underline);
    void fontStrikeOutChanged(bool strikeOut);
    void fontSizeChanged(qreal size);
    void trackingChanged(qreal tracking);
    void lineSpacingChanged(qreal lineSpacing);
    void fillColorChanged(const QColor& color);
    void refreshFontsRequested();

private slots:
    void chooseFillColor();

private:
    void populateWeights();

    QPlainTextEdit* m_textEdit = nullptr;
    QComboBox* m_familyCombo = nullptr;
    QComboBox* m_styleCombo = nullptr;
    QComboBox* m_weightCombo = nullptr;
    QPushButton* m_boldButton = nullptr;
    QPushButton* m_italicButton = nullptr;
    QPushButton* m_underlineButton = nullptr;
    QPushButton* m_strikeOutButton = nullptr;
    SliderSpinBox* m_fontSizeSlider = nullptr;
    SliderSpinBox* m_trackingSlider = nullptr;
    SliderSpinBox* m_lineSpacingSlider = nullptr;
    QPushButton* m_fillButton = nullptr;
    QPushButton* m_refreshFontsButton = nullptr;
    QColor m_fill;
};

} // namespace vt
