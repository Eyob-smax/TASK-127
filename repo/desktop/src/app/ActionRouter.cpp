// ActionRouter.cpp — ProctorOps

#include "app/ActionRouter.h"

ActionRouter::ActionRouter(QObject* parent)
    : QObject(parent)
{}

void ActionRouter::registerAction(const RegisteredAction& action)
{
    for (auto& existing : m_actions) {
        if (existing.id == action.id) {
            existing = action;
            emit actionsChanged();
            return;
        }
    }
    m_actions.append(action);
    emit actionsChanged();
}

bool ActionRouter::dispatch(const QString& id)
{
    for (const auto& action : std::as_const(m_actions)) {
        if (action.id == id) {
            if (action.handler) action.handler();
            return true;
        }
    }
    return false;
}

QList<RegisteredAction> ActionRouter::allActions() const
{
    return m_actions;
}

QList<RegisteredAction> ActionRouter::filter(const QString& query) const
{
    if (query.isEmpty()) return m_actions;

    const QString lq = query.toLower();
    QList<RegisteredAction> result;
    for (const auto& action : std::as_const(m_actions)) {
        if (action.displayName.toLower().contains(lq)
         || action.id.toLower().contains(lq)
         || action.category.toLower().contains(lq)) {
            result.append(action);
        }
    }
    return result;
}

const RegisteredAction* ActionRouter::findByShortcut(const QKeySequence& key) const
{
    if (key.isEmpty()) return nullptr;
    for (const auto& action : std::as_const(m_actions)) {
        if (!action.shortcut.isEmpty() && action.shortcut == key)
            return &action;
    }
    return nullptr;
}
