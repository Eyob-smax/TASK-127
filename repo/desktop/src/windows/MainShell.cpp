// MainShell.cpp — ProctorOps

#include "windows/MainShell.h"
#include "windows/CheckInWindow.h"
#include "windows/QuestionBankWindow.h"
#include "windows/AuditViewerWindow.h"
#include "windows/SyncWindow.h"
#include "windows/DataSubjectWindow.h"
#include "windows/SecurityAdminWindow.h"
#include "windows/UpdateWindow.h"
#include "windows/IngestionMonitorWindow.h"
#include "app/ActionRouter.h"
#include "app/AppContext.h"
#include "app/AppSettings.h"
#include "app/WorkspaceState.h"
#include "services/AuthService.h"
#include "tray/TrayManager.h"
#include "dialogs/CommandPaletteDialog.h"
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QLabel>
#include <QWidget>
#include <QLineEdit>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QInputDialog>
#include <QMessageBox>

namespace {
bool requiredRoleForWindow(const QString& windowId, Role& requiredRole)
{
    if (windowId == QLatin1String(SecurityAdminWindow::WindowId)
        || windowId == QLatin1String(UpdateWindow::WindowId)
        || windowId == QLatin1String(SyncWindow::WindowId)
        || windowId == QLatin1String(DataSubjectWindow::WindowId)
        || windowId == QLatin1String(AuditViewerWindow::WindowId)) {
        requiredRole = Role::SecurityAdministrator;
        return true;
    }

    if (windowId == QLatin1String(IngestionMonitorWindow::WindowId)) {
        requiredRole = Role::ContentManager;
        return true;
    }

    // QuestionBankWindow is viewable by Proctor and above (create/edit/delete
    // is enforced at the service layer for ContentManager+).
    if (windowId == QLatin1String(QuestionBankWindow::WindowId)) {
        requiredRole = Role::Proctor;
        return true;
    }

    return false;
}
}

MainShell::MainShell(ActionRouter& router,
                     WorkspaceState& workspaceState,
                     AppSettings& settings,
                     AppContext* ctx,
                     QWidget* parent)
    : QMainWindow(parent)
    , m_router(router)
    , m_workspaceState(workspaceState)
    , m_settings(settings)
    , m_ctx(ctx)
{
    setWindowTitle(tr("ProctorOps"));
    setMinimumSize(1024, 640);

    m_mdiArea = new QMdiArea(this);
    m_mdiArea->setViewMode(QMdiArea::TabbedView);
    m_mdiArea->setTabsClosable(true);
    m_mdiArea->setTabsMovable(true);
    m_mdiArea->setDocumentMode(true);
    setCentralWidget(m_mdiArea);

    m_commandPalette = new CommandPaletteDialog(m_router, this);

    setupMenuBar();
    setupStatusBar();
    registerShellActions();

    // Restore window geometry from settings
    const QByteArray geom = m_settings.mainWindowGeometry();
    if (!geom.isEmpty())
        restoreGeometry(geom);
}

MainShell::~MainShell() = default;

void MainShell::setTrayManager(TrayManager* tray)
{
    m_tray = tray;
    if (m_tray) {
        connect(m_tray, &TrayManager::lockRequested,
                this, &MainShell::lockConsole);
        connect(m_tray, &TrayManager::exitRequested,
                this, &MainShell::close);
    }
}

