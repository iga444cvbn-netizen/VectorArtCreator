#pragma once

#include "core/document/document.h"
#include "ui/slider_spin_box.h"

#include <QColor>
#include <QCheckBox>
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
    void setFontStyles(const QStringList& styles, const QString& resolvedStyle = {});
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
    void pathLayoutEnabledChanged(bool enabled);
    void pathStartOffsetChanged(qreal offset);
    void pathBaselineOffsetChanged(qreal offset);
    void pathReverseChanged(bool reverse);
    void pathFlipChanged(bool flip);
    void pathFollowTangentChanged(bool followTangent);
    void createPathRequested();
    void removePathRequested();
    void reversePathRequested();
    void pathClosedChanged(bool closed);
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
    QCheckBox* m_pathEnabled = nullptr;
    SliderSpinBox* m_pathStartOffset = nullptr;
    SliderSpinBox* m_pathBaselineOffset = nullptr;
    QCheckBox* m_pathReverse = nullptr;
    QCheckBox* m_pathFlip = nullptr;
    QCheckBox* m_pathFollowTangent = nullptr;
    QCheckBox* m_pathClosed = nullptr;
    QPushButton* m_createPathButton = nullptr;
    QPushButton* m_removePathButton = nullptr;
    QPushButton* m_reversePathButton = nullptr;
    QPushButton* m_refreshFontsButton = nullptr;
    QColor m_fill;
};

} // namespace vt
