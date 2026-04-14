#pragma once
// ActionRouter.h — ProctorOps
// Central registry for named application actions.
//
// Feature modules register actions on startup. Actions are dispatched by
// keyboard shortcuts (Ctrl+F/K/L and others), the command palette (Ctrl+K),
// and domain-sensitive context menus. Every action has a stable machine-readable
// ID, a human display name, a category, an optional shortcut, and a handler.
//
// Thread safety: must be used from the main thread only.

#include <QList>
#include <QObject>
#include <QString>
#include <QKeySequence>
#include <functional>

struct RegisteredAction {
    QString      id;           // unique machine-readable ID, e.g. "window.checkin"
    QString      displayName;  // label shown in the command palette
    QString      category;     // grouping label, e.g. "Windows", "Content", "Admin"
    QKeySequence shortcut;     // optional keyboard shortcut
    bool         requiresAuth; // true if the action requires an active session
    std::function<void()> handler;
};

class ActionRouter : public QObject {
    Q_OBJECT

public:
    explicit ActionRouter(QObject* parent = nullptr);

    /// Register an action. If an action with the same id already exists, it is replaced.
    void registerAction(const RegisteredAction& action);

    /// Dispatch an action by id. Returns false if no action with that id is registered.
    bool dispatch(const QString& id);

    /// Return all registered actions (used by the command palette to build its list).
    [[nodiscard]] QList<RegisteredAction> allActions() const;

    /// Return actions whose displayName, id, or category contains the query string
    /// (case-insensitive). Returns all actions when query is empty.
    [[nodiscard]] QList<RegisteredAction> filter(const QString& query) const;

    /// Return a pointer to the action bound to a keyboard shortcut, or nullptr.
    [[nodiscard]] const RegisteredAction* findByShortcut(const QKeySequence& key) const;

signals:
    /// Emitted whenever the registered action list changes.
    void actionsChanged();

private:
    QList<RegisteredAction> m_actions;
};
