#include "ui/main_window.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>
#include <utility>

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
    applyTheme();

    auto* workspace = new QWidget(this);
    auto* workspaceLayout = new QHBoxLayout(workspace);
    workspaceLayout->setContentsMargins(6, 6, 6, 6);
    workspaceLayout->setSpacing(6);

    m_toolPalette = new ToolPalette(workspace);
    workspaceLayout->addWidget(m_toolPalette, 0);

    auto* splitter = new QSplitter(Qt::Horizontal, workspace);
    auto* canvasColumn = new QWidget(splitter);
    auto* canvasLayout = new QVBoxLayout(canvasColumn);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(4);

    auto* pageHeader = new QHBoxLayout();
    m_pageTabs = new QTabBar(canvasColumn);
    m_pageTabs->setExpanding(false);
    m_pageTabs->setUsesScrollButtons(true);
    m_pageTabs->setMovable(true);
    auto* addPageButton = new QToolButton(canvasColumn);
    auto* duplicatePageButton = new QToolButton(canvasColumn);
    auto* renamePageButton = new QToolButton(canvasColumn);
    auto* sizePageButton = new QToolButton(canvasColumn);
    auto* removePageButton = new QToolButton(canvasColumn);
    addPageButton->setText(QStringLiteral("+"));
    duplicatePageButton->setText(QStringLiteral("⧉"));
    renamePageButton->setText(QStringLiteral("Rename"));
    sizePageButton->setText(QStringLiteral("Size"));
    removePageButton->setText(QStringLiteral("−"));
    addPageButton->setToolTip(QStringLiteral("New page"));
    duplicatePageButton->setToolTip(QStringLiteral("Duplicate page"));
    renamePageButton->setToolTip(QStringLiteral("Rename current page"));
    sizePageButton->setToolTip(QStringLiteral("Set page width and height"));
    removePageButton->setToolTip(QStringLiteral("Delete current page"));
    pageHeader->addWidget(m_pageTabs, 1);
    pageHeader->addWidget(addPageButton);
    pageHeader->addWidget(duplicatePageButton);
    pageHeader->addWidget(renamePageButton);
    pageHeader->addWidget(sizePageButton);
    pageHeader->addWidget(removePageButton);
    canvasLayout->addLayout(pageHeader);

    m_canvas = new EditorCanvas(canvasColumn);
    canvasLayout->addWidget(m_canvas, 1);
    splitter->addWidget(canvasColumn);

    auto* sidebar = new QWidget(splitter);
    sidebar->setMinimumWidth(350);
    sidebar->setMaximumWidth(520);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(4, 4, 4, 4);
    sidebarLayout->setSpacing(4);

    auto* layersGroup = new QGroupBox(QStringLiteral("Layers"), sidebar);
    auto* layersLayout = new QVBoxLayout(layersGroup);
    layersLayout->setContentsMargins(6, 6, 6, 6);
    m_layersPanel = new LayersPanel(layersGroup);
    layersLayout->addWidget(m_layersPanel);
    sidebarLayout->addWidget(layersGroup);

    auto* inspector = new QWidget(sidebar);
    auto* inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    inspectorLayout->setSpacing(2);
    m_transformPanel = new TransformPanel(inspector);
    m_typographyPanel = new TypographyPanel(inspector);
    m_effectsPanel = new EffectsPanel(inspector);
    m_deformationPanel = new DeformationPanel(inspector);
    inspectorLayout->addWidget(new CollapsibleSection(QStringLiteral("Object"),
                                                       QStringLiteral("Object"),
                                                       m_transformPanel,
                                                       true,
                                                       inspector));
    inspectorLayout->addWidget(new CollapsibleSection(QStringLiteral("Typography"),
                                                       QStringLiteral("Typography"),
                                                       m_typographyPanel,
                                                       true,
                                                       inspector));
    inspectorLayout->addWidget(new CollapsibleSection(QStringLiteral("Effects"),
                                                       QStringLiteral("Effects"),
                                                       m_effectsPanel,
                                                       true,
                                                       inspector));
    inspectorLayout->addWidget(new CollapsibleSection(QStringLiteral("Deformation"),
                                                       QStringLiteral("Manual deformation"),
                                                       m_deformationPanel,
                                                       false,
                                                       inspector));
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
    connect(renamePageButton, &QToolButton::clicked, this, &MainWindow::renameCurrentPage);
    connect(sizePageButton, &QToolButton::clicked, this, &MainWindow::setPageSize);
    connect(removePageButton, &QToolButton::clicked,
            m_controller, &EditorController::removeCurrentPage);
    connect(m_pageTabs, &QTabBar::currentChanged, this, &MainWindow::pageTabChanged);
    connect(m_pageTabs, &QTabBar::tabMoved, m_controller, &EditorController::movePage);

    connect(m_toolPalette, &ToolPalette::toolSelected,
            m_controller, &EditorController::setTool);
    connect(m_controller, &EditorController::toolChanged, this, [this](EditorTool tool) {
        m_canvas->setTool(tool);
        m_toolPalette->setActiveTool(tool);
        m_deformationPanel->setTool(tool);
    });
    connect(m_controller, &EditorController::brushSettingsChanged,
            this, [this](BrushMode mode, BrushTarget target, qreal radius, qreal strength, qreal hardness) {
                m_canvas->setBrushSettings(mode, target, radius, strength, hardness);
                m_deformationPanel->setNormalBrushSettings(target, radius, strength, hardness);
            });
    connect(m_controller, &EditorController::maskSettingsChanged,
            this, [this](qreal radius, qreal strength, qreal hardness, bool restore) {
                m_canvas->setMaskBrushSettings(radius, strength, hardness, restore);
                m_deformationPanel->setMaskBrushSettings(radius, strength, hardness, restore);
            });
    connect(m_controller, &EditorController::sceneChanged, this, [this] {
        m_canvas->setScene(m_controller->sceneGeometry(),
                           m_controller->selectedObjectIds(),
                           m_controller->selectionModel()->activeObjectId());
        m_canvas->setMaskTarget(m_controller->selectionModel()->activeObjectId());
        m_canvas->setMaskEnabled(!m_effectsPanel->selectedEffectId().isEmpty());
        m_canvas->setMaskEffectId(m_effectsPanel->selectedEffectId());
    });
    connect(m_controller, &EditorController::documentChanged, this, [this] {
        // Commands mutate the document synchronously while scene evaluation is
        // asynchronous.  End an invalid native session before a stale scene
        // can briefly keep accepting input for a hidden/locked/old-page item.
        if (m_canvas->isTextEditing()) {
            const TextObject* active = m_controller->activeObject();
            const bool samePage = m_canvas->editingPageId() == m_controller->document().currentPageId;
            if (!active || active->id != m_canvas->editingObjectId() || !samePage) {
                m_canvas->finishTextEditing();
            }
        }
        refreshUi();
    });
    connect(m_controller, &EditorController::statusMessageChanged, this, &MainWindow::setStatus);
    connect(m_controller, &EditorController::fontsChanged, this, [this](const QStringList& families) {
        m_typographyPanel->setFontFamilies(families);
        if (const TextObject* object = m_controller->activeObject()) {
            updateStylesForFamily(object->font.family);
        }
    });
    connect(m_controller->selectionModel(), &SelectionModel::selectionChanged, this, [this] {
        m_canvas->setSelection(m_controller->selectedObjectIds(),
                               m_controller->selectionModel()->activeObjectId());
        m_canvas->setMaskTarget(m_controller->selectionModel()->activeObjectId());
        refreshUi();
    });
    connect(m_controller->selectionModel(), &SelectionModel::textRangeChanged,
            this, [this](int start, int end) {
                m_canvas->setTextRange(start, end);
                m_effectsPanel->setTextRange(start, end);
            });

    connect(m_canvas, &EditorCanvas::objectClicked, this,
            [this](const QString& id, bool additive) {
                if (id.isEmpty()) {
                    m_controller->clearSelection();
                } else if (additive) {
                    m_controller->toggleObjectSelection(id);
                } else {
                    m_controller->selectObject(id);
                }
            });
    connect(m_canvas, &EditorCanvas::marqueeSelectionRequested,
            m_controller, &EditorController::selectObjectsInRect);
    connect(m_canvas, &EditorCanvas::moveCommitted, this,
            [this](const QStringList& objectIds, const QPointF& delta) {
                m_controller->moveObjects(objectIds, delta);
            });
    connect(m_canvas, &EditorCanvas::objectTransformCommitted, this,
            [this](const QString& objectId, const ObjectTransform& transform) {
                m_controller->setObjectTransform(objectId, transform);
            });
    connect(m_canvas, &EditorCanvas::nudgeRequested,
            m_controller, &EditorController::nudgeSelectedObjects);
    connect(m_canvas, &EditorCanvas::deleteRequested,
            m_controller, &EditorController::deleteSelectedObjects);
    connect(m_canvas, &EditorCanvas::duplicateRequested,
            m_controller, &EditorController::duplicateSelectedObjects);
    connect(m_canvas, &EditorCanvas::textCreateRequested, this, [this](const QPointF& position) {
        const QString objectId = m_controller->createTextObject(position);
        const TextObject* object = m_controller->activeObject();
        if (object && object->id == objectId) {
            m_newTextEditObjectId = objectId;
            m_newTextEditTouched = false;
            m_canvas->beginTextEditing(objectId,
                                       object->sourceText,
                                       object->font.toQFont(object->typography.fontSize),
                                       QRectF(position, QSizeF(420.0, 130.0)));
        }
    });
    connect(m_canvas, &EditorCanvas::textEditRequested, this, [this](const QString& objectId) {
        m_controller->selectObject(objectId);
        if (const TextObject* object = m_controller->activeObject()) {
            QRectF bounds(QPointF(object->transform.position), QSizeF(420.0, 130.0));
            if (const SceneObjectGeometry* sceneObject = m_controller->sceneGeometry().objectById(objectId)) {
                bounds = sceneObject->visualBounds;
            }
            m_canvas->beginTextEditing(objectId,
                                       object->sourceText,
                                       object->font.toQFont(object->typography.fontSize),
                                       bounds);
        }
    });
    connect(m_canvas, &EditorCanvas::textEdited, this,
            [this](const QString& objectId, const QString& text) {
                if (objectId == m_newTextEditObjectId) {
                    m_newTextEditTouched = true;
                }
                if (m_controller->selectionModel()->activeObjectId() == objectId) {
                    m_controller->setText(text);
                }
            });
    connect(m_canvas, &EditorCanvas::textRangeChanged, this,
            [this](const QString& objectId, int start, int end) {
                if (m_controller->selectionModel()->activeObjectId() == objectId) {
                    m_controller->setTextRange(start, end);
                }
            });
    connect(m_canvas, &EditorCanvas::textEditingChanged,
            this, &MainWindow::handleTextEditingChanged);
    connect(m_canvas, &EditorCanvas::effectMaskStrokeReady, this,
            [this](const QString& objectId, const QString& effectId, const EffectMaskStroke& stroke) {
                if (effectId.isEmpty()) {
                    setStatus(QStringLiteral("Select an effect before painting its mask."));
                    return;
                }
                m_controller->addEffectMaskStroke(objectId, effectId, stroke);
            });
    connect(m_canvas, &EditorCanvas::effectMaskPreviewChanged, this,
            [this](const QString& objectId, const QString& effectId, const EffectMaskStroke& stroke) {
                if (!effectId.isEmpty()) {
                    m_controller->setEffectMaskPreview(objectId, effectId, stroke);
                }
            });
    connect(m_canvas, &EditorCanvas::effectMaskPreviewCleared,
            m_controller, &EditorController::clearEffectMaskPreview);

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
    connect(m_typographyPanel, &TypographyPanel::fontItalicChanged,
            m_controller, &EditorController::setFontItalic);
    connect(m_typographyPanel, &TypographyPanel::fontUnderlineChanged,
            m_controller, &EditorController::setFontUnderline);
    connect(m_typographyPanel, &TypographyPanel::fontStrikeOutChanged,
            m_controller, &EditorController::setFontStrikeOut);
    connect(m_typographyPanel, &TypographyPanel::fontSizeChanged,
            m_controller, &EditorController::setFontSize);
    connect(m_typographyPanel, &TypographyPanel::trackingChanged,
            m_controller, &EditorController::setTracking);
    connect(m_typographyPanel, &TypographyPanel::lineSpacingChanged,
            m_controller, &EditorController::setLineSpacing);
    connect(m_typographyPanel, &TypographyPanel::fillColorChanged,
            m_controller, &EditorController::setFillColor);
    connect(m_typographyPanel, &TypographyPanel::refreshFontsRequested,
            m_controller, &EditorController::refreshFonts);

    connect(m_transformPanel, &TransformPanel::transformChanged, this,
            [this](const ObjectTransform& transform) {
                if (const TextObject* object = m_controller->activeObject()) {
                    m_controller->setObjectTransform(object->id, transform);
                }
            });

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
    connect(m_effectsPanel, &EffectsPanel::effectSelected,
            this, [this](const QString& effectId) {
                m_controller->setSelectedEffectId(effectId);
                m_canvas->setMaskEnabled(!effectId.isEmpty());
                m_canvas->setMaskEffectId(effectId);
            });
    connect(m_controller, &EditorController::selectedEffectChanged, this,
            [this](const QString& effectId) {
                m_effectsPanel->setSelectedEffectId(effectId);
                m_canvas->setMaskEnabled(!effectId.isEmpty());
                m_canvas->setMaskEffectId(effectId);
            });
    connect(m_effectsPanel, &EffectsPanel::effectScopeChanged, this,
            [this](const QString& effectId, const EffectScope& scope) {
                m_controller->setEffectScopeById(effectId, scope);
            });
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

    connect(m_deformationPanel, &DeformationPanel::brushSettingsChanged, this,
            [this](BrushMode, BrushTarget target, qreal radius, qreal strength, qreal hardness) {
                m_controller->setBrushSettings(target, radius, strength, hardness);
            });
    connect(m_deformationPanel, &DeformationPanel::maskSettingsChanged,
            m_controller, &EditorController::setMaskRestoreMode);
    connect(m_deformationPanel, &DeformationPanel::maskBrushSettingsChanged,
            m_controller, &EditorController::setMaskBrushSettings);
    connect(m_deformationPanel, &DeformationPanel::enabledChanged,
            m_controller, &EditorController::setDeformationEnabled);
    connect(m_deformationPanel, &DeformationPanel::overallStrengthChanged,
            m_controller, &EditorController::setDeformationStrength);
    connect(m_deformationPanel, &DeformationPanel::clearRequested,
            m_controller, &EditorController::clearDeformation);
    connect(m_canvas, &EditorCanvas::deformationPreviewChanged,
            m_controller,
            qOverload<const QString&, const DeformationStroke&>(&EditorController::setDeformationPreview));
    connect(m_canvas, &EditorCanvas::deformationPreviewCleared,
            m_controller, &EditorController::clearDeformationPreview);
    connect(m_canvas, &EditorCanvas::deformationStrokeReady,
            m_controller,
            qOverload<const QString&, const DeformationStroke&>(&EditorController::addDeformationStroke));

    connect(m_layersPanel, &LayersPanel::layerSelected,
            m_controller, &EditorController::switchLayer);
    connect(m_layersPanel, &LayersPanel::objectSelected,
            m_controller, [this](const QString& objectId) { m_controller->selectObject(objectId); });
    connect(m_layersPanel, &LayersPanel::addLayerRequested,
            m_controller, &EditorController::addLayer);
    connect(m_layersPanel, &LayersPanel::removeLayerRequested,
            m_controller, &EditorController::removeActiveLayer);
    connect(m_layersPanel, &LayersPanel::moveLayerUpRequested,
            m_controller, &EditorController::moveActiveLayerUp);
    connect(m_layersPanel, &LayersPanel::moveLayerDownRequested,
            m_controller, &EditorController::moveActiveLayerDown);
    connect(m_layersPanel, &LayersPanel::renameLayerRequested,
            m_controller, &EditorController::renameActiveLayer);
    connect(m_layersPanel, &LayersPanel::visibilityToggled,
            m_controller, &EditorController::setActiveLayerVisible);
    connect(m_layersPanel, &LayersPanel::lockToggled,
            m_controller, &EditorController::setActiveLayerLocked);
    connect(m_layersPanel, &LayersPanel::moveObjectRequested,
            m_controller, &EditorController::moveObjectToLayer);

    connect(m_shortcutManager, &ShortcutManager::shortcutsChanged, this, [this] {
        for (auto iterator = m_actions.cbegin(); iterator != m_actions.cend(); ++iterator) {
            if (iterator.value()) {
                iterator.value()->setShortcut(m_shortcutManager->shortcut(iterator.key()));
            }
        }
        const QVector<QPair<EditorTool, QString>> toolCommands = {
            {EditorTool::Select, QStringLiteral("tool.select")},
            {EditorTool::Move, QStringLiteral("tool.move")},
            {EditorTool::Text, QStringLiteral("tool.text")},
            {EditorTool::Push, QStringLiteral("tool.push")},
            {EditorTool::Pull, QStringLiteral("tool.pull")},
            {EditorTool::Inflate, QStringLiteral("tool.inflate")},
            {EditorTool::Pinch, QStringLiteral("tool.pinch")},
            {EditorTool::Smooth, QStringLiteral("tool.smooth")},
            {EditorTool::EffectMask, QStringLiteral("tool.effectMask")},
        };
        for (const auto& [tool, command] : toolCommands) {
            m_toolPalette->setToolShortcut(tool, m_shortcutManager->shortcut(command));
        }
    });

    m_typographyPanel->setFontFamilies(m_controller->fontFamilies());
    QString presetError;
    m_presetNames = m_controller->presetNames(&presetError);
    if (!presetError.isEmpty()) {
        setStatus(presetError);
    }

    createActions();
    createMenus();
    applyNavigationSettings();
    m_controller->setTool(EditorTool::Select);
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
    m_canvas->setScene(m_controller->sceneGeometry(),
                       m_controller->selectedObjectIds(),
                       m_controller->selectionModel()->activeObjectId());
    m_canvas->setTextRange(m_controller->selectionModel()->textRange().first,
                           m_controller->selectionModel()->textRange().second);
    m_canvas->setMaskTarget(m_controller->selectionModel()->activeObjectId());
    m_canvas->setMaskEnabled(!m_effectsPanel->selectedEffectId().isEmpty());
    m_canvas->setMaskEffectId(m_effectsPanel->selectedEffectId());
    m_transformPanel->refresh(object);
    m_typographyPanel->refresh(object);
    m_effectsPanel->refresh(object, m_presetNames);
    if (!object) {
        m_controller->setSelectedEffectId(QString());
    } else if (m_effectsPanel->selectedEffectId() != m_controller->selectedEffectId()) {
        m_controller->setSelectedEffectId(m_effectsPanel->selectedEffectId());
    }
    m_effectsPanel->setTextRange(m_controller->selectionModel()->textRange().first,
                                 m_controller->selectionModel()->textRange().second);
    m_deformationPanel->refresh(object ? &object->deformation : nullptr);
    if (object) {
        updateStylesForFamily(object->font.family);
    }

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
        m_layersPanel->refresh(*page,
                               m_controller->document().activeLayerId,
                               m_controller->selectionModel()->activeObjectId());
    }
    if (m_saveAction) {
        m_saveAction->setEnabled(true);
    }
    setWindowTitle(QStringLiteral("%1%2 — Vector Typography Editor")
                       .arg(!m_controller->document().hasObjects()
                                ? QStringLiteral("Untitled Vector Typography Project")
                                : m_controller->document().title,
                            m_controller->isModified() ? QStringLiteral("* ") : QString()));
    setGlobalEditorShortcutsEnabled(!m_canvas->isTextEditing());
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
    m_canvas->finishTextEditing();
    m_controller->newDocument();
}