void MainShell::openWindow(const QString& windowId)
{
    if (!canOpenWindow(windowId))
        return;

    // Activate if already open
    for (QMdiSubWindow* sub : m_mdiArea->subWindowList()) {
        if (sub->objectName() == windowId) {
            m_mdiArea->setActiveSubWindow(sub);
            return;
        }
    }

    // Create the appropriate feature window; fall back to a placeholder if unknown
    QWidget* content = nullptr;

    if (m_ctx) {
        if (windowId == QLatin1String(CheckInWindow::WindowId))
            content = new CheckInWindow(*m_ctx);
        else if (windowId == QLatin1String(QuestionBankWindow::WindowId))
            content = new QuestionBankWindow(*m_ctx);
        else if (windowId == QLatin1String(AuditViewerWindow::WindowId))
            content = new AuditViewerWindow(*m_ctx);
        else if (windowId == QLatin1String(SyncWindow::WindowId))
            content = new SyncWindow(*m_ctx);
        else if (windowId == QLatin1String(DataSubjectWindow::WindowId))
            content = new DataSubjectWindow(*m_ctx);
        else if (windowId == QLatin1String(SecurityAdminWindow::WindowId))
            content = new SecurityAdminWindow(*m_ctx);
        else if (windowId == QLatin1String(UpdateWindow::WindowId))
            content = new UpdateWindow(*m_ctx);
        else if (windowId == QLatin1String(IngestionMonitorWindow::WindowId))
            content = new IngestionMonitorWindow(*m_ctx);
    }

    if (!content) {
        content = new QWidget();
        content->setObjectName(windowId);
    }

    auto* subWin = m_mdiArea->addSubWindow(content);
    subWin->setObjectName(windowId);
    subWin->setWindowTitle(content->windowTitle().isEmpty() ? windowId : content->windowTitle());
    subWin->setAttribute(Qt::WA_DeleteOnClose, true);
    subWin->resize(800, 560);
    subWin->show();

    // Track close events to update workspace state
    connect(subWin, &QMdiSubWindow::destroyed, this, [this, windowId] {
        m_workspaceState.markWindowClosed(windowId);
    });

    m_workspaceState.markWindowOpen(windowId);
}

bool MainShell::canOpenWindow(const QString& windowId) const
{
    if (!m_ctx || !m_ctx->authService)
        return true;

    Role requiredRole = Role::FrontDeskOperator;
    if (!requiredRoleForWindow(windowId, requiredRole))
        return true;

    auto authResult = m_ctx->authService->requireRoleForActor(
        m_ctx->session.userId,
        requiredRole);
    if (authResult.isOk())
        return true;

    QMessageBox::warning(const_cast<MainShell*>(this),
                         tr("Access Denied"),
                         tr("You do not have permission to open this window."));
    return false;
}

void MainShell::closeWindow(const QString& windowId)
{
    for (QMdiSubWindow* sub : m_mdiArea->subWindowList()) {
        if (sub->objectName() == windowId) {
            sub->close();
            return;
        }
    }
    // If not found in MDI area, just update state
    m_workspaceState.markWindowClosed(windowId);
}

void MainShell::lockConsole()
{
    if (m_locked) return;

    if (m_ctx && m_ctx->authService && !m_ctx->session.token.isEmpty()) {
        auto lockResult = m_ctx->authService->lockConsole(m_ctx->session.token);
        if (lockResult.isErr()) {
            QMessageBox::warning(this, tr("Lock Failed"), lockResult.errorMessage());
            return;
        }
    }

    m_locked = true;
    setEnabled(false);
    statusBar()->showMessage(tr("Console locked — sign in to unlock"));
    if (m_tray) m_tray->setTrayState(TrayManager::TrayState::Locked);
    emit consoleLocked();
}

bool MainShell::promptUnlock()
{
    if (!m_locked)
        return true;

    if (!m_ctx || !m_ctx->authService || m_ctx->session.token.isEmpty()) {
        QMessageBox::warning(this,
                             tr("Unlock Failed"),
                             tr("No active session is available for unlock."));
        return false;
    }

    bool ok = false;
    const QString password = QInputDialog::getText(
        this,
        tr("Unlock Console"),
        tr("Re-enter your password:"),
        QLineEdit::Password,
        {},
        &ok);
    if (!ok)
        return false;

    auto unlockResult = m_ctx->authService->unlockConsole(m_ctx->session.token, password);
    if (unlockResult.isErr()) {
        QMessageBox::warning(this, tr("Unlock Failed"), unlockResult.errorMessage());
        return false;
    }

    m_ctx->session = unlockResult.value();
    unlockConsole();
    return true;
}

