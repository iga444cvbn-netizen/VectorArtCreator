#include "ui/shortcut_manager.h"

#include <QAction>
#include <QSettings>

#include <algorithm>

namespace vt {

ShortcutManager::ShortcutManager(QObject* parent)
    : QObject(parent)
{
}

void ShortcutManager::registerAction(const QString& commandId,
                                     QAction* action,
                                     const QKeySequence& defaultSequence)
{
    registerCommand(commandId,
                    action ? action->text().remove(QLatin1Char('&')) : commandId,
                    action,
                    defaultSequence);
}

void ShortcutManager::registerCommand(const QString& commandId,
                                      const QString& displayName,
                                      QAction* action,
                                      const QKeySequence& defaultSequence)
{
    if (commandId.isEmpty()) {
        return;
    }
    Binding binding;
    binding.action = action;
    binding.name = displayName;
    binding.defaultSequence = defaultSequence;
    const QSettings settings;
    binding.sequence = QKeySequence(settings.value(
        QStringLiteral("shortcuts/%1").arg(commandId), defaultSequence).toString());
    if (action) {
        action->setShortcut(binding.sequence);
    }
    m_bindings.insert(commandId, binding);
}

bool ShortcutManager::setShortcut(const QString& commandId,
                                  const QKeySequence& sequence,
                                  QString* error)
{
    if (!m_bindings.contains(commandId)) {
        if (error) {
            *error = QStringLiteral("Unknown command '%1'.").arg(commandId);
        }
        return false;
    }
    const QStringList conflicts = conflictingCommands(sequence, commandId);
    if (!sequence.isEmpty() && !conflicts.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Shortcut conflicts with: %1").arg(conflicts.join(QStringLiteral(", ")));
        }
        return false;
    }
    Binding& binding = m_bindings[commandId];
    binding.sequence = sequence;
    if (binding.action) {
        binding.action->setShortcut(sequence);
    }
    QSettings settings;
    settings.setValue(QStringLiteral("shortcuts/%1").arg(commandId), sequence.toString());
    settings.sync();
    emit shortcutsChanged();
    return true;
}

QStringList ShortcutManager::conflictingCommands(const QKeySequence& sequence,
                                                 const QString& exceptCommand) const
{
    QStringList conflicts;
    if (sequence.isEmpty()) {
        return conflicts;
    }
    for (auto iterator = m_bindings.cbegin(); iterator != m_bindings.cend(); ++iterator) {
        if (iterator.key() != exceptCommand && iterator.value().sequence == sequence) {
            conflicts.push_back(iterator.key());
        }
    }
    return conflicts;
}

QKeySequence ShortcutManager::shortcut(const QString& commandId) const
{
    return m_bindings.value(commandId).sequence;
}

QKeySequence ShortcutManager::defaultShortcut(const QString& commandId) const
{
    return m_bindings.value(commandId).defaultSequence;
}

QString ShortcutManager::displayName(const QString& commandId) const
{
    return m_bindings.value(commandId).name;
}

QVector<ShortcutManager::CommandInfo> ShortcutManager::commands() const
{
    QVector<CommandInfo> result;
    result.reserve(m_bindings.size());
    for (auto iterator = m_bindings.cbegin(); iterator != m_bindings.cend(); ++iterator) {
        result.push_back({iterator.key(),
                          iterator.value().name,
                          iterator.value().defaultSequence,
                          iterator.value().sequence});
    }
    std::sort(result.begin(), result.end(), [](const CommandInfo& left, const CommandInfo& right) {
        return left.id < right.id;
    });
    return result;
}

void ShortcutManager::resetToDefaults()
{
    QSettings settings;
    for (auto iterator = m_bindings.begin(); iterator != m_bindings.end(); ++iterator) {
        iterator.value().sequence = iterator.value().defaultSequence;
        if (iterator.value().action) {
            iterator.value().action->setShortcut(iterator.value().sequence);
        }
        settings.setValue(QStringLiteral("shortcuts/%1").arg(iterator.key()),
                          iterator.value().sequence.toString());
    }
    settings.sync();
    emit shortcutsChanged();
}

} // namespace vt