void MainWindow::openProject()
{
    if (!maybeSave()) {
        return;
    }
    m_canvas->finishTextEditing();
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
    if (const TextObject* object = m_controller->activeObject()) {
        updateStylesForFamily(object->font.family);
    }
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
    PreferencesDialog dialog(m_shortcutManager, this);
    connect(&dialog, &PreferencesDialog::preferencesChanged, this, [this] {
        applyTheme();
        applyNavigationSettings();
        setStatus(QStringLiteral("Preferences applied."));
    });
    dialog.exec();
}

void MainWindow::pageTabChanged(int index)
{
    if (index >= 0) {
        m_controller->switchPage(m_pageTabs->tabData(index).toString());
    }
}

void MainWindow::renameCurrentPage()
{
    const Page* page = m_controller->document().currentPage();
    if (!page) {
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(this,
                                               QStringLiteral("Rename page"),
                                               QStringLiteral("Name"),
                                               QLineEdit::Normal,
                                               page->name,
                                               &accepted);
    if (accepted) {
        m_controller->renameCurrentPage(name);
    }
}

void MainWindow::setPageSize()
{
    Page* page = m_controller->document().currentPage();
    if (!page) {
        return;
    }
    bool accepted = false;
    const double width = QInputDialog::getDouble(this,
                                                 QStringLiteral("Page width"),
                                                 QStringLiteral("Width"),
                                                 page->size.width(),
                                                 64.0,
                                                 10000.0,
                                                 0,
                                                 &accepted);
    if (!accepted) {
        return;
    }
    const double height = QInputDialog::getDouble(this,
                                                  QStringLiteral("Page height"),
                                                  QStringLiteral("Height"),
                                                  page->size.height(),
                                                  64.0,
                                                  10000.0,
                                                  0,
                                                  &accepted);
    if (accepted) {
        m_controller->setCurrentPageSize(QSizeF(width, height));
    }
}

void MainWindow::createActions()
{
    auto registerAction = [this](const QString& id,
                                 const QString& name,
                                 const QKeySequence& sequence,
                                 const std::function<void()>& callback) {
        auto* action = new QAction(name, this);
        connect(action, &QAction::triggered, this, callback);
        addAction(action);
        m_shortcutManager->registerCommand(id, name, action, sequence);
        m_actions.insert(id, action);
        return action;
    };

    registerAction(QStringLiteral("file.new"), QStringLiteral("New Project"), QKeySequence::New,
                   [this] { newProject(); });
    registerAction(QStringLiteral("file.open"), QStringLiteral("Open Project…"), QKeySequence::Open,
                   [this] { openProject(); });
    m_saveAction = registerAction(QStringLiteral("file.save"), QStringLiteral("Save Project"), QKeySequence::Save,
                                   [this] { saveProject(); });
    registerAction(QStringLiteral("file.saveAs"), QStringLiteral("Save Project As…"),
                   QKeySequence(QStringLiteral("Ctrl+Shift+S")), [this] { saveProjectAs(); });
    registerAction(QStringLiteral("file.exportSvg"), QStringLiteral("Export SVG…"),
                   QKeySequence(QStringLiteral("Ctrl+Alt+S")), [this] { exportSvg(); });

    auto* undoAction = m_controller->undoStack()->createUndoAction(this, QStringLiteral("Undo"));
    auto* redoAction = m_controller->undoStack()->createRedoAction(this, QStringLiteral("Redo"));
    addAction(undoAction);
    addAction(redoAction);
    m_shortcutManager->registerCommand(QStringLiteral("edit.undo"), QStringLiteral("Undo"), undoAction, QKeySequence::Undo);
    m_shortcutManager->registerCommand(QStringLiteral("edit.redo"), QStringLiteral("Redo"), redoAction,
                                        QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
    m_actions.insert(QStringLiteral("edit.undo"), undoAction);
    m_actions.insert(QStringLiteral("edit.redo"), redoAction);

    m_copyAction = registerAction(QStringLiteral("edit.copy"), QStringLiteral("Copy Objects"), QKeySequence::Copy,
                                  [this] { m_controller->copySelectedObjects(); });
    m_cutAction = registerAction(QStringLiteral("edit.cut"), QStringLiteral("Cut Objects"), QKeySequence::Cut,
                                 [this] { m_controller->cutSelectedObjects(); });
    m_pasteAction = registerAction(QStringLiteral("edit.paste"), QStringLiteral("Paste Objects"), QKeySequence::Paste,
                                   [this] { m_controller->pasteObjects(); });
    m_selectAllAction = registerAction(QStringLiteral("edit.selectAll"), QStringLiteral("Select All Objects"),
                                       QKeySequence::SelectAll, [this] { m_controller->selectAllObjects(); });
    registerAction(QStringLiteral("edit.duplicate"), QStringLiteral("Duplicate Objects"),
                   QKeySequence(QStringLiteral("Ctrl+D")), [this] { m_controller->duplicateSelectedObjects(); });
    registerAction(QStringLiteral("edit.delete"), QStringLiteral("Delete Selected Objects"), QKeySequence::Delete,
                   [this] { m_controller->deleteSelectedObjects(); });

    const QVector<QPair<EditorTool, QPair<QString, QString>>> tools = {
        {EditorTool::Select, {QStringLiteral("tool.select"), QStringLiteral("Select Tool")} },
        {EditorTool::Move, {QStringLiteral("tool.move"), QStringLiteral("Move Tool")} },
        {EditorTool::Text, {QStringLiteral("tool.text"), QStringLiteral("Text Tool")} },
        {EditorTool::Push, {QStringLiteral("tool.push"), QStringLiteral("Push Tool")} },
        {EditorTool::Pull, {QStringLiteral("tool.pull"), QStringLiteral("Pull Tool")} },
        {EditorTool::Inflate, {QStringLiteral("tool.inflate"), QStringLiteral("Inflate Tool")} },
        {EditorTool::Pinch, {QStringLiteral("tool.pinch"), QStringLiteral("Pinch Tool")} },
        {EditorTool::Smooth, {QStringLiteral("tool.smooth"), QStringLiteral("Smooth Tool")} },
        {EditorTool::EffectMask, {QStringLiteral("tool.effectMask"), QStringLiteral("Effect Mask Tool")} },
    };
    const QVector<QKeySequence> defaults = {
        QKeySequence(QStringLiteral("V")), QKeySequence(QStringLiteral("M")),
        QKeySequence(QStringLiteral("T")), QKeySequence(QStringLiteral("B")),
        QKeySequence(QStringLiteral("P")), QKeySequence(QStringLiteral("I")),
        QKeySequence(QStringLiteral("N")), QKeySequence(QStringLiteral("S")),
        QKeySequence(QStringLiteral("E"))};
    for (int index = 0; index < tools.size(); ++index) {
        const auto& [tool, command] = tools[index];
        QAction* action = registerAction(command.first,
                                         command.second,
                                         defaults[index],
                                         [this, tool] { m_controller->setTool(tool); });
        m_toolActions.push_back(action);
        m_toolPalette->setToolShortcut(tool, m_shortcutManager->shortcut(command.first));
    }
    registerAction(QStringLiteral("view.fitPage"), QStringLiteral("Fit Page"), QKeySequence(QStringLiteral("F")),
                   [this] { m_canvas->fitContent(); });
    registerAction(QStringLiteral("view.zoomIn"), QStringLiteral("Zoom In"), QKeySequence(QStringLiteral("+")),
                   [this] { m_canvas->zoomIn(); });
    registerAction(QStringLiteral("view.zoomOut"), QStringLiteral("Zoom Out"), QKeySequence(QStringLiteral("-")),
                   [this] { m_canvas->zoomOut(); });
    registerAction(QStringLiteral("view.zoom100"), QStringLiteral("Zoom 100%"), QKeySequence(QStringLiteral("1")),
                   [this] { m_canvas->zoom100(); });
    registerAction(QStringLiteral("page.new"), QStringLiteral("New Page"), QKeySequence(QStringLiteral("Ctrl+Shift+N")),
                   [this] { m_controller->addPage(); });
    registerAction(QStringLiteral("page.next"), QStringLiteral("Next Page"),
                   QKeySequence(QStringLiteral("Ctrl+PageDown")), [this] {
                       const int next = m_pageTabs->currentIndex() + 1;
                       if (next < m_pageTabs->count()) m_pageTabs->setCurrentIndex(next);
                   });
    registerAction(QStringLiteral("page.previous"), QStringLiteral("Previous Page"),
                   QKeySequence(QStringLiteral("Ctrl+PageUp")), [this] {
                       const int previous = m_pageTabs->currentIndex() - 1;
                       if (previous >= 0) m_pageTabs->setCurrentIndex(previous);
                   });
    registerAction(QStringLiteral("layer.new"), QStringLiteral("New Layer"), QKeySequence(QStringLiteral("Ctrl+Shift+L")),
                   [this] { m_controller->addLayer(); });
    registerAction(QStringLiteral("brush.radiusDecrease"), QStringLiteral("Decrease Brush Radius"),
                   QKeySequence(QStringLiteral("[")), [this] {
                       m_controller->setBrushSettings(m_controller->brushTarget(),
                                                      m_controller->brushRadius() / 1.2,
                                                      m_controller->brushStrength(),
                                                      m_controller->brushHardness());
                   });
    registerAction(QStringLiteral("brush.radiusIncrease"), QStringLiteral("Increase Brush Radius"),
                   QKeySequence(QStringLiteral("]")), [this] {
                       m_controller->setBrushSettings(m_controller->brushTarget(),
                                                      m_controller->brushRadius() * 1.2,
                                                      m_controller->brushStrength(),
                                                      m_controller->brushHardness());
                   });
    auto* refreshFontsAction = registerAction(QStringLiteral("tools.refreshFonts"), QStringLiteral("Refresh Fonts"),
                                              QKeySequence(Qt::Key_F5), [this] { refreshFonts(); });
    Q_UNUSED(refreshFontsAction);
    registerAction(QStringLiteral("tools.preferences"), QStringLiteral("Preferences…"), QKeySequence(),
                   [this] { showPreferences(); });

    auto* fileToolBar = addToolBar(QStringLiteral("File"));
    fileToolBar->setMovable(false);
    fileToolBar->addAction(m_actions.value(QStringLiteral("file.new")));
    fileToolBar->addAction(m_actions.value(QStringLiteral("file.open")));
    fileToolBar->addAction(m_saveAction);
    fileToolBar->addAction(m_actions.value(QStringLiteral("file.exportSvg")));
    fileToolBar->addSeparator();
    fileToolBar->addAction(m_actions.value(QStringLiteral("view.fitPage")));
}

void MainWindow::createMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    fileMenu->addAction(m_actions.value(QStringLiteral("file.new")));
    fileMenu->addAction(m_actions.value(QStringLiteral("file.open")));
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_actions.value(QStringLiteral("file.saveAs")));
    fileMenu->addSeparator();
    fileMenu->addAction(m_actions.value(QStringLiteral("file.exportSvg")));

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("Edit"));
    editMenu->addAction(m_actions.value(QStringLiteral("edit.undo")));
    editMenu->addAction(m_actions.value(QStringLiteral("edit.redo")));
    editMenu->addSeparator();
    editMenu->addAction(m_copyAction);
    editMenu->addAction(m_cutAction);
    editMenu->addAction(m_pasteAction);
    editMenu->addAction(m_actions.value(QStringLiteral("edit.duplicate")));
    editMenu->addAction(m_actions.value(QStringLiteral("edit.delete")));
    editMenu->addAction(m_selectAllAction);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    viewMenu->addAction(m_actions.value(QStringLiteral("view.fitPage")));
    viewMenu->addAction(m_actions.value(QStringLiteral("view.zoomIn")));
    viewMenu->addAction(m_actions.value(QStringLiteral("view.zoomOut")));
    viewMenu->addAction(m_actions.value(QStringLiteral("view.zoom100")));

    QMenu* toolsMenu = menuBar()->addMenu(QStringLiteral("Tools"));
    for (const QAction* action : std::as_const(m_toolActions)) {
        toolsMenu->addAction(const_cast<QAction*>(action));
    }
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_actions.value(QStringLiteral("tools.refreshFonts")));
    toolsMenu->addAction(m_actions.value(QStringLiteral("tools.preferences")));
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