void MainShell::unlockConsole()
{
    if (!m_locked) return;
    m_locked = false;
    setEnabled(true);
    statusBar()->clearMessage();
    if (m_tray) m_tray->setTrayState(TrayManager::TrayState::Normal);
    emit consoleUnlocked();
}

void MainShell::restoreWorkspace()
{
    const auto& snap = m_workspaceState.snapshot();
    for (const QString& winId : snap.openWindows)
        openWindow(winId);
}

void MainShell::setWarningIndicator(bool active, const QString& tooltip)
{
    if (m_tray) {
        m_tray->setTrayState(active
            ? TrayManager::TrayState::Warning
            : TrayManager::TrayState::Normal);
    }
    if (active) {
        statusBar()->showMessage(
            tooltip.isEmpty() ? tr("Attention required") : tooltip);
    } else {
        statusBar()->clearMessage();
    }
}

void MainShell::keyPressEvent(QKeyEvent* event)
{
    const bool ctrl = (event->modifiers() & Qt::ControlModifier) != 0;
    if (ctrl) {
        switch (event->key()) {
        case Qt::Key_K: onCommandPalette(); return;
        case Qt::Key_F: onAdvancedFilter(); return;
        case Qt::Key_L: onConsoleLock();    return;
        default: break;
        }
    }
    QMainWindow::keyPressEvent(event);
}

void MainShell::closeEvent(QCloseEvent* event)
{
    m_settings.setMainWindowGeometry(saveGeometry());
    emit exitRequested();
    event->accept();
}

void MainShell::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    buildContextMenu(&menu);
    if (!menu.actions().isEmpty())
        menu.exec(event->globalPos());
}

void MainShell::onCommandPalette()
{
    if (m_locked) return;
    m_commandPalette->activate();
}

void MainShell::onAdvancedFilter()
{
    if (m_locked) return;

    // Direct filter activation on the active MDI sub-window (no router dispatch
    // — the router action also calls this method; dispatching from here would
    // cause infinite recursion).
    QMdiSubWindow* active = m_mdiArea->activeSubWindow();
    if (!active || !active->widget()) {
        statusBar()->showMessage(tr("No active window for filtering"), 3000);
        return;
    }

    // Feature windows that support filtering expose an "activateFilter" slot
    // via Qt meta-object invocation.
    bool invoked = QMetaObject::invokeMethod(
        active->widget(), "activateFilter", Qt::DirectConnection);
    if (!invoked) {
        statusBar()->showMessage(
            tr("Active window does not support advanced filtering"), 3000);
    }
}

void MainShell::onConsoleLock()
{
    lockConsole();
}

void MainShell::setupMenuBar()
{
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("Lock Console\tCtrl+L"),
                        this, &MainShell::lockConsole);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Exit"), this, &MainShell::close);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tr("Command Palette\tCtrl+K"),
                        this, &MainShell::onCommandPalette);
    viewMenu->addAction(tr("Advanced Filter\tCtrl+F"),
                        this, &MainShell::onAdvancedFilter);

    auto* windowMenu = menuBar()->addMenu(tr("&Windows"));
    windowMenu->addAction(tr("Check-In Desk"),
        this, [this]{ openWindow(QLatin1String(CheckInWindow::WindowId)); });
    windowMenu->addAction(tr("Question Bank"),
        this, [this]{ openWindow(QLatin1String(QuestionBankWindow::WindowId)); });
    windowMenu->addAction(tr("Audit Viewer"),
        this, [this]{ openWindow(QLatin1String(AuditViewerWindow::WindowId)); });
}

void MainShell::setupStatusBar()
{
    statusBar()->showMessage(tr("Ready"));
}

