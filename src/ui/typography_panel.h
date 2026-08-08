#pragma once

#include "core/document/document.h"

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
    void refresh(const TextObject& object);

signals:
    void textChangedByUser(const QString& text);
    void fontFamilyChanged(const QString& family);
    void fontStyleChanged(const QString& styleName);
    void fontWeightChanged(int weight);
    void fontSizeChanged(qreal size);
    void trackingChanged(qreal tracking);
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
    QDoubleSpinBox* m_fontSizeSpin = nullptr;
    QDoubleSpinBox* m_trackingSpin = nullptr;
    QPushButton* m_fillButton = nullptr;
    QPushButton* m_refreshFontsButton = nullptr;
    QColor m_fill;
};

} // namespace vt
