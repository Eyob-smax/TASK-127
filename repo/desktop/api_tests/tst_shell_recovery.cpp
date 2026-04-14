// tst_shell_recovery.cpp — ProctorOps
// Integration tests for shell crash-recovery and workspace restoration flows.
//
// Tests that interrupted operational state is preserved across a simulated crash:
// open windows, pending workflow action markers, and interrupted ingestion job IDs
// are all saved to SQLite and correctly loaded on the next session.
// The crash detection logic (unclosed app_lifecycle row) is also verified.

#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDateTime>
#include "app/WorkspaceState.h"

// ── Schema setup ──────────────────────────────────────────────────────────────

static bool exec(QSqlDatabase& db, const QString& sql)
{
    return QSqlQuery(db).exec(sql);
}

static bool createSchema(QSqlDatabase& db)
{
    return exec(db, QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS app_lifecycle (
            id                INTEGER PRIMARY KEY AUTOINCREMENT,
            started_at        TEXT NOT NULL,
            clean_shutdown_at TEXT,
            app_version       TEXT NOT NULL
        )
    )")) &&
    exec(db, QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS workspace_state (
            id                  INTEGER PRIMARY KEY CHECK (id = 1),
            open_windows        TEXT NOT NULL DEFAULT '[]',
            pending_actions     TEXT NOT NULL DEFAULT '[]',
            interrupted_job_ids TEXT NOT NULL DEFAULT '[]',
            updated_at          TEXT NOT NULL
        )
    )"));
}

static bool detectCrash(QSqlDatabase& db)
{
    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "SELECT id FROM app_lifecycle "
        "WHERE clean_shutdown_at IS NULL "
        "ORDER BY id DESC LIMIT 1"
    ));
    return q.next();
}

static int startSession(QSqlDatabase& db)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO app_lifecycle (started_at, app_version) VALUES (?,?)"
    ));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.addBindValue(QStringLiteral("0.1.0"));
    q.exec();
    return static_cast<int>(q.lastInsertId().toLongLong());
}

static void endSession(QSqlDatabase& db, int id)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE app_lifecycle SET clean_shutdown_at = ? WHERE id = ?"
    ));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.addBindValue(id);
    q.exec();
}

// ── Test class ────────────────────────────────────────────────────────────────

class TstShellRecovery : public QObject {
    Q_OBJECT

private:
    QSqlDatabase m_db;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    // Crash detection
    void cleanSessionNoCrash();
    void unclosedSessionIsCrash();
    void closedThenUnclosedIsCrash();

    // Window restoration
    void openWindowsRestoredAfterCrash();
    void closedWindowsNotRestored();
    void allThreeWindowsRestored();

    // Pending action markers
    void pendingActionsRestoredAfterCrash();
    void completedActionsNotPresent();

    // Interrupted job IDs
    void interruptedJobsRestoredAfterCrash();
    void clearedJobsNotPresent();

    // Combined crash + workspace
    void fullCrashScenarioRestoresCompleteState();
    void gracefulShutdownLeavesCleanState();
};

void TstShellRecovery::initTestCase()
{
    m_db = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"), QStringLiteral("tst_shell_recovery"));
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(m_db.open());
    QVERIFY(createSchema(m_db));
}

void TstShellRecovery::cleanupTestCase()
{
    m_db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("tst_shell_recovery"));
}

void TstShellRecovery::cleanup()
{
    exec(m_db, QStringLiteral("DELETE FROM app_lifecycle"));
    exec(m_db, QStringLiteral("DELETE FROM workspace_state"));
}

void TstShellRecovery::cleanSessionNoCrash()
{
    const int id = startSession(m_db);
    endSession(m_db, id);
    QVERIFY(!detectCrash(m_db));
}

void TstShellRecovery::unclosedSessionIsCrash()
{
    startSession(m_db);
    QVERIFY(detectCrash(m_db));
}

void TstShellRecovery::closedThenUnclosedIsCrash()
{
    const int id = startSession(m_db);
    endSession(m_db, id);
    startSession(m_db); // new session, unclosed
    QVERIFY(detectCrash(m_db));
}

void TstShellRecovery::openWindowsRestoredAfterCrash()
{
    // Session 1: open windows and crash (no shutdown)
    {
        WorkspaceState ws(m_db);
        ws.load();
        ws.markWindowOpen(QStringLiteral("CheckInWindow"));
        ws.markWindowOpen(QStringLiteral("AuditViewerWindow"));
    }
    // Session 2 recovery
    WorkspaceState ws2(m_db);
    QVERIFY(ws2.load());
    QCOMPARE(ws2.snapshot().openWindows.size(), 2);
    QVERIFY(ws2.snapshot().openWindows.contains(QStringLiteral("CheckInWindow")));
    QVERIFY(ws2.snapshot().openWindows.contains(QStringLiteral("AuditViewerWindow")));
}

void TstShellRecovery::closedWindowsNotRestored()
{
    {
        WorkspaceState ws(m_db);
        ws.load();
        ws.markWindowOpen(QStringLiteral("A"));
        ws.markWindowOpen(QStringLiteral("B"));
        ws.markWindowClosed(QStringLiteral("B")); // closed before crash
    }
    WorkspaceState ws2(m_db);
    QVERIFY(ws2.load());
    QCOMPARE(ws2.snapshot().openWindows.size(), 1);
    QVERIFY(ws2.snapshot().openWindows.contains(QStringLiteral("A")));
    QVERIFY(!ws2.snapshot().openWindows.contains(QStringLiteral("B")));
}

