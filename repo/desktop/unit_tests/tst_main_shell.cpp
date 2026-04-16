// tst_main_shell.cpp — ProctorOps
// Unit tests for MainShell workspace/window lifecycle and shell action wiring.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QMdiArea>
#include <QMdiSubWindow>

#include "windows/MainShell.h"
#include "app/ActionRouter.h"
#include "app/AppSettings.h"
#include "app/WorkspaceState.h"

class TstMainShell : public QObject {
    Q_OBJECT

private:
    QSqlDatabase m_db;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    void test_openWindow_deduplicatesByWindowId();
    void test_closeWindow_updatesWorkspaceState();
    void test_restoreWorkspace_reopensTrackedWindows();
    void test_lockUnlock_emitsSignalsAndState();
    void test_registerShellActions_exposesExpectedIds();
};

void TstMainShell::initTestCase()
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("tst_main_shell"));
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(m_db.open());

    QSqlQuery q(m_db);
    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE workspace_state ("
        "id INTEGER PRIMARY KEY CHECK (id = 1),"
        "open_windows TEXT NOT NULL DEFAULT '[]',"
        "pending_actions TEXT NOT NULL DEFAULT '[]',"
        "interrupted_job_ids TEXT NOT NULL DEFAULT '[]',"
        "updated_at TEXT NOT NULL)")));
}

void TstMainShell::cleanupTestCase()
{
    m_db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("tst_main_shell"));
}

void TstMainShell::cleanup()
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("DELETE FROM workspace_state"));
}

void TstMainShell::test_openWindow_deduplicatesByWindowId()
{
    ActionRouter router;
    WorkspaceState ws(m_db);
    QVERIFY(ws.load());
    AppSettings settings;

    MainShell shell(router, ws, settings, nullptr);
    shell.openWindow(QStringLiteral("window.checkin"));
    shell.openWindow(QStringLiteral("window.checkin"));

    auto* mdi = shell.findChild<QMdiArea*>();
    QVERIFY(mdi != nullptr);
    QCOMPARE(mdi->subWindowList().size(), 1);
    QCOMPARE(mdi->subWindowList().first()->objectName(), QStringLiteral("window.checkin"));
}

void TstMainShell::test_closeWindow_updatesWorkspaceState()
{
    ActionRouter router;
    WorkspaceState ws(m_db);
    QVERIFY(ws.load());
    AppSettings settings;

    MainShell shell(router, ws, settings, nullptr);
    shell.show();
    QTest::qWait(10);

    shell.openWindow(QStringLiteral("window.sync"));
    QVERIFY(ws.snapshot().openWindows.contains(QStringLiteral("window.sync")));

    shell.closeWindow(QStringLiteral("window.sync"));
    QTRY_VERIFY(!ws.snapshot().openWindows.contains(QStringLiteral("window.sync")));
}

void TstMainShell::test_restoreWorkspace_reopensTrackedWindows()
{
    ActionRouter router;
    WorkspaceState ws(m_db);
    QVERIFY(ws.load());
    ws.markWindowOpen(QStringLiteral("window.checkin"));
    ws.markWindowOpen(QStringLiteral("window.audit_viewer"));
    AppSettings settings;

    MainShell shell(router, ws, settings, nullptr);
    shell.restoreWorkspace();

    auto* mdi = shell.findChild<QMdiArea*>();
    QVERIFY(mdi != nullptr);
    QCOMPARE(mdi->subWindowList().size(), 2);
}

void TstMainShell::test_lockUnlock_emitsSignalsAndState()
{
    ActionRouter router;
    WorkspaceState ws(m_db);
    QVERIFY(ws.load());
    AppSettings settings;

    MainShell shell(router, ws, settings, nullptr);
    QSignalSpy lockedSpy(&shell, &MainShell::consoleLocked);
    QSignalSpy unlockedSpy(&shell, &MainShell::consoleUnlocked);

    shell.lockConsole();
    QVERIFY(shell.isLocked());
    QCOMPARE(lockedSpy.count(), 1);

    shell.unlockConsole();
    QVERIFY(!shell.isLocked());
    QCOMPARE(unlockedSpy.count(), 1);
}

void TstMainShell::test_registerShellActions_exposesExpectedIds()
{
    ActionRouter router;
    WorkspaceState ws(m_db);
    QVERIFY(ws.load());
    AppSettings settings;

    MainShell shell(router, ws, settings, nullptr);
    Q_UNUSED(shell);

    const auto actions = router.allActions();
    bool hasLock = false;
    bool hasPalette = false;
    bool hasFilter = false;
    bool hasCheckin = false;
    for (const auto& action : actions) {
        if (action.id == QStringLiteral("shell.lock")) hasLock = true;
        if (action.id == QStringLiteral("shell.command_palette")) hasPalette = true;
        if (action.id == QStringLiteral("shell.advanced_filter")) hasFilter = true;
        if (action.id == QStringLiteral("window.checkin")) hasCheckin = true;
    }

    QVERIFY(hasLock);
    QVERIFY(hasPalette);
    QVERIFY(hasFilter);
    QVERIFY(hasCheckin);
}

QTEST_MAIN(TstMainShell)
#include "tst_main_shell.moc"
