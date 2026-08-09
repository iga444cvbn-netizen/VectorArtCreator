#pragma once

#include <QKeySequence>
#include <QObject>
#include <QHash>
#include <QStringList>

class QAction;

namespace vt {

class ShortcutManager final : public QObject {
    Q_OBJECT

public:
    explicit ShortcutManager(QObject* parent = nullptr);

    void registerAction(const QString& commandId,
                        QAction* action,
                        const QKeySequence& defaultSequence);
    [[nodiscard]] bool setShortcut(const QString& commandId,
                                   const QKeySequence& sequence,
                                   QString* error = nullptr);
    [[nodiscard]] QStringList conflictingCommands(const QKeySequence& sequence,
                                                  const QString& exceptCommand = {}) const;
    [[nodiscard]] QKeySequence shortcut(const QString& commandId) const;
    void resetToDefaults();

signals:
    void shortcutsChanged();

private:
    struct Binding {
        QAction* action = nullptr;
        QKeySequence defaultSequence;
        QKeySequence sequence;
    };

    QHash<QString, Binding> m_bindings;
};

} // namespace vt
