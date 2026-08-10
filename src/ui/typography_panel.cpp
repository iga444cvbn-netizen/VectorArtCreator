#include "ui/typography_panel.h"

#include <QColorDialog>
#include <QCompleter>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QTextCursor>
#include <QVBoxLayout>

namespace vt {

namespace {

QIcon formattingIcon(const QString& letter, bool italic, bool underline, bool strikeOut)
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font(QStringLiteral("Segoe UI"), 14, QFont::Bold);
    font.setItalic(italic);
    painter.setFont(font);
    painter.setPen(QColor(225, 230, 240));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, letter);
    if (underline) {
        painter.drawLine(5, 20, 19, 20);
    }
    if (strikeOut) {
        painter.drawLine(4, 12, 20, 12);
    }
    return QIcon(pixmap);
}

QPushButton* formattingButton(const QIcon& icon, const QString& name, QWidget* parent)
{
    auto* button = new QPushButton(parent);
    button->setIcon(icon);
    button->setIconSize(QSize(20, 20));
    button->setAccessibleName(name);
    button->setToolTip(name);
    button->setCheckable(true);
    button->setFixedWidth(32);
    return button;
}

} // namespace

TypographyPanel::TypographyPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto* group = new QGroupBox(QStringLiteral("Typography"), this);
    auto* layout = new QFormLayout(group);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_textEdit = new QPlainTextEdit(group);
    m_textEdit->setPlaceholderText(QStringLiteral("Enter Latin or Cyrillic text…"));
    m_textEdit->setMinimumHeight(70);
    layout->addRow(QStringLiteral("Text"), m_textEdit);

    m_familyCombo = new QComboBox(group);
    m_familyCombo->setEditable(true);
    m_familyCombo->setInsertPolicy(QComboBox::NoInsert);
    m_familyCombo->setPlaceholderText(QStringLiteral("Search installed fonts"));
    if (m_familyCombo->completer()) {
        m_familyCombo->completer()->setFilterMode(Qt::MatchContains);
        m_familyCombo->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    }
    layout->addRow(QStringLiteral("Family"), m_familyCombo);

    m_styleCombo = new QComboBox(group);
    layout->addRow(QStringLiteral("Style"), m_styleCombo);

    m_weightCombo = new QComboBox(group);
    populateWeights();
    m_weightCombo->setToolTip(QStringLiteral("Advanced font weight"));
    auto* formatting = new QWidget(group);
    auto* formattingLayout = new QHBoxLayout(formatting);
    formattingLayout->setContentsMargins(0, 0, 0, 0);
    m_boldButton = formattingButton(formattingIcon(QStringLiteral("B"), false, false, false),
                                    QStringLiteral("Bold"), formatting);
    m_italicButton = formattingButton(formattingIcon(QStringLiteral("I"), true, false, false),
                                      QStringLiteral("Italic"), formatting);
    m_underlineButton = formattingButton(formattingIcon(QStringLiteral("U"), false, true, false),
                                         QStringLiteral("Underline"), formatting);
    m_strikeOutButton = formattingButton(formattingIcon(QStringLiteral("S"), false, false, true),
                                         QStringLiteral("Strikeout"), formatting);
    formattingLayout->addWidget(m_boldButton);
    formattingLayout->addWidget(m_italicButton);
    formattingLayout->addWidget(m_underlineButton);
    formattingLayout->addWidget(m_strikeOutButton);
    formattingLayout->addStretch(1);
    layout->addRow(QStringLiteral("Format"), formatting);
    layout->addRow(QStringLiteral("Advanced weight"), m_weightCombo);

    m_fontSizeSlider = new SliderSpinBox(group);
    m_fontSizeSlider->setRange(1.0, 2000.0);
    m_fontSizeSlider->setSingleStep(1.0);
    m_fontSizeSlider->setDecimals(1);
    m_fontSizeSlider->setSuffix(QStringLiteral(" pt"));
    m_fontSizeSlider->setLogarithmic(true);
    layout->addRow(QStringLiteral("Size"), m_fontSizeSlider);

    m_trackingSlider = new SliderSpinBox(group);
    m_trackingSlider->setRange(-1.0, 1.0);
    m_trackingSlider->setSingleStep(0.01);
    m_trackingSlider->setDecimals(3);
    m_trackingSlider->setSuffix(QStringLiteral(" em"));
    layout->addRow(QStringLiteral("Tracking"), m_trackingSlider);

    m_lineSpacingSlider = new SliderSpinBox(group);
    m_lineSpacingSlider->setRange(0.5, 3.0);
    m_lineSpacingSlider->setSingleStep(0.05);
    m_lineSpacingSlider->setDecimals(2);
    m_lineSpacingSlider->setSuffix(QStringLiteral(" ×"));
    layout->addRow(QStringLiteral("Line spacing"), m_lineSpacingSlider);

    m_fillButton = new QPushButton(group);
    m_fillButton->setText(QStringLiteral("Choose color…"));
    layout->addRow(QStringLiteral("Fill"), m_fillButton);

    m_refreshFontsButton = new QPushButton(QStringLiteral("Refresh installed fonts"), group);
    layout->addRow(QString(), m_refreshFontsButton);

    outerLayout->addWidget(group);

    connect(m_textEdit, &QPlainTextEdit::textChanged, this, [this] {
        emit textChangedByUser(m_textEdit->toPlainText());
    });
    connect(m_familyCombo, &QComboBox::textActivated, this, [this](const QString& family) {
        emit fontFamilyChanged(family);
    });
    connect(m_familyCombo->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        emit fontFamilyChanged(m_familyCombo->currentText());
    });
    connect(m_styleCombo, &QComboBox::currentTextChanged, this, [this](const QString& style) {
        emit fontStyleChanged(style);
    });
    connect(m_weightCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) {
            emit fontWeightChanged(m_weightCombo->itemData(index).toInt());
        }
    });
    connect(m_boldButton, &QPushButton::toggled, this, [this](bool enabled) {
        emit fontWeightChanged(enabled ? static_cast<int>(QFont::Bold) : static_cast<int>(QFont::Normal));
    });
    connect(m_italicButton, &QPushButton::toggled, this, &TypographyPanel::fontItalicChanged);
    connect(m_underlineButton, &QPushButton::toggled, this, &TypographyPanel::fontUnderlineChanged);
    connect(m_strikeOutButton, &QPushButton::toggled, this, &TypographyPanel::fontStrikeOutChanged);
    connect(m_fontSizeSlider, &SliderSpinBox::valueChanged, this,
            [this](double value) { emit fontSizeChanged(value); });
    connect(m_trackingSlider, &SliderSpinBox::valueChanged, this,
            [this](double value) { emit trackingChanged(value); });
    connect(m_lineSpacingSlider, &SliderSpinBox::valueChanged, this,
            [this](double value) { emit lineSpacingChanged(value); });
    connect(m_fillButton, &QPushButton::clicked, this, &TypographyPanel::chooseFillColor);
    connect(m_refreshFontsButton, &QPushButton::clicked, this, &TypographyPanel::refreshFontsRequested);
}

