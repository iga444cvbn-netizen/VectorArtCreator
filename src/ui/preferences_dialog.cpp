#include "ui/preferences_dialog.h"

#include "ui/shortcut_manager.h"

#include <QCheckBox>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <stdexcept>

namespace vt {

PreferencesDialog::PreferencesDialog(ShortcutManager* shortcutManager, QWidget* parent)
    : QDialog(parent)
    , m_shortcutManager(shortcutManager)
{
    setWindowTitle(QStringLiteral("Preferences"));
    resize(680, 560);
    auto* root = new QVBoxLayout(this);

    auto* form = new QFormLayout();
    m_theme = new QComboBox(this);
    m_theme->addItem(QStringLiteral("Dark"), QStringLiteral("dark"));
    m_theme->addItem(QStringLiteral("Light"), QStringLiteral("light"));
    m_theme->addItem(QStringLiteral("System"), QStringLiteral("system"));
    const QSettings settings;
    const QString theme = settings.value(QStringLiteral("appearance/theme"),
                                         settings.value(QStringLiteral("appearance/darkTheme"), true).toBool()
                                             ? QStringLiteral("dark")
                                             : QStringLiteral("light"))
                              .toString();
    const int themeIndex = m_theme->findData(theme);
    m_theme->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);
    form->addRow(QStringLiteral("Theme"), m_theme);

    m_navigationMode = new QComboBox(this);
    m_navigationMode->addItem(QStringLiteral("Middle drag + Space/Left drag"), QStringLiteral("middleSpace"));
    m_navigationMode->addItem(QStringLiteral("Middle drag only"), QStringLiteral("middle"));
    const int navigationIndex = m_navigationMode->findData(
        settings.value(QStringLiteral("navigation/mode"), QStringLiteral("middleSpace")));
    if (navigationIndex >= 0) {
        m_navigationMode->setCurrentIndex(navigationIndex);
    }
    form->addRow(QStringLiteral("Pan"), m_navigationMode);

    m_invertZoom = new QCheckBox(QStringLiteral("Invert wheel zoom"), this);
    m_invertZoom->setChecked(settings.value(QStringLiteral("navigation/invertZoom"), false).toBool());
    form->addRow(m_invertZoom);
    root->addLayout(form);

    auto* shortcutGroup = new QGroupBox(QStringLiteral("Shortcuts"), this);
    auto* shortcutLayout = new QVBoxLayout(shortcutGroup);
    m_shortcutSearch = new QLineEdit(shortcutGroup);
    m_shortcutSearch->setPlaceholderText(QStringLiteral("Search commands…"));
    shortcutLayout->addWidget(m_shortcutSearch);
    m_shortcutTable = new QTableWidget(shortcutGroup);
    m_shortcutTable->setColumnCount(2);
    m_shortcutTable->setHorizontalHeaderLabels({QStringLiteral("Command"), QStringLiteral("Shortcut")});
    m_shortcutTable->horizontalHeader()->setStretchLastSection(true);
    m_shortcutTable->setSelectionMode(QAbstractItemView::SingleSelection);
    shortcutLayout->addWidget(m_shortcutTable, 1);
    auto* resetShortcuts = new QPushButton(QStringLiteral("Reset all shortcuts"), shortcutGroup);
    shortcutLayout->addWidget(resetShortcuts, 0, Qt::AlignRight);
    root->addWidget(shortcutGroup, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    populateShortcutTable();

    connect(m_shortcutSearch, &QLineEdit::textChanged, this, [this](const QString& query) {
        for (int row = 0; row < m_shortcutTable->rowCount(); ++row) {
            const QString command = m_shortcutTable->item(row, 0)->text();
            m_shortcutTable->setRowHidden(row, !query.trimmed().isEmpty()
                                                && !command.contains(query, Qt::CaseInsensitive));
        }
    });
    connect(resetShortcuts, &QPushButton::clicked, this, [this] {
        if (m_shortcutManager) {
            m_shortcutManager->resetToDefaults();
            populateShortcutTable();
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_shortcutManager) {
            try {
                applyShortcutTable();
            } catch (...) {
                return;
            }
        }
        QSettings settings;
        settings.setValue(QStringLiteral("appearance/theme"), m_theme->currentData());
        settings.setValue(QStringLiteral("appearance/darkTheme"),
                          m_theme->currentData().toString() != QStringLiteral("light"));
        settings.setValue(QStringLiteral("navigation/mode"), m_navigationMode->currentData());
        settings.setValue(QStringLiteral("navigation/invertZoom"), m_invertZoom->isChecked());
        settings.sync();
        emit preferencesChanged();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void PreferencesDialog::populateShortcutTable()
{
    if (!m_shortcutTable) {
        return;
    }
    m_shortcutTable->setRowCount(0);
    if (!m_shortcutManager) {
        return;
    }
    for (const ShortcutManager::CommandInfo& command : m_shortcutManager->commands()) {
        const int row = m_shortcutTable->rowCount();
        m_shortcutTable->insertRow(row);
        auto* commandItem = new QTableWidgetItem(command.name);
        commandItem->setData(Qt::UserRole, command.id);
        commandItem->setToolTip(command.id);
        m_shortcutTable->setItem(row, 0, commandItem);
        auto* editor = new QKeySequenceEdit(command.sequence, m_shortcutTable);
        editor->setMaximumSequenceLength(1);
        m_shortcutTable->setCellWidget(row, 1, editor);
    }
}

void PreferencesDialog::applyShortcutTable()
{
    if (!m_shortcutManager) {
        return;
    }
    for (int row = 0; row < m_shortcutTable->rowCount(); ++row) {
        auto* commandItem = m_shortcutTable->item(row, 0);
        auto* editor = qobject_cast<QKeySequenceEdit*>(m_shortcutTable->cellWidget(row, 1));
        if (!commandItem || !editor) {
            continue;
        }
        QString error;
        if (!m_shortcutManager->setShortcut(commandItem->data(Qt::UserRole).toString(),
                                            editor->keySequence(),
                                            &error)) {
            QMessageBox::warning(this, QStringLiteral("Shortcut conflict"), error);
            throw std::runtime_error("shortcut conflict");
        }
    }
}

} // namespace vt
