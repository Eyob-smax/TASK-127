// TrayManager.cpp — ProctorOps

#include "tray/TrayManager.h"
#include "windows/MainShell.h"
#include <QApplication>
#include <QMenu>
#include <QAction>
#include <QStyle>
#include <QSystemTrayIcon>

TrayManager::TrayManager(MainShell* shell, QObject* parent)
    : QObject(parent)
    , m_shell(shell)
{
    m_trayIcon = new QSystemTrayIcon(this);
    buildMenu();
    updateIcon();
    m_trayIcon->setToolTip(tr("ProctorOps"));

    connect(m_trayIcon, &QSystemTrayIcon::activated,
            this, &TrayManager::onTrayActivated);
}

TrayManager::~TrayManager()
{
    hide();
}

void TrayManager::show()
{
    m_trayIcon->show();
}

void TrayManager::hide()
{
    m_trayIcon->hide();
}

void TrayManager::setTrayState(TrayState state)
{
    m_state = state;
    updateIcon();

    switch (state) {
    case TrayState::Locked:
        m_trayIcon->setToolTip(tr("ProctorOps — Console Locked"));
        m_lockAction->setEnabled(false);
        break;
    case TrayState::KioskActive:
        m_trayIcon->setToolTip(tr("ProctorOps — Kiosk Check-In Mode"));
        m_lockAction->setEnabled(true);
        break;
    case TrayState::Warning:
        m_trayIcon->setToolTip(tr("ProctorOps — Attention Required"));
        m_lockAction->setEnabled(true);
        break;
    case TrayState::Error:
        m_trayIcon->setToolTip(tr("ProctorOps — Error"));
        m_lockAction->setEnabled(true);
        break;
    case TrayState::Normal:
        m_trayIcon->setToolTip(tr("ProctorOps"));
        m_lockAction->setEnabled(true);
        break;
    }
}

void TrayManager::enterKioskMode()
{
    m_kioskMode = true;
    if (m_shell) {
        // Qt::Tool hides the window from the Windows taskbar
        m_shell->setWindowFlag(Qt::Tool, true);
        m_shell->showMinimized();
    }
    m_kioskAction->setText(tr("Exit Kiosk Mode"));
    setTrayState(TrayState::KioskActive);
    emit kioskModeToggled(true);
}

void TrayManager::exitKioskMode()
{
    m_kioskMode = false;
    if (m_shell) {
        m_shell->setWindowFlag(Qt::Tool, false);
        m_shell->showNormal();
        m_shell->raise();
        m_shell->activateWindow();
    }
    m_kioskAction->setText(tr("Enter Kiosk Mode"));
    setTrayState(TrayState::Normal);
    emit kioskModeToggled(false);
}

void TrayManager::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick)
        onRestoreAction();
}

void TrayManager::onRestoreAction()
{
    if (m_kioskMode) {
        exitKioskMode();
    } else if (m_shell) {
        if (m_shell->isLocked() && !m_shell->promptUnlock())
            return;

        m_shell->showNormal();
        m_shell->raise();
        m_shell->activateWindow();
    }
    emit restoreRequested();
}

void TrayManager::onLockAction()
{
    emit lockRequested();
}

void TrayManager::onKioskToggle()
{
    if (m_kioskMode)
        exitKioskMode();
    else
        enterKioskMode();
}

void TrayManager::onExitAction()
{
    emit exitRequested();
}

void TrayManager::buildMenu()
{
    m_menu = new QMenu();

    m_restoreAction = m_menu->addAction(tr("Restore ProctorOps"));
    connect(m_restoreAction, &QAction::triggered, this, &TrayManager::onRestoreAction);

    m_kioskAction = m_menu->addAction(tr("Enter Kiosk Mode"));
    connect(m_kioskAction, &QAction::triggered, this, &TrayManager::onKioskToggle);

    m_menu->addSeparator();

    m_lockAction = m_menu->addAction(tr("Lock Console"));
    connect(m_lockAction, &QAction::triggered, this, &TrayManager::onLockAction);

    m_menu->addSeparator();

    m_exitAction = m_menu->addAction(tr("Exit ProctorOps"));
    connect(m_exitAction, &QAction::triggered, this, &TrayManager::onExitAction);

    m_trayIcon->setContextMenu(m_menu);
}

void TrayManager::updateIcon()
{
    // Use standard Qt style icons as built-in placeholders;
    // replace with branded artwork in the resources build step.
    QStyle::StandardPixmap px;
    switch (m_state) {
    case TrayState::Warning: px = QStyle::SP_MessageBoxWarning;  break;
    case TrayState::Error:   px = QStyle::SP_MessageBoxCritical; break;
    default:                 px = QStyle::SP_ComputerIcon;       break;
    }
    m_trayIcon->setIcon(QApplication::style()->standardIcon(px));
}
