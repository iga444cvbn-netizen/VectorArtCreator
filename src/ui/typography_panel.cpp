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

const QString TraitModeLabel = QStringLiteral("Auto / Traits");

bool isTraitModeLabel(const QString& value)
{
    return value == TraitModeLabel;
}

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
    m_textEdit->setObjectName(QStringLiteral("textSource"));
    m_textEdit->setPlaceholderText(QStringLiteral("Enter Latin or Cyrillic text…"));
    m_textEdit->setMinimumHeight(70);
    layout->addRow(QStringLiteral("Text"), m_textEdit);

    m_familyCombo = new QComboBox(group);
    m_familyCombo->setObjectName(QStringLiteral("fontFamily"));
    m_familyCombo->setEditable(true);
    m_familyCombo->setInsertPolicy(QComboBox::NoInsert);
    m_familyCombo->setPlaceholderText(QStringLiteral("Search installed fonts"));
    if (m_familyCombo->completer()) {
        m_familyCombo->completer()->setFilterMode(Qt::MatchContains);
        m_familyCombo->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    }
    layout->addRow(QStringLiteral("Family"), m_familyCombo);

    m_styleCombo = new QComboBox(group);
    m_styleCombo->setObjectName(QStringLiteral("fontStyle"));
    layout->addRow(QStringLiteral("Style"), m_styleCombo);

    m_weightCombo = new QComboBox(group);
    populateWeights();
    m_weightCombo->setToolTip(QStringLiteral("Advanced font weight"));
    auto* formatting = new QWidget(group);
    auto* formattingLayout = new QHBoxLayout(formatting);
    formattingLayout->setContentsMargins(0, 0, 0, 0);
    m_boldButton = formattingButton(formattingIcon(QStringLiteral("B"), false, false, false),
                                    QStringLiteral("Bold"), formatting);
    m_boldButton->setObjectName(QStringLiteral("fontBold"));
    m_italicButton = formattingButton(formattingIcon(QStringLiteral("I"), true, false, false),
                                      QStringLiteral("Italic"), formatting);
    m_italicButton->setObjectName(QStringLiteral("fontItalic"));
    m_underlineButton = formattingButton(formattingIcon(QStringLiteral("U"), false, true, false),
                                         QStringLiteral("Underline"), formatting);
    m_underlineButton->setObjectName(QStringLiteral("fontUnderline"));
    m_strikeOutButton = formattingButton(formattingIcon(QStringLiteral("S"), false, false, true),
                                          QStringLiteral("Strikeout"), formatting);
    m_strikeOutButton->setObjectName(QStringLiteral("fontStrikeOut"));
    formattingLayout->addWidget(m_boldButton);
    formattingLayout->addWidget(m_italicButton);
    formattingLayout->addWidget(m_underlineButton);
    formattingLayout->addWidget(m_strikeOutButton);
    formattingLayout->addStretch(1);
    layout->addRow(QStringLiteral("Format"), formatting);
    layout->addRow(QStringLiteral("Advanced weight"), m_weightCombo);

    m_fontSizeSlider = new SliderSpinBox(group);
    m_fontSizeSlider->setObjectName(QStringLiteral("fontSize"));
    m_fontSizeSlider->setRange(1.0, 2000.0);
    m_fontSizeSlider->setSingleStep(1.0);
    m_fontSizeSlider->setDecimals(1);
    m_fontSizeSlider->setSuffix(QStringLiteral(" pt"));
    m_fontSizeSlider->setLogarithmic(true);
    layout->addRow(QStringLiteral("Size"), m_fontSizeSlider);

    m_trackingSlider = new SliderSpinBox(group);
    m_trackingSlider->setObjectName(QStringLiteral("tracking"));
    m_trackingSlider->setRange(-1.0, 1.0);
    m_trackingSlider->setSingleStep(0.01);
    m_trackingSlider->setDecimals(3);
    m_trackingSlider->setSuffix(QStringLiteral(" em"));
    layout->addRow(QStringLiteral("Tracking"), m_trackingSlider);

    m_lineSpacingSlider = new SliderSpinBox(group);
    m_lineSpacingSlider->setObjectName(QStringLiteral("lineSpacing"));
    m_lineSpacingSlider->setRange(0.5, 3.0);
    m_lineSpacingSlider->setSingleStep(0.05);
    m_lineSpacingSlider->setDecimals(2);
    m_lineSpacingSlider->setSuffix(QStringLiteral(" ×"));
    layout->addRow(QStringLiteral("Line spacing"), m_lineSpacingSlider);

    m_layoutModeCombo = new QComboBox(group);
    m_layoutModeCombo->setObjectName(QStringLiteral("typographyLayoutMode"));
    m_layoutModeCombo->addItem(QStringLiteral("Baseline"),
                               static_cast<int>(TypographyLayoutMode::Baseline));
    m_layoutModeCombo->addItem(QStringLiteral("Path"),
                               static_cast<int>(TypographyLayoutMode::Path));
    m_layoutModeCombo->addItem(QStringLiteral("Region"),
                               static_cast<int>(TypographyLayoutMode::Region));
    layout->addRow(QStringLiteral("Layout mode"), m_layoutModeCombo);

    auto* regionPresetButtons = new QWidget(group);
    auto* regionPresetLayout = new QHBoxLayout(regionPresetButtons);
    regionPresetLayout->setContentsMargins(0, 0, 0, 0);
    m_createRegionRectangle = new QPushButton(QStringLiteral("Rectangle"), regionPresetButtons);
    m_createRegionRectangle->setObjectName(QStringLiteral("createRegionRectangle"));
    m_createRegionEllipse = new QPushButton(QStringLiteral("Ellipse"), regionPresetButtons);
    m_createRegionEllipse->setObjectName(QStringLiteral("createRegionEllipse"));
    m_createRegionCustom = new QPushButton(QStringLiteral("Custom"), regionPresetButtons);
    m_createRegionCustom->setObjectName(QStringLiteral("createRegionCustom"));
    regionPresetLayout->addWidget(m_createRegionRectangle);
    regionPresetLayout->addWidget(m_createRegionEllipse);
    regionPresetLayout->addWidget(m_createRegionCustom);
    layout->addRow(QStringLiteral("Region preset"), regionPresetButtons);

    auto makePadding = [group](const QString& name) {
        auto* slider = new SliderSpinBox(group);
        slider->setRange(0.0, 100000.0);
        slider->setSingleStep(1.0);
        slider->setDecimals(1);
        slider->setSuffix(QStringLiteral(" px"));
        slider->setObjectName(name);
        return slider;
    };
    m_regionPaddingLeft = makePadding(QStringLiteral("regionPaddingLeft"));
    m_regionPaddingRight = makePadding(QStringLiteral("regionPaddingRight"));
    m_regionPaddingTop = makePadding(QStringLiteral("regionPaddingTop"));
    m_regionPaddingBottom = makePadding(QStringLiteral("regionPaddingBottom"));
    layout->addRow(QStringLiteral("Region padding L"), m_regionPaddingLeft);
    layout->addRow(QStringLiteral("Region padding R"), m_regionPaddingRight);
    layout->addRow(QStringLiteral("Region padding T"), m_regionPaddingTop);
    layout->addRow(QStringLiteral("Region padding B"), m_regionPaddingBottom);

    m_regionHorizontal = new QComboBox(group);
    m_regionHorizontal->setObjectName(QStringLiteral("regionHorizontalAlignment"));
    m_regionHorizontal->addItem(QStringLiteral("Left"), static_cast<int>(RegionHorizontalAlignment::Left));
    m_regionHorizontal->addItem(QStringLiteral("Center"), static_cast<int>(RegionHorizontalAlignment::Center));
    m_regionHorizontal->addItem(QStringLiteral("Right"), static_cast<int>(RegionHorizontalAlignment::Right));
    m_regionHorizontal->addItem(QStringLiteral("Justified"), static_cast<int>(RegionHorizontalAlignment::Justified));
    layout->addRow(QStringLiteral("Region horizontal"), m_regionHorizontal);
    m_regionVertical = new QComboBox(group);
    m_regionVertical->setObjectName(QStringLiteral("regionVerticalAlignment"));
    m_regionVertical->addItem(QStringLiteral("Top"), static_cast<int>(RegionVerticalAlignment::Top));
    m_regionVertical->addItem(QStringLiteral("Center"), static_cast<int>(RegionVerticalAlignment::Center));
    m_regionVertical->addItem(QStringLiteral("Bottom"), static_cast<int>(RegionVerticalAlignment::Bottom));
    layout->addRow(QStringLiteral("Region vertical"), m_regionVertical);
    m_regionOverflow = new QComboBox(group);
    m_regionOverflow->setObjectName(QStringLiteral("regionOverflow"));
    m_regionOverflow->addItem(QStringLiteral("Clip"), static_cast<int>(RegionOverflowMode::Clip));
    layout->addRow(QStringLiteral("Region overflow"), m_regionOverflow);

    auto* regionEditButtons = new QWidget(group);
    auto* regionEditLayout = new QHBoxLayout(regionEditButtons);
    regionEditLayout->setContentsMargins(0, 0, 0, 0);
    m_editRegion = new QPushButton(QStringLiteral("Edit Region"), regionEditButtons);
    m_editRegion->setObjectName(QStringLiteral("editRegion"));
    m_editRegionHole = new QPushButton(QStringLiteral("Edit Hole"), regionEditButtons);
    m_editRegionHole->setObjectName(QStringLiteral("editRegionHole"));
    m_removeRegion = new QPushButton(QStringLiteral("Remove Region"), regionEditButtons);
    m_removeRegion->setObjectName(QStringLiteral("removeRegion"));
    m_addRegionHole = new QPushButton(QStringLiteral("Add Hole"), regionEditButtons);
    m_addRegionHole->setObjectName(QStringLiteral("addRegionHole"));
    m_removeRegionHole = new QPushButton(QStringLiteral("Remove Hole"), regionEditButtons);
    m_removeRegionHole->setObjectName(QStringLiteral("removeRegionHole"));
    regionEditLayout->addWidget(m_editRegion);
    regionEditLayout->addWidget(m_editRegionHole);
    regionEditLayout->addWidget(m_removeRegion);
    regionEditLayout->addWidget(m_addRegionHole);
    regionEditLayout->addWidget(m_removeRegionHole);
    layout->addRow(QString(), regionEditButtons);

    m_pathEnabled = new QCheckBox(QStringLiteral("Text on path"), group);
    m_pathEnabled->setObjectName(QStringLiteral("textOnPathEnabled"));
    layout->addRow(QStringLiteral("Path layout"), m_pathEnabled);
    m_createPathButton = new QPushButton(QStringLiteral("Create / edit path"), group);
    m_createPathButton->setObjectName(QStringLiteral("createTextPath"));
    layout->addRow(QString(), m_createPathButton);
    m_pathStartOffset = new SliderSpinBox(group);
    m_pathStartOffset->setObjectName(QStringLiteral("pathStartOffset"));
    m_pathStartOffset->setRange(-100000.0, 100000.0);
    m_pathStartOffset->setSingleStep(1.0);
    m_pathStartOffset->setDecimals(1);
    m_pathStartOffset->setSuffix(QStringLiteral(" px"));
    layout->addRow(QStringLiteral("Path start"), m_pathStartOffset);
    m_pathBaselineOffset = new SliderSpinBox(group);
    m_pathBaselineOffset->setObjectName(QStringLiteral("pathBaselineOffset"));
    m_pathBaselineOffset->setRange(-100000.0, 100000.0);
    m_pathBaselineOffset->setSingleStep(1.0);
    m_pathBaselineOffset->setDecimals(1);
    m_pathBaselineOffset->setSuffix(QStringLiteral(" px"));
    layout->addRow(QStringLiteral("Path baseline"), m_pathBaselineOffset);
    m_pathReverse = new QCheckBox(QStringLiteral("Reverse traversal"), group);
    m_pathReverse->setObjectName(QStringLiteral("pathReverse"));
    layout->addRow(QString(), m_pathReverse);
    m_pathFlip = new QCheckBox(QStringLiteral("Flip text side"), group);
    m_pathFlip->setObjectName(QStringLiteral("pathFlip"));
    layout->addRow(QString(), m_pathFlip);
    m_pathFollowTangent = new QCheckBox(QStringLiteral("Follow tangent"), group);
    m_pathFollowTangent->setObjectName(QStringLiteral("pathFollowTangent"));
    m_pathFollowTangent->setChecked(true);
    layout->addRow(QString(), m_pathFollowTangent);
    m_pathClosed = new QCheckBox(QStringLiteral("Closed path"), group);
    m_pathClosed->setObjectName(QStringLiteral("pathClosed"));
    layout->addRow(QString(), m_pathClosed);
    auto* pathButtons = new QWidget(group);
    auto* pathButtonsLayout = new QHBoxLayout(pathButtons);
    pathButtonsLayout->setContentsMargins(0, 0, 0, 0);
    m_reversePathButton = new QPushButton(QStringLiteral("Reverse geometry"), pathButtons);
    m_reversePathButton->setObjectName(QStringLiteral("reverseTextPath"));
    m_removePathButton = new QPushButton(QStringLiteral("Remove"), pathButtons);
    m_removePathButton->setObjectName(QStringLiteral("removeTextPath"));
    pathButtonsLayout->addWidget(m_reversePathButton);
    pathButtonsLayout->addWidget(m_removePathButton);
    layout->addRow(QString(), pathButtons);

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
        if (!isTraitModeLabel(style)) {
            emit fontStyleChanged(style);
        }
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
    connect(m_layoutModeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) {
            emit typographyLayoutModeChanged(m_layoutModeCombo->itemData(index).toInt());
        }
    });
    connect(m_createRegionRectangle, &QPushButton::clicked,
            this, &TypographyPanel::createRegionRectangleRequested);
    connect(m_createRegionEllipse, &QPushButton::clicked,
            this, &TypographyPanel::createRegionEllipseRequested);
    connect(m_createRegionCustom, &QPushButton::clicked,
            this, &TypographyPanel::createRegionCustomRequested);
    const auto connectPadding = [this](SliderSpinBox* slider, int side) {
        connect(slider, &SliderSpinBox::valueChanged, this,
                [this, side](double value) { emit regionPaddingChanged(side, value); });
        connect(slider, &SliderSpinBox::interactionStarted, this,
                [this, side] { emit regionPaddingInteractionStarted(side); });
        connect(slider, &SliderSpinBox::interactionFinished, this,
                [this] { emit regionPaddingInteractionFinished(); });
    };
    connectPadding(m_regionPaddingLeft, static_cast<int>(RegionPaddingSide::Left));
    connectPadding(m_regionPaddingRight, static_cast<int>(RegionPaddingSide::Right));
    connectPadding(m_regionPaddingTop, static_cast<int>(RegionPaddingSide::Top));
    connectPadding(m_regionPaddingBottom, static_cast<int>(RegionPaddingSide::Bottom));
    connect(m_regionHorizontal, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) {
            emit regionHorizontalAlignmentChanged(m_regionHorizontal->itemData(index).toInt());
        }
    });
    connect(m_regionVertical, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) {
            emit regionVerticalAlignmentChanged(m_regionVertical->itemData(index).toInt());
        }
    });
    connect(m_regionOverflow, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) {
            emit regionOverflowChanged(m_regionOverflow->itemData(index).toInt());
        }
    });
    connect(m_editRegion, &QPushButton::clicked,
            this, &TypographyPanel::editRegionRequested);
    connect(m_editRegionHole, &QPushButton::clicked,
            this, &TypographyPanel::editRegionHoleRequested);
    connect(m_removeRegion, &QPushButton::clicked,
            this, &TypographyPanel::removeRegionRequested);
    connect(m_addRegionHole, &QPushButton::clicked,
            this, &TypographyPanel::addRegionHoleRequested);
    connect(m_removeRegionHole, &QPushButton::clicked,
            this, &TypographyPanel::removeRegionHoleRequested);
    connect(m_pathEnabled, &QCheckBox::toggled,
            this, &TypographyPanel::pathLayoutEnabledChanged);
    connect(m_pathStartOffset, &SliderSpinBox::valueChanged, this,
            [this](double value) { emit pathStartOffsetChanged(value); });
    connect(m_pathStartOffset, &SliderSpinBox::interactionStarted, this,
            &TypographyPanel::pathStartOffsetInteractionStarted);
    connect(m_pathStartOffset, &SliderSpinBox::interactionFinished, this,
            &TypographyPanel::pathStartOffsetInteractionFinished);
    connect(m_pathBaselineOffset, &SliderSpinBox::valueChanged, this,
            [this](double value) { emit pathBaselineOffsetChanged(value); });
    connect(m_pathBaselineOffset, &SliderSpinBox::interactionStarted, this,
            &TypographyPanel::pathBaselineOffsetInteractionStarted);
    connect(m_pathBaselineOffset, &SliderSpinBox::interactionFinished, this,
            &TypographyPanel::pathBaselineOffsetInteractionFinished);
    connect(m_pathReverse, &QCheckBox::toggled,
            this, &TypographyPanel::pathReverseChanged);
    connect(m_pathFlip, &QCheckBox::toggled,
            this, &TypographyPanel::pathFlipChanged);
    connect(m_pathFollowTangent, &QCheckBox::toggled,
            this, &TypographyPanel::pathFollowTangentChanged);
    connect(m_pathClosed, &QCheckBox::toggled,
            this, &TypographyPanel::pathClosedChanged);
    connect(m_createPathButton, &QPushButton::clicked,
            this, &TypographyPanel::createPathRequested);
    connect(m_removePathButton, &QPushButton::clicked,
            this, &TypographyPanel::removePathRequested);
    connect(m_reversePathButton, &QPushButton::clicked,
            this, &TypographyPanel::reversePathRequested);
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

