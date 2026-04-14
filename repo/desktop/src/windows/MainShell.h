#pragma once
// MainShell.h — ProctorOps
// Primary workspace controller.
//
// MainShell is a QMainWindow that owns:
//   - A QMdiArea (tabbed view) for side-by-side feature sub-windows
//   - Global keyboard shortcuts: Ctrl+K (command palette), Ctrl+F (filter), Ctrl+L (lock)
//   - Menu bar with File / View / Windows menus
//   - Status bar for transient messages and warning indicators
//   - Domain-sensitive right-click context menu routing
//   - Coordination with TrayManager for lock state and kiosk mode
//   - Workspace state persistence on every open/close transition

#include <QMainWindow>
#include <QString>

class QMdiArea;
class ActionRouter;
class TrayManager;
class CommandPaletteDialog;
class WorkspaceState;
class AppSettings;
struct AppContext;

class MainShell : public QMainWindow {
    Q_OBJECT

public:
    explicit MainShell(ActionRouter& router,
                       WorkspaceState& workspaceState,
                       AppSettings& settings,
                       AppContext* ctx = nullptr,
                       QWidget* parent = nullptr);
    ~MainShell() override;

    void setTrayManager(TrayManager* tray);

    /// Open a named feature sub-window inside the MDI area.
    /// If already open, activates it instead of creating a duplicate.
    void openWindow(const QString& windowId);

    /// Close a named feature sub-window and update workspace state.
    void closeWindow(const QString& windowId);

    /// Lock the console — disables all operator interaction.
    void lockConsole();

    /// Unlock after successful re-authentication.
    void unlockConsole();

    /// Prompt for password and attempt authenticated unlock.
    [[nodiscard]] bool promptUnlock();

    [[nodiscard]] bool isLocked() const { return m_locked; }

    /// Restore all windows listed in WorkspaceState (called after crash detection).
    void restoreWorkspace();

    /// Show or clear the warning indicator in the status bar and tray.
    void setWarningIndicator(bool active, const QString& tooltip = {});

signals:
    void consoleLocked();
    void consoleUnlocked();
    void exitRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private slots:
    void onCommandPalette();
    void onAdvancedFilter();
    void onConsoleLock();

private:
    [[nodiscard]] bool canOpenWindow(const QString& windowId) const;

    void setupMenuBar();
    void setupStatusBar();
    void registerShellActions();
    void buildContextMenu(QMenu* menu);

    ActionRouter&         m_router;
    WorkspaceState&       m_workspaceState;
    AppSettings&          m_settings;
    AppContext*           m_ctx{nullptr};
    TrayManager*          m_tray{nullptr};
    QMdiArea*             m_mdiArea{nullptr};
    CommandPaletteDialog* m_commandPalette{nullptr};
    bool                  m_locked{false};
};
