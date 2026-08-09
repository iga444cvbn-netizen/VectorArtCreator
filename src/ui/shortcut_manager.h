#pragma once

#include <QKeySequence>
#include <QObject>
#include <QHash>
#include <QStringList>
#include <QVector>

class QAction;

namespace vt {

class ShortcutManager final : public QObject {
    Q_OBJECT

public:
    struct CommandInfo {
        QString id;
        QString name;
        QKeySequence defaultSequence;
        QKeySequence sequence;
    };

    explicit ShortcutManager(QObject* parent = nullptr);

    void registerCommand(const QString& commandId,
                         const QString& displayName,
                         QAction* action,
                         const QKeySequence& defaultSequence);

    void registerAction(const QString& commandId,
                        QAction* action,
                        const QKeySequence& defaultSequence);
    [[nodiscard]] bool setShortcut(const QString& commandId,
                                   const QKeySequence& sequence,
                                   QString* error = nullptr);
    [[nodiscard]] QStringList conflictingCommands(const QKeySequence& sequence,
                                                  const QString& exceptCommand = {}) const;
    [[nodiscard]] QKeySequence shortcut(const QString& commandId) const;
    [[nodiscard]] QKeySequence defaultShortcut(const QString& commandId) const;
    [[nodiscard]] QString displayName(const QString& commandId) const;
    [[nodiscard]] QVector<CommandInfo> commands() const;
    void resetToDefaults();

signals:
    void shortcutsChanged();

private:
    struct Binding {
        QAction* action = nullptr;
        QString name;
        QKeySequence defaultSequence;
        QKeySequence sequence;
    };

    QHash<QString, Binding> m_bindings;
};

} // namespace vt