void TypographyPanel::setFontStyles(const QStringList& styles, const QString& resolvedStyle)
{
    const QString current = resolvedStyle.isEmpty() ? m_styleCombo->currentText() : resolvedStyle;
    const QSignalBlocker blocker(m_styleCombo);
    m_styleCombo->clear();
    m_styleCombo->addItem(TraitModeLabel);
    m_styleCombo->addItems(styles);
    if (!isTraitModeLabel(current) && !current.isEmpty() && m_styleCombo->findText(current) >= 0) {
        m_styleCombo->setCurrentText(current);
    } else {
        m_styleCombo->setCurrentIndex(0);
    }
}

void TypographyPanel::refresh(const TextObject* object)
{
    setEnabled(object != nullptr);
    if (!object) {
        {
            const QSignalBlocker blocker(m_textEdit);
            m_textEdit->clear();
            m_textEdit->setPlaceholderText(QStringLiteral("No object selected"));
        }
        {
            const QSignalBlocker blocker(m_pathEnabled);
            m_pathEnabled->setChecked(false);
        }
        {
            const QSignalBlocker blocker(m_pathStartOffset);
            m_pathStartOffset->setValue(0.0);
        }
        {
            const QSignalBlocker blocker(m_pathBaselineOffset);
            m_pathBaselineOffset->setValue(0.0);
        }
        {
            const QSignalBlocker blocker(m_pathReverse);
            m_pathReverse->setChecked(false);
        }
        {
            const QSignalBlocker blocker(m_pathFlip);
            m_pathFlip->setChecked(false);
        }
        {
            const QSignalBlocker blocker(m_pathFollowTangent);
            m_pathFollowTangent->setChecked(true);
        }
        {
            const QSignalBlocker blocker(m_pathClosed);
            m_pathClosed->setChecked(false);
        }
        {
            const QSignalBlocker blocker(m_layoutModeCombo);
            m_layoutModeCombo->setCurrentIndex(0);
        }
        {
            const QSignalBlocker blocker(m_regionPaddingLeft);
            m_regionPaddingLeft->setValue(0.0);
        }
        {
            const QSignalBlocker blocker(m_regionPaddingRight);
            m_regionPaddingRight->setValue(0.0);
        }
        {
            const QSignalBlocker blocker(m_regionPaddingTop);
            m_regionPaddingTop->setValue(0.0);
        }
        {
            const QSignalBlocker blocker(m_regionPaddingBottom);
            m_regionPaddingBottom->setValue(0.0);
        }
        {
            const QSignalBlocker blocker(m_regionHorizontal);
            m_regionHorizontal->setCurrentIndex(0);
        }
        {
            const QSignalBlocker blocker(m_regionVertical);
            m_regionVertical->setCurrentIndex(0);
        }
        {
            const QSignalBlocker blocker(m_regionOverflow);
            m_regionOverflow->setCurrentIndex(0);
        }
        m_layoutModeCombo->setEnabled(false);
        m_createRegionRectangle->setEnabled(false);
        m_createRegionEllipse->setEnabled(false);
        m_createRegionCustom->setEnabled(false);
        m_regionPaddingLeft->setEnabled(false);
        m_regionPaddingRight->setEnabled(false);
        m_regionPaddingTop->setEnabled(false);
        m_regionPaddingBottom->setEnabled(false);
        m_regionHorizontal->setEnabled(false);
        m_regionVertical->setEnabled(false);
        m_regionOverflow->setEnabled(false);
        m_editRegion->setEnabled(false);
        m_editRegionHole->setEnabled(false);
        m_removeRegion->setEnabled(false);
        m_addRegionHole->setEnabled(false);
        m_removeRegionHole->setEnabled(false);
        m_pathEnabled->setEnabled(false);
        m_pathStartOffset->setEnabled(false);
        m_pathBaselineOffset->setEnabled(false);
        m_pathReverse->setEnabled(false);
        m_pathFlip->setEnabled(false);
        m_pathFollowTangent->setEnabled(false);
        m_pathClosed->setEnabled(false);
        m_createPathButton->setEnabled(false);
        m_removePathButton->setEnabled(false);
        m_reversePathButton->setEnabled(false);
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
        const int styleIndex = object->font.styleName.isEmpty()
            ? 0
            : m_styleCombo->findText(object->font.styleName);
        m_styleCombo->setCurrentIndex(styleIndex >= 0 ? styleIndex : 0);
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

    const TypographyLayoutMode layoutMode = activeTypographyLayoutMode(*object);
    const bool hasRegion = object->region.has_value();
    const bool regionActive = layoutMode == TypographyLayoutMode::Region && hasRegion;
    {
        const QSignalBlocker blocker(m_layoutModeCombo);
        const int index = m_layoutModeCombo->findData(static_cast<int>(layoutMode));
        m_layoutModeCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    {
        const QSignalBlocker blocker(m_regionPaddingLeft);
        m_regionPaddingLeft->setValue(object->regionLayout.paddingLeft);
    }
    {
        const QSignalBlocker blocker(m_regionPaddingRight);
        m_regionPaddingRight->setValue(object->regionLayout.paddingRight);
    }
    {
        const QSignalBlocker blocker(m_regionPaddingTop);
        m_regionPaddingTop->setValue(object->regionLayout.paddingTop);
    }
    {
        const QSignalBlocker blocker(m_regionPaddingBottom);
        m_regionPaddingBottom->setValue(object->regionLayout.paddingBottom);
    }
    {
        const QSignalBlocker blocker(m_regionHorizontal);
        const int index = m_regionHorizontal->findData(
            static_cast<int>(object->regionLayout.horizontalAlignment));
        m_regionHorizontal->setCurrentIndex(index >= 0 ? index : 0);
    }
    {
        const QSignalBlocker blocker(m_regionVertical);
        const int index = m_regionVertical->findData(
            static_cast<int>(object->regionLayout.verticalAlignment));
        m_regionVertical->setCurrentIndex(index >= 0 ? index : 0);
    }
    {
        const QSignalBlocker blocker(m_regionOverflow);
        const int index = m_regionOverflow->findData(
            static_cast<int>(object->regionLayout.overflow));
        m_regionOverflow->setCurrentIndex(index >= 0 ? index : 0);
    }
    m_layoutModeCombo->setEnabled(true);
    m_createRegionRectangle->setEnabled(true);
    m_createRegionEllipse->setEnabled(true);
    m_createRegionCustom->setEnabled(true);
    m_regionPaddingLeft->setEnabled(regionActive);
    m_regionPaddingRight->setEnabled(regionActive);
    m_regionPaddingTop->setEnabled(regionActive);
    m_regionPaddingBottom->setEnabled(regionActive);
    m_regionHorizontal->setEnabled(regionActive);
    m_regionVertical->setEnabled(regionActive);
    m_regionOverflow->setEnabled(regionActive);
    m_editRegion->setEnabled(regionActive);
    m_editRegionHole->setEnabled(regionActive && !object->region->holes.isEmpty());
    m_removeRegion->setEnabled(hasRegion);
    m_addRegionHole->setEnabled(regionActive);
    m_removeRegionHole->setEnabled(regionActive && !object->region->holes.isEmpty());

    const bool hasPath = object->path.has_value();
    const bool pathEnabled = hasPath && object->pathLayout.enabled;
    {
        const QSignalBlocker blocker(m_pathEnabled);
        m_pathEnabled->setChecked(pathEnabled);
    }
    {
        const QSignalBlocker blocker(m_pathStartOffset);
        m_pathStartOffset->setValue(object->pathLayout.startOffset);
    }
    {
        const QSignalBlocker blocker(m_pathBaselineOffset);
        m_pathBaselineOffset->setValue(object->pathLayout.baselineOffset);
    }
    {
        const QSignalBlocker blocker(m_pathReverse);
        m_pathReverse->setChecked(object->pathLayout.reverse);
    }
    {
        const QSignalBlocker blocker(m_pathFlip);
        m_pathFlip->setChecked(object->pathLayout.flip);
    }
    {
        const QSignalBlocker blocker(m_pathFollowTangent);
        m_pathFollowTangent->setChecked(object->pathLayout.followTangent);
    }
    {
        const QSignalBlocker blocker(m_pathClosed);
        m_pathClosed->setChecked(hasPath && object->path->closed);
    }
    m_pathEnabled->setEnabled(true);
    m_pathStartOffset->setEnabled(hasPath);
    m_pathBaselineOffset->setEnabled(hasPath);
    m_pathReverse->setEnabled(hasPath);
    m_pathFlip->setEnabled(hasPath);
    m_pathFollowTangent->setEnabled(hasPath);
    m_pathClosed->setEnabled(hasPath);
    m_createPathButton->setEnabled(true);
    m_removePathButton->setEnabled(hasPath);
    m_reversePathButton->setEnabled(hasPath);

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
