#pragma once

#include "ui/editor_canvas.h"
#include "ui/editor_controller.h"
#include "ui/effects_panel.h"
#include "ui/typography_panel.h"

#include <QAction>
#include <QCloseEvent>
#include <QMainWindow>

namespace vt {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void refreshUi();
    void refreshFonts();
    void updateStylesForFamily(const QString& family);
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void exportSvg();

private:
    void createActions();
    void createMenus();
    void setStatus(const QString& message);
    [[nodiscard]] bool maybeSave();

    EditorController* m_controller = nullptr;
    EditorCanvas* m_canvas = nullptr;
    TypographyPanel* m_typographyPanel = nullptr;
    EffectsPanel* m_effectsPanel = nullptr;
    QStringList m_presetNames;
    QString m_projectPath;
    QAction* m_saveAction = nullptr;
};

} // namespace vt