void TypographyPanel::setFontFamilies(const QStringList& families)
{
    const QString current = m_familyCombo->currentText();
    const QSignalBlocker blocker(m_familyCombo);
    m_familyCombo->clear();
    m_familyCombo->addItems(families);
    m_familyCombo->setCurrentText(current);
}

void TypographyPanel::setFontStyles(const QStringList& styles)
{
    const QString current = m_styleCombo->currentText();
    const QSignalBlocker blocker(m_styleCombo);
    m_styleCombo->clear();
    m_styleCombo->addItems(styles);
    if (!current.isEmpty() && m_styleCombo->findText(current) >= 0) {
        m_styleCombo->setCurrentText(current);
    } else if (!styles.isEmpty()) {
        m_styleCombo->setCurrentIndex(0);
    }
}

void TypographyPanel::refresh(const TextObject* object)
{
    setEnabled(object != nullptr);
    if (!object) {
        const QSignalBlocker blocker(m_textEdit);
        m_textEdit->clear();
        m_textEdit->setPlaceholderText(QStringLiteral("No object selected"));
        return;
    }
    m_textEdit->setPlaceholderText(QStringLiteral("Enter Latin or Cyrillic text…"));
    {
        const QSignalBlocker blocker(m_textEdit);
        const QTextCursor oldCursor = m_textEdit->textCursor();
        m_textEdit->setPlainText(object->sourceText);
        QTextCursor newCursor = m_textEdit->textCursor();
        const int newLength = object->sourceText.size();
        newCursor.setPosition(qBound(0, oldCursor.position(), newLength));
        if (oldCursor.hasSelection()) {
            newCursor.setPosition(qBound(0, oldCursor.anchor(), newLength), QTextCursor::KeepAnchor);
        }
        m_textEdit->setTextCursor(newCursor);
    }
    {
        const QSignalBlocker blocker(m_familyCombo);
        m_familyCombo->setCurrentText(object->font.family);
    }
    {
        const QSignalBlocker blocker(m_styleCombo);
        m_styleCombo->setCurrentText(object->font.styleName);
    }
    {
        const QSignalBlocker blocker(m_weightCombo);
        const int index = m_weightCombo->findData(object->font.weight);
        if (index >= 0) {
            m_weightCombo->setCurrentIndex(index);
        }
    }
    {
        const QSignalBlocker blocker(m_boldButton);
        m_boldButton->setChecked(object->font.weight >= static_cast<int>(QFont::Bold));
    }
    {
        const QSignalBlocker blocker(m_italicButton);
        m_italicButton->setChecked(object->font.italic);
    }
    {
        const QSignalBlocker blocker(m_underlineButton);
        m_underlineButton->setChecked(object->font.underline);
    }
    {
        const QSignalBlocker blocker(m_strikeOutButton);
        m_strikeOutButton->setChecked(object->font.strikeOut);
    }
    {
        const QSignalBlocker blocker(m_fontSizeSlider);
        m_fontSizeSlider->setValue(object->typography.fontSize);
    }
    {
        const QSignalBlocker blocker(m_trackingSlider);
        m_trackingSlider->setValue(object->typography.trackingEm);
    }
    {
        const QSignalBlocker blocker(m_lineSpacingSlider);
        m_lineSpacingSlider->setValue(object->typography.lineSpacing);
    }
    const bool multiline = object->sourceText.contains(QLatin1Char('\n'));
    m_lineSpacingSlider->setEnabled(multiline);
    m_lineSpacingSlider->setToolTip(QStringLiteral("Affects spacing between multiple lines."));

    m_fill = object->fill;
    m_fillButton->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; }")
                                    .arg(m_fill.name(QColor::HexRgb)));
}

