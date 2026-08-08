#include "ui/main_window.h"

#include <QCloseEvent>
#include <QAction>
#include <QFileDialog>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

namespace vt {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_controller(new EditorController(this))
{
    setWindowTitle(QStringLiteral("Vector Typography Editor"));
    resize(1280, 820);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    m_canvas = new EditorCanvas(splitter);
    splitter->addWidget(m_canvas);

    auto* sidebar = new QWidget();
    sidebar->setMinimumWidth(350);
    sidebar->setMaximumWidth(440);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(10, 10, 10, 10);
    m_typographyPanel = new TypographyPanel(sidebar);
    m_effectsPanel = new EffectsPanel(sidebar);
    sidebarLayout->addWidget(m_typographyPanel);
    sidebarLayout->addWidget(m_effectsPanel);

    auto* scrollArea = new QScrollArea(splitter);
    scrollArea->setWidget(sidebar);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    splitter->addWidget(scrollArea);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({900, 380});
    setCentralWidget(splitter);

    m_typographyPanel->setFontFamilies(m_controller->fontFamilies());
    updateStylesForFamily(m_controller->document().primaryTextObject().font.family);
    QString presetError;
    m_presetNames = m_controller->presetNames(&presetError);
    if (!presetError.isEmpty()) {
        setStatus(presetError);
    }

    connect(m_controller, &EditorController::sceneChanged, this, [this] {
        const TextObject& object = m_controller->document().primaryTextObject();
        m_canvas->setScene(m_controller->geometry(), object.fill);
    });
    connect(m_controller, &EditorController::documentChanged, this, &MainWindow::refreshUi);
    connect(m_controller, &EditorController::statusMessageChanged, this, &MainWindow::setStatus);
    connect(m_controller, &EditorController::fontsChanged, this, [this](const QStringList& families) {
        m_typographyPanel->setFontFamilies(families);
        updateStylesForFamily(m_controller->document().primaryTextObject().font.family);
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

    createActions();
    createMenus();
    refreshUi();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSave()) {
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::refreshUi()
{
    const TextObject& object = m_controller->document().primaryTextObject();
    m_canvas->setScene(m_controller->geometry(), object.fill);
    m_typographyPanel->refresh(object);
    m_effectsPanel->refresh(object, m_presetNames);
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
    updateStylesForFamily(m_controller->document().primaryTextObject().font.family);
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

void MainWindow::createActions()
{
    auto* newAction = new QAction(QStringLiteral("New Project"), this);
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newProject);
    addAction(newAction);

    auto* openAction = new QAction(QStringLiteral("Open Project…"), this);
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openProject);
    addAction(openAction);

    m_saveAction = new QAction(QStringLiteral("Save Project"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveProject);
    addAction(m_saveAction);

    auto* saveAsAction = new QAction(QStringLiteral("Save Project As…"), this);
    saveAsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveProjectAs);
    addAction(saveAsAction);

    auto* exportAction = new QAction(QStringLiteral("Export SVG…"), this);
    exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+S")));
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportSvg);
    addAction(exportAction);

    auto* fitAction = new QAction(QStringLiteral("Fit Content"), this);
    fitAction->setShortcut(Qt::Key_F);
    connect(fitAction, &QAction::triggered, m_canvas, &EditorCanvas::fitContent);
    addAction(fitAction);

    auto* refreshFontsAction = new QAction(QStringLiteral("Refresh Fonts"), this);
    refreshFontsAction->setShortcut(Qt::Key_F5);
    connect(refreshFontsAction, &QAction::triggered, this, &MainWindow::refreshFonts);
    addAction(refreshFontsAction);

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
    editMenu->addAction(m_controller->undoStack()->createUndoAction(this, QStringLiteral("Undo")));
    editMenu->addAction(m_controller->undoStack()->createRedoAction(this, QStringLiteral("Redo")));

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    for (QAction* action : actions()) {
        if (action->text() == QStringLiteral("Fit Content")) {
            viewMenu->addAction(action);
        }
    }

    QMenu* toolsMenu = menuBar()->addMenu(QStringLiteral("Tools"));
    for (QAction* action : actions()) {
        if (action->text() == QStringLiteral("Refresh Fonts")) {
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
