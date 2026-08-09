#include "ui/main_window.h"

#include <QAction>
#include <QCloseEvent>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace vt {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_controller(new EditorController(this))
    , m_shortcutManager(new ShortcutManager(this))
{
    setWindowTitle(QStringLiteral("Vector Typography Editor"));
    resize(1440, 900);
    const QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget { background: #272a30; color: #e8eaf0; }"
        "QGroupBox { border: 1px solid #454a54; border-radius: 4px; margin-top: 8px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
        "QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QSpinBox, QDoubleSpinBox, QListWidget {"
        " background: #1e2126; color: #f1f3f7; border: 1px solid #4b515c; border-radius: 3px; padding: 3px; }"
        "QPushButton, QToolButton { background: #343943; border: 1px solid #515866; border-radius: 3px; padding: 4px; }"
        "QPushButton:hover, QToolButton:hover { background: #414957; }"
        "QToolButton:checked { background: #2e76ba; border-color: #73b8f0; }"
        "QTabBar::tab { background: #343943; padding: 5px 10px; }"
        "QTabBar::tab:selected { background: #2e76ba; }"));

    auto* workspace = new QWidget(this);
    auto* workspaceLayout = new QHBoxLayout(workspace);
    workspaceLayout->setContentsMargins(6, 6, 6, 6);
    workspaceLayout->setSpacing(6);

    m_toolPalette = new ToolPalette(workspace);
    workspaceLayout->addWidget(m_toolPalette, 0);

    auto* splitter = new QSplitter(Qt::Horizontal, workspace);
    m_canvas = new EditorCanvas(splitter);
    splitter->addWidget(m_canvas);

    auto* sidebar = new QWidget();
    sidebar->setMinimumWidth(360);
    sidebar->setMaximumWidth(500);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(8, 8, 8, 8);
    sidebarLayout->setSpacing(8);

    auto* pageHeader = new QHBoxLayout();
    m_pageTabs = new QTabBar(sidebar);
    m_pageTabs->setExpanding(false);
    m_pageTabs->setUsesScrollButtons(true);
    auto* addPageButton = new QToolButton(sidebar);
    addPageButton->setText(QStringLiteral("+"));
    auto* duplicatePageButton = new QToolButton(sidebar);
    duplicatePageButton->setText(QStringLiteral("⧉"));
    auto* removePageButton = new QToolButton(sidebar);
    removePageButton->setText(QStringLiteral("−"));
    pageHeader->addWidget(m_pageTabs, 1);
    pageHeader->addWidget(addPageButton);
    pageHeader->addWidget(duplicatePageButton);
    pageHeader->addWidget(removePageButton);
    sidebarLayout->addLayout(pageHeader);

    auto* layersGroup = new QGroupBox(QStringLiteral("Layers"), sidebar);
    auto* layersLayout = new QVBoxLayout(layersGroup);
    m_layersPanel = new LayersPanel(layersGroup);
    layersLayout->addWidget(m_layersPanel);
    sidebarLayout->addWidget(layersGroup);

    auto* inspector = new QWidget(sidebar);
    auto* inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    m_typographyPanel = new TypographyPanel(inspector);
    m_effectsPanel = new EffectsPanel(inspector);
    m_deformationPanel = new DeformationPanel(inspector);
    inspectorLayout->addWidget(m_typographyPanel);
    inspectorLayout->addWidget(m_effectsPanel);
    inspectorLayout->addWidget(m_deformationPanel);
    inspectorLayout->addStretch(1);
    auto* scrollArea = new QScrollArea(sidebar);
    scrollArea->setWidget(inspector);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sidebarLayout->addWidget(scrollArea, 1);

    splitter->addWidget(sidebar);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({1000, 420});
    workspaceLayout->addWidget(splitter, 1);
    setCentralWidget(workspace);

    connect(addPageButton, &QToolButton::clicked, m_controller, &EditorController::addPage);
    connect(duplicatePageButton, &QToolButton::clicked,
            m_controller, &EditorController::duplicateCurrentPage);
    connect(removePageButton, &QToolButton::clicked,
            m_controller, &EditorController::removeCurrentPage);
    connect(m_pageTabs, &QTabBar::currentChanged, this, &MainWindow::pageTabChanged);

    connect(m_toolPalette, &ToolPalette::toolSelected, m_canvas, &EditorCanvas::setTool);
    connect(m_controller, &EditorController::sceneChanged, this, [this] {
        m_canvas->setScene(m_controller->sceneGeometry(), m_controller->selectedObjectIds());
    });
    connect(m_controller, &EditorController::documentChanged, this, &MainWindow::refreshUi);
    connect(m_controller, &EditorController::statusMessageChanged, this, &MainWindow::setStatus);
    connect(m_controller, &EditorController::fontsChanged, this, [this](const QStringList& families) {
        m_typographyPanel->setFontFamilies(families);
        updateStylesForFamily(m_controller->activeObject()->font.family);
    });
    connect(m_controller->selectionModel(), &SelectionModel::selectionChanged,
            this, [this] { m_canvas->setSelection(m_controller->selectedObjectIds()); });

    connect(m_canvas, &EditorCanvas::objectClicked, this, [this](const QString& id, bool additive) {
        if (additive) {
            m_controller->toggleObjectSelection(id);
        } else {
            m_controller->selectObject(id);
        }
    });
    connect(m_canvas, &EditorCanvas::marqueeSelectionRequested,
            m_controller, &EditorController::selectObjectsInRect);
    connect(m_canvas, &EditorCanvas::moveCommitted, this,
            [this](const QStringList&, const QPointF& delta) { m_controller->moveSelectedObjects(delta); });
    connect(m_canvas, &EditorCanvas::nudgeRequested,
            m_controller, &EditorController::nudgeSelectedObjects);
    connect(m_canvas, &EditorCanvas::deleteRequested,
            m_controller, &EditorController::deleteSelectedObjects);
    connect(m_canvas, &EditorCanvas::duplicateRequested,
            m_controller, &EditorController::duplicateSelectedObjects);
    connect(m_canvas, &EditorCanvas::textCreateRequested, this, [this](const QPointF& position) {
        m_controller->createTextObject(position, QStringLiteral("Text"));
        m_canvas->setTool(EditorTool::Select);
    });
    connect(m_canvas, &EditorCanvas::textEditRequested, this, [this](const QString&) {
        m_typographyPanel->setFocus();
    });

    connect(m_typographyPanel, &TypographyPanel::textChangedByUser,
            m_controller, &EditorController::setText);
    connect(m_typographyPanel, &TypographyPanel::fontFamilyChanged, this, [this](const QString& family) {
        m_controller->setFontFamily(family);
        updateStylesForFamily(family);
    });
    connect(m_typographyPanel, &TypographyPanel::fontStyleChanged,
            m_controller, &EditorController::setFontStyle);
    connect(m_typographyPanel, &TypographyPanel::fontWeightChanged,
            m_controller, &EditorController::setFontWeight);
    connect(m_typographyPanel, &TypographyPanel::fontSizeChanged,
            m_controller, &EditorController::setFontSize);
    connect(m_typographyPanel, &TypographyPanel::trackingChanged,
            m_controller, &EditorController::setTracking);
    connect(m_typographyPanel, &TypographyPanel::fillColorChanged,
            m_controller, &EditorController::setFillColor);
    connect(m_typographyPanel, &TypographyPanel::refreshFontsRequested,
            m_controller, &EditorController::refreshFonts);

    connect(m_effectsPanel, &EffectsPanel::addEffectRequested,
            m_controller, &EditorController::addEffect);
    connect(m_effectsPanel, &EffectsPanel::removeEffectRequested,
            m_controller, &EditorController::removeEffect);
    connect(m_effectsPanel, &EffectsPanel::moveEffectRequested,
            m_controller, &EditorController::moveEffect);
    connect(m_effectsPanel, &EffectsPanel::effectEnabledChanged,
            m_controller, &EditorController::setEffectEnabled);
    connect(m_effectsPanel, &EffectsPanel::effectParameterChanged,
            m_controller, &EditorController::setEffectParameter);
    connect(m_effectsPanel, &EffectsPanel::effectMasterStrengthChanged,
            m_controller, &EditorController::setEffectMasterStrength);
    connect(m_effectsPanel, &EffectsPanel::savePresetRequested, this, [this](const QString& name) {
        QString error;
        if (!m_controller->savePreset(name, &error)) {
            QMessageBox::warning(this, QStringLiteral("Save preset"), error);
        } else {
            m_presetNames = m_controller->presetNames(&error);
        }
        if (!error.isEmpty()) {
            setStatus(error);
        }
        refreshUi();
    });
    connect(m_effectsPanel, &EffectsPanel::applyPresetRequested, this, [this](const QString& name) {
        QString error;
        if (!m_controller->applyPreset(name, &error)) {
            QMessageBox::warning(this, QStringLiteral("Apply preset"), error);
        }
    });
    connect(m_effectsPanel, &EffectsPanel::deletePresetRequested, this, [this](const QString& name) {
        QString error;
        if (!m_controller->deletePreset(name, &error)) {
            QMessageBox::warning(this, QStringLiteral("Delete preset"), error);
        } else {
            m_presetNames = m_controller->presetNames(&error);
        }
        if (!error.isEmpty()) {
            setStatus(error);
        }
        refreshUi();
    });

    connect(m_deformationPanel, &DeformationPanel::toolChanged,
            m_canvas, &EditorCanvas::setTool);
    connect(m_deformationPanel, &DeformationPanel::brushSettingsChanged,
            m_canvas, &EditorCanvas::setBrushSettings);
    connect(m_deformationPanel, &DeformationPanel::enabledChanged,
            m_controller, &EditorController::setDeformationEnabled);
    connect(m_deformationPanel, &DeformationPanel::overallStrengthChanged,
            m_controller, &EditorController::setDeformationStrength);
    connect(m_deformationPanel, &DeformationPanel::clearRequested,
            m_controller, &EditorController::clearDeformation);
    connect(m_canvas, &EditorCanvas::deformationPreviewChanged,
            m_controller, &EditorController::setDeformationPreview);
    connect(m_canvas, &EditorCanvas::deformationPreviewCleared,
            m_controller, &EditorController::clearDeformationPreview);
    connect(m_canvas, &EditorCanvas::deformationStrokeReady,
            m_controller, &EditorController::addDeformationStroke);

    connect(m_layersPanel, &LayersPanel::layerSelected,
            m_controller, &EditorController::switchLayer);
    connect(m_layersPanel, &LayersPanel::addLayerRequested,
            m_controller, &EditorController::addLayer);
    connect(m_layersPanel, &LayersPanel::removeLayerRequested,
            m_controller, &EditorController::removeActiveLayer);
    connect(m_layersPanel, &LayersPanel::renameLayerRequested,
            m_controller, &EditorController::renameActiveLayer);
    connect(m_layersPanel, &LayersPanel::visibilityToggled,
            m_controller, &EditorController::setActiveLayerVisible);
    connect(m_layersPanel, &LayersPanel::lockToggled,
            m_controller, &EditorController::setActiveLayerLocked);

    m_typographyPanel->setFontFamilies(m_controller->fontFamilies());
    updateStylesForFamily(m_controller->activeObject()->font.family);
    QString presetError;
    m_presetNames = m_controller->presetNames(&presetError);
    if (!presetError.isEmpty()) {
        setStatus(presetError);
    }

    createActions();
    createMenus();
    refreshUi();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSave()) {
        QSettings settings;
        settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::refreshUi()
{
    const TextObject* object = m_controller->activeObject();
    if (!object) {
        return;
    }
    m_canvas->setScene(m_controller->sceneGeometry(), m_controller->selectedObjectIds());
    m_typographyPanel->refresh(*object);
    m_effectsPanel->refresh(*object, m_presetNames);
    m_deformationPanel->refresh(object->deformation);

    {
        const QSignalBlocker blocker(m_pageTabs);
        while (m_pageTabs->count() > 0) {
            m_pageTabs->removeTab(m_pageTabs->count() - 1);
        }
        for (const auto& page : m_controller->document().pages) {
            if (page) {
                const int tab = m_pageTabs->addTab(page->name);
                m_pageTabs->setTabData(tab, page->id);
                if (page->id == m_controller->document().currentPageId) {
                    m_pageTabs->setCurrentIndex(tab);
                }
            }
        }
    }
    if (const Page* page = m_controller->document().currentPage()) {
        m_layersPanel->refresh(*page, m_controller->document().activeLayerId);
    }
    if (m_saveAction) {
        m_saveAction->setEnabled(true);
    }
    setWindowTitle(QStringLiteral("%1%2 — Vector Typography Editor")
                       .arg(m_controller->document().title,
                            m_controller->isModified() ? QStringLiteral("* ") : QString()));
}

void MainWindow::refreshFonts()
{
    m_controller->refreshFonts();
}

void MainWindow::updateStylesForFamily(const QString& family)
{
    m_typographyPanel->setFontStyles(m_controller->fontStyles(family));
}

void MainWindow::newProject()
{
    if (!maybeSave()) {
        return;
    }
    m_projectPath.clear();
    m_controller->newDocument();
}

void MainWindow::openProject()
{
    if (!maybeSave()) {
        return;
    }
    const QString filePath = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open project"), QString(), QStringLiteral("Vector Typography Project (*.vtproj *.json)"));
    if (filePath.isEmpty()) {
        return;
    }
    QString error;
    if (!m_controller->openProject(filePath, &error)) {
        QMessageBox::critical(this, QStringLiteral("Open project"), error);
        return;
    }
    m_projectPath = filePath;
    updateStylesForFamily(m_controller->activeObject()->font.family);
}

void MainWindow::saveProject()
{
    if (m_projectPath.isEmpty()) {
        saveProjectAs();
        return;
    }
    QString error;
    if (!m_controller->saveProject(m_projectPath, &error)) {
        QMessageBox::critical(this, QStringLiteral("Save project"), error);
    }
    refreshUi();
}

void MainWindow::saveProjectAs()
{
    const QString filePath = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save project as"), QString(), QStringLiteral("Vector Typography Project (*.vtproj)"));
    if (filePath.isEmpty()) {
        return;
    }
    QString selectedPath = filePath;
    if (!selectedPath.endsWith(QStringLiteral(".vtproj"), Qt::CaseInsensitive)) {
        selectedPath += QStringLiteral(".vtproj");
    }
    QString error;
    if (!m_controller->saveProject(selectedPath, &error)) {
        QMessageBox::critical(this, QStringLiteral("Save project"), error);
        return;
    }
    m_projectPath = selectedPath;
    refreshUi();
}

void MainWindow::exportSvg()
{
    const QString filePath = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export SVG"), QString(), QStringLiteral("SVG vector file (*.svg)"));
    if (filePath.isEmpty()) {
        return;
    }
    QString selectedPath = filePath;
    if (!selectedPath.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
        selectedPath += QStringLiteral(".svg");
    }
    QString error;
    if (!m_controller->exportSvg(selectedPath, &error)) {
        QMessageBox::critical(this, QStringLiteral("Export SVG"), error);
        return;
    }
    setStatus(QStringLiteral("SVG exported: %1").arg(selectedPath));
}

void MainWindow::showPreferences()
{
    PreferencesDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        setStatus(QStringLiteral("Preferences saved. Restart the editor to apply all workspace settings."));
    }
}

