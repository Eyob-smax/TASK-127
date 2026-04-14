#pragma once
// TrayManager.h — ProctorOps
// System tray icon, kiosk-mode check-in desk integration, and operator-safe
// shutdown handling.
//
// TrayState reflects the current operational state: Normal, Locked (console
// lock), KioskActive (hidden from taskbar), Warning (pending corrections /
// alerts), or Error. The tray icon tooltip and icon image update to match.
//
// Kiosk mode: hides the main shell from the taskbar, making it appear as a
// single-purpose check-in station. A tray icon double-click restores it.
// Lock mode: the console is locked and requires re-authentication to unlock.

#include <QObject>
#include <QSystemTrayIcon>

class QMenu;
class QAction;
class MainShell;

class TrayManager : public QObject {
    Q_OBJECT

public:
    enum class TrayState {
        Normal,       // running normally
        Locked,       // console locked — re-authentication required
        KioskActive,  // kiosk / check-in station mode
        Warning,      // attention required (e.g. pending corrections)
        Error         // serious issue requiring operator action
    };
    Q_ENUM(TrayState)

    explicit TrayManager(MainShell* shell, QObject* parent = nullptr);
    ~TrayManager() override;

    /// Show the system tray icon. Call after Application::initialize().
    void show();

    /// Hide and remove the tray icon before application exit.
    void hide();

    void setTrayState(TrayState state);
    [[nodiscard]] TrayState trayState() const { return m_state; }

    /// Enter kiosk mode: minimize and hide the shell from the taskbar.
    void enterKioskMode();

    /// Exit kiosk mode: restore the shell window normally.
    void exitKioskMode();

    [[nodiscard]] bool isKioskMode() const { return m_kioskMode; }

signals:
    void lockRequested();
    void restoreRequested();
    void exitRequested();
    void kioskModeToggled(bool active);

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onRestoreAction();
    void onLockAction();
    void onKioskToggle();
    void onExitAction();

private:
    void buildMenu();
    void updateIcon();

    MainShell*       m_shell{nullptr};
    QSystemTrayIcon* m_trayIcon{nullptr};
    QMenu*           m_menu{nullptr};
    QAction*         m_restoreAction{nullptr};
    QAction*         m_kioskAction{nullptr};
    QAction*         m_lockAction{nullptr};
    QAction*         m_exitAction{nullptr};
    TrayState        m_state{TrayState::Normal};
    bool             m_kioskMode{false};
};
