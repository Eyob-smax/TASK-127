// tst_main_entrypoint.cpp — ProctorOps
// Dedicated isolated contract tests for src/main.cpp orchestration flow.

#include <QtTest/QtTest>
#include <QFile>

class TstMainEntrypoint : public QObject
{
    Q_OBJECT

private slots:
    void test_mainOrchestration_containsExpectedSequence();
    void test_loginGate_existsBeforeShellLoop();
};

void TstMainEntrypoint::test_mainOrchestration_containsExpectedSequence()
{
    QFile file(QStringLiteral(SOURCE_ROOT "/src/main.cpp"));
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), "Unable to open src/main.cpp");

    const QString content = QString::fromUtf8(file.readAll());

    const int iAppCtor = content.indexOf(QStringLiteral("Application app(argc, argv);"));
    const int iInit = content.indexOf(QStringLiteral("if (!app.initialize())"));
    const int iBuildCtx = content.indexOf(QStringLiteral("auto ctx = buildAppContext(app.database(), app.settings());"));
    const int iLogin = content.indexOf(QStringLiteral("LoginWindow loginWin(*ctx->authService);"));
    const int iShell = content.indexOf(QStringLiteral("MainShell    shell(router, app.workspaceState(), app.settings(), ctx.get());"));
    const int iTray = content.indexOf(QStringLiteral("TrayManager tray(&shell);"));
    const int iSchedulerStart = content.indexOf(QStringLiteral("ctx->jobScheduler->start();"));
    const int iExec = content.indexOf(QStringLiteral("const int exitCode = app.exec();"));
    const int iSchedulerStop = content.indexOf(QStringLiteral("ctx->jobScheduler->stop();"));

    QVERIFY(iAppCtor >= 0);
    QVERIFY(iInit > iAppCtor);
    QVERIFY(iBuildCtx > iInit);
    QVERIFY(iLogin > iBuildCtx);
    QVERIFY(iShell > iLogin);
    QVERIFY(iTray > iShell);
    QVERIFY(iSchedulerStart > iTray);
    QVERIFY(iExec > iSchedulerStart);
    QVERIFY(iSchedulerStop > iExec);
}

void TstMainEntrypoint::test_loginGate_existsBeforeShellLoop()
{
    QFile file(QStringLiteral(SOURCE_ROOT "/src/main.cpp"));
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), "Unable to open src/main.cpp");

    const QString content = QString::fromUtf8(file.readAll());

    const int iLoginGate = content.indexOf(QStringLiteral("if (!loggedIn) return 0;"));
    const int iShell = content.indexOf(QStringLiteral("MainShell    shell(router, app.workspaceState(), app.settings(), ctx.get());"));

    QVERIFY(iLoginGate >= 0);
    QVERIFY(iShell > iLoginGate);
}

QTEST_APPLESS_MAIN(TstMainEntrypoint)
#include "tst_main_entrypoint.moc"
