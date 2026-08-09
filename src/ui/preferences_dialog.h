#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;

namespace vt {

class PreferencesDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget* parent = nullptr);

private:
    QCheckBox* m_darkTheme = nullptr;
    QComboBox* m_navigationMode = nullptr;
};

} // namespace vt