void MainWindow::pageTabChanged(int index)
{
    if (index < 0) {
        return;
    }
    m_controller->switchPage(m_pageTabs->tabData(index).toString());
}

void MainWindow::createActions()
{
    auto registerAction = [this](QAction* action, const QString& id, const QKeySequence& sequence) {
        addAction(action);
        m_shortcutManager->registerAction(id, action, sequence);
    };

    auto* newAction = new QAction(QStringLiteral("New Project"), this);
    connect(newAction, &QAction::triggered, this, &MainWindow::newProject);
    registerAction(newAction, QStringLiteral("file.new"), QKeySequence::New);

    auto* openAction = new QAction(QStringLiteral("Open Project…"), this);
    connect(openAction, &QAction::triggered, this, &MainWindow::openProject);
    registerAction(openAction, QStringLiteral("file.open"), QKeySequence::Open);

    m_saveAction = new QAction(QStringLiteral("Save Project"), this);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveProject);
    registerAction(m_saveAction, QStringLiteral("file.save"), QKeySequence::Save);

    auto* saveAsAction = new QAction(QStringLiteral("Save Project As…"), this);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveProjectAs);
    registerAction(saveAsAction, QStringLiteral("file.saveAs"), QKeySequence(QStringLiteral("Ctrl+Shift+S")));

    auto* exportAction = new QAction(QStringLiteral("Export SVG…"), this);
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportSvg);
    registerAction(exportAction, QStringLiteral("file.exportSvg"), QKeySequence(QStringLiteral("Ctrl+Alt+S")));

    auto* undoAction = m_controller->undoStack()->createUndoAction(this, QStringLiteral("Undo"));
    auto* redoAction = m_controller->undoStack()->createRedoAction(this, QStringLiteral("Redo"));
    registerAction(undoAction, QStringLiteral("edit.undo"), QKeySequence::Undo);
    registerAction(redoAction, QStringLiteral("edit.redo"), QKeySequence::Redo);

    auto* deleteAction = new QAction(QStringLiteral("Delete Selected Objects"), this);
    connect(deleteAction, &QAction::triggered, m_controller, &EditorController::deleteSelectedObjects);
    registerAction(deleteAction, QStringLiteral("edit.delete"), QKeySequence::Delete);

    auto* duplicateAction = new QAction(QStringLiteral("Duplicate Selected Objects"), this);
    connect(duplicateAction, &QAction::triggered,
            m_controller, &EditorController::duplicateSelectedObjects);
    registerAction(duplicateAction, QStringLiteral("edit.duplicate"), QKeySequence(QStringLiteral("Ctrl+D")));

    auto* selectAction = new QAction(QStringLiteral("Select Tool"), this);
    connect(selectAction, &QAction::triggered, this, [this] { m_canvas->setTool(EditorTool::Select); });
    registerAction(selectAction, QStringLiteral("tool.select"), QKeySequence(QStringLiteral("V")));
    auto* moveAction = new QAction(QStringLiteral("Move Tool"), this);
    connect(moveAction, &QAction::triggered, this, [this] { m_canvas->setTool(EditorTool::Move); });
    registerAction(moveAction, QStringLiteral("tool.move"), QKeySequence(QStringLiteral("M")));
    auto* textAction = new QAction(QStringLiteral("Text Tool"), this);
    connect(textAction, &QAction::triggered, this, [this] { m_canvas->setTool(EditorTool::Text); });
    registerAction(textAction, QStringLiteral("tool.text"), QKeySequence(QStringLiteral("T")));

    auto* fitAction = new QAction(QStringLiteral("Fit Content"), this);
    connect(fitAction, &QAction::triggered, m_canvas, &EditorCanvas::fitContent);
    registerAction(fitAction, QStringLiteral("view.fit"), QKeySequence(QStringLiteral("F")));
    auto* refreshFontsAction = new QAction(QStringLiteral("Refresh Fonts"), this);
    connect(refreshFontsAction, &QAction::triggered, this, &MainWindow::refreshFonts);
    registerAction(refreshFontsAction, QStringLiteral("tools.refreshFonts"), QKeySequence(Qt::Key_F5));

    auto* preferencesAction = new QAction(QStringLiteral("Preferences…"), this);
    connect(preferencesAction, &QAction::triggered, this, &MainWindow::showPreferences);
    addAction(preferencesAction);

    auto* fileToolBar = addToolBar(QStringLiteral("File"));
    fileToolBar->addAction(newAction);
    fileToolBar->addAction(openAction);
    fileToolBar->addAction(m_saveAction);
    fileToolBar->addAction(exportAction);
    fileToolBar->addSeparator();
    fileToolBar->addAction(fitAction);
}