void MainWindow::applyTheme()
{
    const QSettings settings;
    const QString theme = settings.value(QStringLiteral("appearance/theme"), QStringLiteral("dark")).toString();
    const bool light = theme == QStringLiteral("light")
        || (theme == QStringLiteral("system") && palette().color(QPalette::Window).lightness() > 160);
    if (light) {
        qApp->setStyleSheet(QStringLiteral(
            "QMainWindow, QWidget { background: #eef1f5; color: #20242b; }"
            "QGroupBox { border: 1px solid #c7ced8; margin-top: 8px; padding-top: 8px; }"
            "QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QListWidget, QTreeWidget {"
            " background: #ffffff; color: #20242b; border: 1px solid #b6bfcc; padding: 3px; }"
            "QSpinBox, QDoubleSpinBox { background: #ffffff; color: #20242b; border: 1px solid #b6bfcc;"
            " padding-left: 3px; padding-right: 22px; }"
            "QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border;"
            " subcontrol-position: top right; width: 19px; border-left: 1px solid #b6bfcc; }"
            "QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border;"
            " subcontrol-position: bottom right; width: 19px; border-left: 1px solid #b6bfcc; }"
            "QPushButton, QToolButton { background: #e3e8ef; border: 1px solid #b6bfcc; padding: 4px; }"
            "QPushButton:hover, QToolButton:hover { background: #d5e4f6; }"
            "QMenu::item:selected { background: #b9d8f4; color: #162233; }"
            "QMenu::item:checked { background: #d5e4f6; color: #162233; }"
            "QMenu::item:disabled { color: #7c8795; } QMenu::separator { height: 1px; background: #b6bfcc; margin: 4px 8px; }"
            "QToolButton:checked { background: #4b91ce; color: white; }"
            "QTabBar::tab { background: #dbe2eb; padding: 5px 10px; }"
            "QTabBar::tab:selected { background: #4b91ce; color: white; }"));
    } else {
        qApp->setStyleSheet(QStringLiteral(
            "QMainWindow, QWidget { background: #272a30; color: #e8eaf0; }"
            "QGroupBox { border: 1px solid #454a54; margin-top: 8px; padding-top: 8px; }"
            "QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QListWidget, QTreeWidget {"
            " background: #1e2126; color: #f1f3f7; border: 1px solid #4b515c; padding: 3px; }"
            "QSpinBox, QDoubleSpinBox { background: #1e2126; color: #f1f3f7; border: 1px solid #4b515c;"
            " padding-left: 3px; padding-right: 22px; }"
            "QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border;"
            " subcontrol-position: top right; width: 19px; border-left: 1px solid #4b515c; }"
            "QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border;"
            " subcontrol-position: bottom right; width: 19px; border-left: 1px solid #4b515c; }"
            "QPushButton, QToolButton { background: #343943; border: 1px solid #515866; padding: 4px; }"
            "QPushButton:hover, QToolButton:hover { background: #414957; }"
            "QMenu::item:selected { background: #2e76ba; color: #ffffff; }"
            "QMenu::item:checked { background: #414957; color: #ffffff; }"
            "QMenu::item:disabled { color: #858b96; } QMenu::separator { height: 1px; background: #515866; margin: 4px 8px; }"
            "QToolButton:checked { background: #2e76ba; border-color: #73b8f0; }"
            "QTabBar::tab { background: #343943; padding: 5px 10px; }"
            "QTabBar::tab:selected { background: #2e76ba; }"));
    }
}