void MainShell::registerShellActions()
{
    // Shell-level actions: always present in the command palette
    m_router.registerAction({
        QStringLiteral("shell.lock"),
        tr("Lock Console"),
        tr("Shell"),
        QKeySequence(Qt::CTRL | Qt::Key_L),
        false,
        [this]{ lockConsole(); }
    });
    m_router.registerAction({
        QStringLiteral("shell.command_palette"),
        tr("Open Command Palette"),
        tr("Shell"),
        QKeySequence(Qt::CTRL | Qt::Key_K),
        false,
        [this]{ onCommandPalette(); }
    });
    m_router.registerAction({
        QStringLiteral("shell.advanced_filter"),
        tr("Advanced Filter"),
        tr("Shell"),
        QKeySequence(Qt::CTRL | Qt::Key_F),
        true,
        [this]{ onAdvancedFilter(); }
    });

    // Window-open actions
    m_router.registerAction({
        QLatin1String(CheckInWindow::WindowId),
        tr("Open Check-In Desk"),
        tr("Windows"),
        {},
        true,
        [this]{ openWindow(QLatin1String(CheckInWindow::WindowId)); }
    });
    m_router.registerAction({
        QLatin1String(QuestionBankWindow::WindowId),
        tr("Open Question Bank"),
        tr("Windows"),
        {},
        true,
        [this]{ openWindow(QLatin1String(QuestionBankWindow::WindowId)); }
    });
    m_router.registerAction({
        QLatin1String(AuditViewerWindow::WindowId),
        tr("Open Audit Viewer"),
        tr("Windows"),
        {},
        true,
        [this]{ openWindow(QLatin1String(AuditViewerWindow::WindowId)); }
    });

    // Domain-sensitive context-menu actions — delegate to the active MDI sub-window.
    // Each feature window exposes the matching Q_INVOKABLE slot when applicable.
    m_router.registerAction({
        QStringLiteral("content.map_to_kp"),
        tr("Map to Knowledge Point"),
        tr("Content"),
        {},
        true,
        [this]{
            QMdiSubWindow* active = m_mdiArea->activeSubWindow();
            if (active && active->widget())
                QMetaObject::invokeMethod(active->widget(), "mapSelectedToKnowledgePoint",
                                          Qt::DirectConnection);
        }
    });
    m_router.registerAction({
        QStringLiteral("member.mask_pii"),
        tr("Mask PII"),
        tr("Members"),
        {},
        true,
        [this]{
            QMdiSubWindow* active = m_mdiArea->activeSubWindow();
            if (active && active->widget())
                QMetaObject::invokeMethod(active->widget(), "maskSelectedPii",
                                          Qt::DirectConnection);
        }
    });
    m_router.registerAction({
        QStringLiteral("member.export_request"),
        tr("Export for Request"),
        tr("Members"),
        {},
        true,
        [this]{
            QMdiSubWindow* active = m_mdiArea->activeSubWindow();
            if (active && active->widget())
                QMetaObject::invokeMethod(active->widget(), "exportSelectedForRequest",
                                          Qt::DirectConnection);
        }
    });
}

void MainShell::buildContextMenu(QMenu* menu)
{
    if (m_locked) return;
    menu->addAction(tr("Command Palette\tCtrl+K"),
                    this, &MainShell::onCommandPalette);
    menu->addSeparator();
    menu->addAction(tr("Map to Knowledge Point"),
        this, [this]{ m_router.dispatch(QStringLiteral("content.map_to_kp")); });
    menu->addAction(tr("Mask PII"),
        this, [this]{ m_router.dispatch(QStringLiteral("member.mask_pii")); });
    menu->addAction(tr("Export for Request"),
        this, [this]{ m_router.dispatch(QStringLiteral("member.export_request")); });
    menu->addSeparator();
    menu->addAction(tr("Lock Console"), this, &MainShell::lockConsole);
}