void TstShellRecovery::allThreeWindowsRestored()
{
    {
        WorkspaceState ws(m_db);
        ws.load();
        ws.markWindowOpen(QStringLiteral("CheckInWindow"));
        ws.markWindowOpen(QStringLiteral("QuestionBankWindow"));
        ws.markWindowOpen(QStringLiteral("AuditViewerWindow"));
    }
    WorkspaceState ws2(m_db);
    QVERIFY(ws2.load());
    QCOMPARE(ws2.snapshot().openWindows.size(), 3);
}

void TstShellRecovery::pendingActionsRestoredAfterCrash()
{
    {
        WorkspaceState ws(m_db);
        ws.load();
        ws.addPendingAction(QStringLiteral("correction:request:req-001"));
        ws.addPendingAction(QStringLiteral("export:request:exp-001"));
    }
    WorkspaceState ws2(m_db);
    QVERIFY(ws2.load());
    QCOMPARE(ws2.snapshot().pendingActionMarkers.size(), 2);
}

void TstShellRecovery::completedActionsNotPresent()
{
    {
        WorkspaceState ws(m_db);
        ws.load();
        ws.addPendingAction(QStringLiteral("action-a"));
        ws.addPendingAction(QStringLiteral("action-b"));
        ws.removePendingAction(QStringLiteral("action-a")); // completed before crash
    }
    WorkspaceState ws2(m_db);
    QVERIFY(ws2.load());
    QCOMPARE(ws2.snapshot().pendingActionMarkers.size(), 1);
    QVERIFY(!ws2.snapshot().pendingActionMarkers.contains(QStringLiteral("action-a")));
    QVERIFY(ws2.snapshot().pendingActionMarkers.contains(QStringLiteral("action-b")));
}

void TstShellRecovery::interruptedJobsRestoredAfterCrash()
{
    {
        WorkspaceState ws(m_db);
        ws.load();
        ws.addInterruptedJob(QStringLiteral("job-001"));
        ws.addInterruptedJob(QStringLiteral("job-002"));
        ws.addInterruptedJob(QStringLiteral("job-003"));
    }
    WorkspaceState ws2(m_db);
    QVERIFY(ws2.load());
    QCOMPARE(ws2.snapshot().interruptedJobIds.size(), 3);
    QVERIFY(ws2.snapshot().interruptedJobIds.contains(QStringLiteral("job-002")));
}

void TstShellRecovery::clearedJobsNotPresent()
{
    {
        WorkspaceState ws(m_db);
        ws.load();
        ws.addInterruptedJob(QStringLiteral("job-001"));
        ws.clearInterruptedJobs(); // cleared before crash
    }
    WorkspaceState ws2(m_db);
    QVERIFY(ws2.load());
    QVERIFY(ws2.snapshot().interruptedJobIds.isEmpty());
}

void TstShellRecovery::fullCrashScenarioRestoresCompleteState()
{
    // Simulate: operator had all three windows open, one job running, one correction pending
    {
        WorkspaceState ws(m_db);
        ws.load();
        ws.markWindowOpen(QStringLiteral("CheckInWindow"));
        ws.markWindowOpen(QStringLiteral("QuestionBankWindow"));
        ws.markWindowOpen(QStringLiteral("AuditViewerWindow"));
        ws.addPendingAction(QStringLiteral("correction:request:req-xyz"));
        ws.addInterruptedJob(QStringLiteral("roster-import-job-001"));
    }

    // Simulate crash (unclosed lifecycle row)
    startSession(m_db); // no shutdown → crash

    QVERIFY(detectCrash(m_db));

    WorkspaceState ws2(m_db);
    QVERIFY(ws2.load());
    QCOMPARE(ws2.snapshot().openWindows.size(), 3);
    QCOMPARE(ws2.snapshot().pendingActionMarkers.size(), 1);
    QCOMPARE(ws2.snapshot().interruptedJobIds.size(), 1);
    QVERIFY(ws2.snapshot().interruptedJobIds.contains(
        QStringLiteral("roster-import-job-001")));
}

void TstShellRecovery::gracefulShutdownLeavesCleanState()
{
    const int sessionId = startSession(m_db);

    WorkspaceState ws(m_db);
    ws.load();
    ws.markWindowOpen(QStringLiteral("CheckInWindow"));
    ws.addInterruptedJob(QStringLiteral("stale-job"));

    // Graceful shutdown: clear transient state and record lifecycle
    ws.markWindowClosed(QStringLiteral("CheckInWindow"));
    ws.clearInterruptedJobs();
    endSession(m_db, sessionId);

    QVERIFY(!detectCrash(m_db));

    WorkspaceState ws2(m_db);
    QVERIFY(ws2.load());
    QVERIFY(ws2.snapshot().openWindows.isEmpty());
    QVERIFY(ws2.snapshot().interruptedJobIds.isEmpty());
}

QTEST_MAIN(TstShellRecovery)
#include "tst_shell_recovery.moc"
