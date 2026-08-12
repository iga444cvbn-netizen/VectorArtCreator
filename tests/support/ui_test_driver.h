#pragma once

#include <QPointer>
#include <QString>

class QGraphicsProxyWidget;
class QGraphicsView;
class QPlainTextEdit;

namespace vt {
class EditorCanvas;
class EditorController;
class MainWindow;

namespace test {

class UiTestDriver final {
public:
    UiTestDriver();
    ~UiTestDriver();

    [[nodiscard]] MainWindow& window() const;
    [[nodiscard]] EditorController& controller() const;
    [[nodiscard]] EditorCanvas& canvas() const;

    void newProject();
    void clickAddText();
    void selectTool(const QString& stableToolName);
    void clickCanvasAtDocumentPoint(const QPointF& point);
    void typeText(const QString& text);
    void pressKey(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    void forceEditorFocusForSetup();
    void exitTextEditing();
    void selectObject(const QString& id);
    void applyBuiltInStyle(const QString& name);
    void setStyleIntensity(double value);
    void undo();
    void redo();
    void waitForSceneGeneration(const QString& objectId = {}) const;

    [[nodiscard]] QString activeObjectId() const;
    [[nodiscard]] QPlainTextEdit* nativeEditor() const;
    [[nodiscard]] QGraphicsProxyWidget* nativeEditorProxy() const;

    void expectEditing(bool expected = true) const;
    void expectActiveText(const QString& text) const;
    void expectVectorGeometry() const;
    void expectEditorAttachedToActiveObject() const;
    void expectInvariants() const;
    void saveDiagnostic(const QString& name) const;

private:
    QPointer<MainWindow> m_window;
    QPointer<EditorController> m_controller;
    QPointer<EditorCanvas> m_canvas;
};

} // namespace test
} // namespace vt
