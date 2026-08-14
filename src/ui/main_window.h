#pragma once

#include "ui/editor_canvas.h"
#include "ui/editor_controller.h"
#include "ui/collapsible_section.h"
#include "ui/deformation_panel.h"
#include "ui/effects_panel.h"
#include "ui/layers_panel.h"
#include "ui/preferences_dialog.h"
#include "ui/shortcut_manager.h"
#include "ui/tool_palette.h"
#include "ui/transform_panel.h"
#include "ui/typography_panel.h"
#include "ui/style_gallery.h"
#include "ui/slider_spin_box.h"

#include <QAction>
#include <QHash>
#include <QCloseEvent>
#include <QMainWindow>
#include <QTabBar>
#include <QVector>

class QPushButton;
class QComboBox;

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
    void copyForWord();
    void showPreferences();
    void pageTabChanged(int index);
    void renameCurrentPage();
    void setPageSize();

private:
    void createActions();
    void createMenus();
    void setStatus(const QString& message);
    [[nodiscard]] bool maybeSave();
    void applyTheme();
    void applyNavigationSettings();
    [[nodiscard]] bool selectedEffectSupportsMask() const;
    void refreshEffectMaskCapability();
    void refreshPathCapability();
    void setGlobalEditorShortcutsEnabled(bool enabled);
    void handleTextEditingChanged(bool editing);
    void addTextAtPageCenter();
    [[nodiscard]] ExportScope outputScope() const;

    EditorController* m_controller = nullptr;
    EditorCanvas* m_canvas = nullptr;
    TypographyPanel* m_typographyPanel = nullptr;
    TransformPanel* m_transformPanel = nullptr;
    EffectsPanel* m_effectsPanel = nullptr;
    StyleGallery* m_styleGallery = nullptr;
    SliderSpinBox* m_styleIntensity = nullptr;
    DeformationPanel* m_deformationPanel = nullptr;
    ToolPalette* m_toolPalette = nullptr;
    LayersPanel* m_layersPanel = nullptr;
    QTabBar* m_pageTabs = nullptr;
    ShortcutManager* m_shortcutManager = nullptr;
    QStringList m_presetNames;
    QString m_projectPath;
    QAction* m_saveAction = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_cutAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_selectAllAction = nullptr;
    QAction* m_copyForWordAction = nullptr;
    QComboBox* m_outputScope = nullptr;
    QPushButton* m_addTextButton = nullptr;
    QVector<QAction*> m_toolActions;
    QHash<QString, QAction*> m_actions;
    QString m_newTextEditObjectId;
    bool m_newTextEditTouched = false;
};

} // namespace vt
