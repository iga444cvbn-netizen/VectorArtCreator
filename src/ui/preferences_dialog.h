#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QTableWidget;

namespace vt {

class ShortcutManager;

class PreferencesDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(ShortcutManager* shortcutManager = nullptr,
                               QWidget* parent = nullptr);

signals:
    void preferencesChanged();

private:
    void populateShortcutTable();
    void applyShortcutTable();

    QComboBox* m_theme = nullptr;
    QComboBox* m_navigationMode = nullptr;
    QCheckBox* m_invertZoom = nullptr;
    QLineEdit* m_shortcutSearch = nullptr;
    QTableWidget* m_shortcutTable = nullptr;
    ShortcutManager* m_shortcutManager = nullptr;
};

} // namespace vt
