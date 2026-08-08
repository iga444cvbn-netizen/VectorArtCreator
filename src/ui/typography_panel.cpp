#include "ui/typography_panel.h"

#include <QColorDialog>
#include <QCompleter>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTextCursor>
#include <QVBoxLayout>

namespace vt {

TypographyPanel::TypographyPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto* group = new QGroupBox(QStringLiteral("Typography"), this);
    auto* layout = new QFormLayout(group);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_textEdit = new QPlainTextEdit(group);
    m_textEdit->setPlaceholderText(QStringLiteral("Enter Latin or Cyrillic text…"));
    m_textEdit->setMinimumHeight(74);
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
    m_styleCombo->setEditable(false);
    layout->addRow(QStringLiteral("Style"), m_styleCombo);

    m_weightCombo = new QComboBox(group);
    populateWeights();
    layout->addRow(QStringLiteral("Weight"), m_weightCombo);

    m_fontSizeSpin = new QDoubleSpinBox(group);
    m_fontSizeSpin->setRange(1.0, 2000.0);
    m_fontSizeSpin->setDecimals(1);
    m_fontSizeSpin->setSingleStep(1.0);
    m_fontSizeSpin->setSuffix(QStringLiteral(" pt"));
    layout->addRow(QStringLiteral("Size"), m_fontSizeSpin);

    m_trackingSpin = new QDoubleSpinBox(group);
    m_trackingSpin->setRange(-500.0, 500.0);
    m_trackingSpin->setDecimals(2);
    m_trackingSpin->setSingleStep(0.5);
    m_trackingSpin->setSuffix(QStringLiteral(" pt"));
    layout->addRow(QStringLiteral("Tracking"), m_trackingSpin);

    m_fillButton = new QPushButton(group);
    m_fillButton->setText(QStringLiteral("Choose color…"));
    layout->addRow(QStringLiteral("Fill"), m_fillButton);

    m_refreshFontsButton = new QPushButton(QStringLiteral("Refresh installed fonts"), group);
    layout->addRow(QString(), m_refreshFontsButton);

    outerLayout->addWidget(group);
    outerLayout->addStretch(1);

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
    connect(m_fontSizeSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        emit fontSizeChanged(value);
    });
    connect(m_trackingSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        emit trackingChanged(value);
    });
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
    if (!current.isEmpty()) {
        m_styleCombo->setCurrentText(current);
    }
}

void TypographyPanel::refresh(const TextObject& object)
{
    {
        const QSignalBlocker blocker(m_textEdit);
        const QTextCursor oldCursor = m_textEdit->textCursor();
        m_textEdit->setPlainText(object.sourceText);
        QTextCursor newCursor = m_textEdit->textCursor();
        const int newLength = object.sourceText.size();
        newCursor.setPosition(qBound(0, oldCursor.position(), newLength));
        if (oldCursor.hasSelection()) {
            newCursor.setPosition(qBound(0, oldCursor.anchor(), newLength), QTextCursor::KeepAnchor);
        }
        m_textEdit->setTextCursor(newCursor);
    }
    {
        const QSignalBlocker blocker(m_familyCombo);
        m_familyCombo->setCurrentText(object.font.family);
    }
    {
        const QSignalBlocker blocker(m_styleCombo);
        m_styleCombo->setCurrentText(object.font.styleName);
    }
    {
        const QSignalBlocker blocker(m_weightCombo);
        const int index = m_weightCombo->findData(object.font.weight);
        if (index >= 0) {
            m_weightCombo->setCurrentIndex(index);
        }
    }
    {
        const QSignalBlocker blocker(m_fontSizeSpin);
        m_fontSizeSpin->setValue(object.typography.fontSize);
    }
    {
        const QSignalBlocker blocker(m_trackingSpin);
        m_trackingSpin->setValue(object.typography.tracking);
    }

    m_fill = object.fill;
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