void MainWindow::createMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    for (QAction* action : actions()) {
        if (action->text().startsWith(QStringLiteral("New"))
            || action->text().startsWith(QStringLiteral("Open"))
            || action == m_saveAction
            || action->text().startsWith(QStringLiteral("Save Project As"))
            || action->text().startsWith(QStringLiteral("Export"))) {
            fileMenu->addAction(action);
        }
    }

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("Edit"));
    for (QAction* action : actions()) {
        if (action->text() == QStringLiteral("Undo")
            || action->text() == QStringLiteral("Redo")
            || action->text().startsWith(QStringLiteral("Delete"))
            || action->text().startsWith(QStringLiteral("Duplicate"))) {
            editMenu->addAction(action);
        }
    }

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    for (QAction* action : actions()) {
        if (action->text() == QStringLiteral("Fit Content")) {
            viewMenu->addAction(action);
        }
    }
    QMenu* toolsMenu = menuBar()->addMenu(QStringLiteral("Tools"));
    for (QAction* action : actions()) {
        if (action->text() == QStringLiteral("Refresh Fonts")
            || action->text().contains(QStringLiteral("Tool"))) {
            toolsMenu->addAction(action);
        }
    }
    toolsMenu->addSeparator();
    for (QAction* action : actions()) {
        if (action->text().startsWith(QStringLiteral("Preferences"))) {
            toolsMenu->addAction(action);
        }
    }
}

void MainWindow::setStatus(const QString& message)
{
    statusBar()->showMessage(message, 5000);
}

bool MainWindow::maybeSave()
{
    if (!m_controller->isModified()) {
        return true;
    }
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this,
        QStringLiteral("Unsaved changes"),
        QStringLiteral("The project has unsaved changes. Save before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Cancel) {
        return false;
    }
    if (choice == QMessageBox::Save) {
        saveProject();
        return !m_controller->isModified();
    }
    return true;
}

} // namespace vt
