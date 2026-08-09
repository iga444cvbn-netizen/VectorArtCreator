#include "ui/preferences_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSettings>

namespace vt {

PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Preferences"));
    auto* form = new QFormLayout(this);
    m_darkTheme = new QCheckBox(QStringLiteral("Use dark workspace theme"), this);
    m_navigationMode = new QComboBox(this);
    m_navigationMode->addItem(QStringLiteral("Pan with middle mouse / Space"), QStringLiteral("middleSpace"));
    m_navigationMode->addItem(QStringLiteral("Pan with middle mouse only"), QStringLiteral("middle"));
    const QSettings settings;
    m_darkTheme->setChecked(settings.value(QStringLiteral("appearance/darkTheme"), true).toBool());
    const int navigationIndex = m_navigationMode->findData(
        settings.value(QStringLiteral("navigation/mode"), QStringLiteral("middleSpace")));
    if (navigationIndex >= 0) {
        m_navigationMode->setCurrentIndex(navigationIndex);
    }
    form->addRow(m_darkTheme);
    form->addRow(QStringLiteral("Navigation"), m_navigationMode);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QSettings settings;
        settings.setValue(QStringLiteral("appearance/darkTheme"), m_darkTheme->isChecked());
        settings.setValue(QStringLiteral("navigation/mode"), m_navigationMode->currentData());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

} // namespace vt