void MainWindow::applyNavigationSettings()
{
    const QSettings settings;
    m_canvas->setNavigationSettings(settings.value(QStringLiteral("navigation/mode"),
                                                    QStringLiteral("middleSpace"))
                                        .toString(),
                                    settings.value(QStringLiteral("navigation/invertZoom"), false).toBool());
}

void MainWindow::handleTextEditingChanged(bool editing)
{
    if (!editing) {
        m_newTextEditObjectId.clear();
        m_newTextEditTouched = false;
    }
    setGlobalEditorShortcutsEnabled(!editing);
}

void MainWindow::setGlobalEditorShortcutsEnabled(bool enabled)
{
    for (QAction* action : std::as_const(m_toolActions)) {
        action->setEnabled(enabled);
    }

    // These actions belong to the editor canvas, not to the in-place text
    // document.  Disabling their application/window shortcuts lets the
    // focused QPlainTextEdit receive ordinary letters and its native editing
    // commands.  File commands remain available while text is being edited.
    const QStringList editorActionIds = {
        QStringLiteral("edit.copy"),
        QStringLiteral("edit.cut"),
        QStringLiteral("edit.paste"),
        QStringLiteral("edit.selectAll"),
        QStringLiteral("edit.duplicate"),
        QStringLiteral("edit.delete"),
        QStringLiteral("edit.undo"),
        QStringLiteral("edit.redo"),
        QStringLiteral("view.fitPage"),
        QStringLiteral("view.zoomIn"),
        QStringLiteral("view.zoomOut"),
        QStringLiteral("view.zoom100"),
        QStringLiteral("page.new"),
        QStringLiteral("page.next"),
        QStringLiteral("page.previous"),
        QStringLiteral("layer.new"),
        QStringLiteral("brush.radiusDecrease"),
        QStringLiteral("brush.radiusIncrease"),
        QStringLiteral("tools.refreshFonts"),
        QStringLiteral("tools.preferences"),
    };
    for (const QString& actionId : editorActionIds) {
        if (QAction* action = m_actions.value(actionId, nullptr)) {
            action->setEnabled(enabled);
        }
    }
}

} // namespace vt