void TypographyPanel::chooseFillColor()
{
    const QColor selected = QColorDialog::getColor(m_fill, this, QStringLiteral("Choose fill color"));
    if (!selected.isValid()) {
        return;
    }
    m_fill = selected;
    m_fillButton->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; }")
                                    .arg(m_fill.name(QColor::HexRgb)));
    emit fillColorChanged(m_fill);
}

void TypographyPanel::populateWeights()
{
    const QList<QPair<QString, int>> weights = {
        {QStringLiteral("Thin"), static_cast<int>(QFont::Thin)},
        {QStringLiteral("Extra Light"), static_cast<int>(QFont::ExtraLight)},
        {QStringLiteral("Light"), static_cast<int>(QFont::Light)},
        {QStringLiteral("Normal"), static_cast<int>(QFont::Normal)},
        {QStringLiteral("Medium"), static_cast<int>(QFont::Medium)},
        {QStringLiteral("Demi Bold"), static_cast<int>(QFont::DemiBold)},
        {QStringLiteral("Bold"), static_cast<int>(QFont::Bold)},
        {QStringLiteral("Extra Bold"), static_cast<int>(QFont::ExtraBold)},
        {QStringLiteral("Black"), static_cast<int>(QFont::Black)},
    };
    for (const auto& [name, value] : weights) {
        m_weightCombo->addItem(QStringLiteral("%1 (%2)").arg(name).arg(value), value);
    }
}

} // namespace vt
