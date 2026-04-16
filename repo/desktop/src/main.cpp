// main.cpp — ProctorOps
// Application entry point.
//
// ProctorOps is a Windows 11 offline-native Qt desktop console for exam
// content governance, on-site entry validation, and compliance-grade
// traceability. There is no web frontend, no remote backend, and no SaaS
// dependency of any kind.
//
// Signed .msi override:
//   Delivery does not require a signed .msi artifact. See docs/design.md §2.
//
// Startup sequence:
//   1. Application::initialize() — migrations, crash detection, workspace load
//   2. AppContext — crypto, repositories, services initialized in dependency order
//   3. ActionRouter — central action registry
//   4. MainShell — workspace controller, MDI area, global shortcuts
//   5. TrayManager — system tray icon, kiosk mode, lock menu
//   6. Crash recovery prompt if previous session was unclean
//   7. PerformanceObserver memory sampling begins

#include "app/Application.h"
#include "app/ActionRouter.h"
#include "app/AppBootstrap.h"
#include "app/AppContext.h"
#include "windows/MainShell.h"
#include "windows/LoginWindow.h"
#include "tray/TrayManager.h"

// Crypto
#include "crypto/KeyStore.h"
#include "crypto/AesGcmCipher.h"

// Repositories
#include "repositories/UserRepository.h"
#include "repositories/AuditRepository.h"
#include "repositories/QuestionRepository.h"
#include "repositories/KnowledgePointRepository.h"
#include "repositories/MemberRepository.h"
#include "repositories/CheckInRepository.h"
#include "repositories/IngestionRepository.h"
#include "repositories/SyncRepository.h"
#include "repositories/UpdateRepository.h"

// Services
#include "services/AuditService.h"
#include "services/AuthService.h"
#include "services/QuestionService.h"
#include "services/CheckInService.h"
#include "services/IngestionService.h"
#include "services/SyncService.h"
#include "services/DataSubjectService.h"
#include "services/UpdateService.h"
#include "services/PackageVerifier.h"
#include "scheduler/JobScheduler.h"

#include "utils/Logger.h"
#include "utils/PerformanceObserver.h"

#include <QEventLoop>
int main(int argc, char *argv[])
{
    Application app(argc, argv);

    if (!app.initialize()) {
        // Fatal: migration failure or DB open error. Logger has the details.
        return 1;
    }

    // Build infrastructure context
    auto ctx = buildAppContext(app.database(), app.settings());
    if (!ctx) {
        Logger::instance().error(
            QStringLiteral("startup"),
            QStringLiteral("Fatal: AppContext initialization failed"),
            {});
        return 1;
    }

    // ── Login ─────────────────────────────────────────────────────────────────
    // Run LoginWindow in a nested event loop. The shell's main event loop
    // (app.exec()) is started only after successful authentication so that
    // services are not accessed before a valid session is established.
    {
        LoginWindow loginWin(*ctx->authService);
        loginWin.setWindowTitle(QStringLiteral("ProctorOps — Sign In"));
        loginWin.show();
        loginWin.checkBootstrapMode();

        bool loggedIn = false;
        QEventLoop loginLoop;

        QObject::connect(&loginWin, &LoginWindow::loginSucceeded,
                         [&ctx, &loggedIn, &loginWin, &loginLoop]
                         (const QString& sessionToken, const QString& userId) {
            ctx->session.token  = sessionToken;
            ctx->session.userId = userId;
            ctx->session.active = true;
            ctx->authenticated  = true;
            loggedIn            = true;
            loginWin.close();
            loginLoop.quit();
        });
        // If user closes login window without signing in, quit
        QObject::connect(&loginWin, &QWidget::destroyed,
                         &loginLoop, &QEventLoop::quit);

        loginLoop.exec();
        if (!loggedIn) return 0;
    }

    // ── Shell and action routing ───────────────────────────────────────────────
    ActionRouter router;
    MainShell    shell(router, app.workspaceState(), app.settings(), ctx.get());

    // System tray
    TrayManager tray(&shell);
    tray.show();
    shell.setTrayManager(&tray);

    // Wire exit signal to clean-shutdown record
    QObject::connect(&shell, &MainShell::exitRequested, [&app] {
        app.recordCleanShutdown();
    });

    // Wire tray exit to shell close
    QObject::connect(&tray, &TrayManager::exitRequested,
                     &shell, &MainShell::close);

    // Show shell and restore previous workspace
    shell.show();

    if (app.crashDetected()) {
        shell.setWarningIndicator(
            true,
            QStringLiteral("Previous session ended unexpectedly. "
                           "Check for interrupted ingestion jobs."));
    }

    shell.restoreWorkspace();

    // Start background scheduler
    ctx->jobScheduler->start();

    // Begin periodic memory observation (60-second interval)
    app.performanceObserver().startMemoryObservation(60000);

    const int exitCode = app.exec();

    // Graceful cleanup
    ctx->jobScheduler->stop();
    return exitCode;
}
