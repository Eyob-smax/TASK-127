// tst_crash_recovery.cpp — ProctorOps
// Unit tests for crash-recovery lifecycle detection and workspace state recovery.
// Tests the app_lifecycle crash-detection logic and WorkspaceState restoration
// directly via SQL without requiring a full Application initialization.

#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDateTime>
#include "app/WorkspaceState.h"

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool exec(QSqlDatabase& db, const QString& sql)
{
    return QSqlQuery(db).exec(sql);
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

static int recordStart(QSqlDatabase& db)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO app_lifecycle (started_at, app_version) VALUES (?, ?)"
    ));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.addBindValue(QStringLiteral("0.1.0"));
    q.exec();
    return static_cast<int>(q.lastInsertId().toLongLong());
}

static void recordShutdown(QSqlDatabase& db, int id)
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

class TstCrashRecovery : public QObject {
    Q_OBJECT

private:
    QSqlDatabase m_db;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    void noPreviousSessionNoCrash();
    void unclosedSessionDetectedAsCrash();
    void closedSessionNotDetectedAsCrash();
    void onlyLatestSessionCheckedForCrash();
    void multipleUnclosedSessionsDetectsLatest();
    void cleanShutdownClosesCrashWindow();
    void workspaceWindowsRestoredAfterCrash();
    void pendingActionsRestoredAfterCrash();
    void interruptedJobsRestoredAfterCrash();
    void cleanWorkspaceAfterNormalShutdown();
};

void TstCrashRecovery::initTestCase()
{
    m_db = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"), QStringLiteral("tst_cr"));
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(m_db.open());

    QVERIFY(exec(m_db, QStringLiteral(R"(
        CREATE TABLE app_lifecycle (
            id                INTEGER PRIMARY KEY AUTOINCREMENT,
            started_at        TEXT NOT NULL,
            clean_shutdown_at TEXT,
            app_version       TEXT NOT NULL
        )
    )")));
    QVERIFY(exec(m_db, QStringLiteral(R"(
        CREATE TABLE workspace_state (
            id                  INTEGER PRIMARY KEY CHECK (id = 1),
            open_windows        TEXT NOT NULL DEFAULT '[]',
            pending_actions     TEXT NOT NULL DEFAULT '[]',
            interrupted_job_ids TEXT NOT NULL DEFAULT '[]',
            updated_at          TEXT NOT NULL
        )
    )")));
}

void TstCrashRecovery::cleanupTestCase()
{
    m_db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("tst_cr"));
}

void TstCrashRecovery::cleanup()
{
    exec(m_db, QStringLiteral("DELETE FROM app_lifecycle"));
    exec(m_db, QStringLiteral("DELETE FROM workspace_state"));
}

void TstCrashRecovery::noPreviousSessionNoCrash()
{
    QVERIFY(!detectCrash(m_db));
}

void TstCrashRecovery::unclosedSessionDetectedAsCrash()
{
    recordStart(m_db); // no shutdown → looks like crash
    QVERIFY(detectCrash(m_db));
}

void TstCrashRecovery::closedSessionNotDetectedAsCrash()
{
    const int id = recordStart(m_db);
    recordShutdown(m_db, id);
    QVERIFY(!detectCrash(m_db));
}

void TstCrashRecovery::onlyLatestSessionCheckedForCrash()
{
    // Old session closed cleanly, new session unclosed → crash detected
    const int id1 = recordStart(m_db);
    recordShutdown(m_db, id1);
    recordStart(m_db); // new unclosed session
    QVERIFY(detectCrash(m_db));
}

void TstCrashRecovery::multipleUnclosedSessionsDetectsLatest()
{
    recordStart(m_db);
    recordStart(m_db);
    QVERIFY(detectCrash(m_db));
}

void TstCrashRecovery::cleanShutdownClosesCrashWindow()
{
    const int id = recordStart(m_db);
    QVERIFY(detectCrash(m_db));
    recordShutdown(m_db, id);
    QVERIFY(!detectCrash(m_db));
}

void TstCrashRecovery::workspaceWindowsRestoredAfterCrash()
{
    // Session 1: save workspace then "crash" (no clean shutdown)
    {
        WorkspaceState ws(m_db);
        ws.load();
        ws.markWindowOpen(QStringLiteral("CheckInWindow"));
        ws.markWindowOpen(QStringLiteral("QuestionBankWindow"));
    }
    // Session 2: restore
    WorkspaceState ws2(m_db);
    QVERIFY(ws2.load());
    QCOMPARE(ws2.snapshot().openWindows.size(), 2);
    QVERIFY(ws2.snapshot().openWindows.contains(QStringLiteral("CheckInWindow")));
    QVERIFY(ws2.snapshot().openWindows.contains(QStringLiteral("QuestionBankWindow")));
}

void TstCrashRecovery::pendingActionsRestoredAfterCrash()
{
    {
        WorkspaceState ws(m_db);
        ws.load();
        ws.addPendingAction(QStringLiteral("correction:request:req-001"));
        ws.addPendingAction(QStringLiteral("ingestion:job:job-xyz"));
    }
    WorkspaceState ws2(m_db);
    QVERIFY(ws2.load());
    QCOMPARE(ws2.snapshot().pendingActionMarkers.size(), 2);
    QVERIFY(ws2.snapshot().pendingActionMarkers.contains(
        QStringLiteral("correction:request:req-001")));
}

void TstCrashRecovery::interruptedJobsRestoredAfterCrash()
{
    {
        WorkspaceState ws(m_db);
        ws.load();
        ws.addInterruptedJob(QStringLiteral("job-001"));
        ws.addInterruptedJob(QStringLiteral("job-002"));
    }
    WorkspaceState ws2(m_db);
    QVERIFY(ws2.load());
    QCOMPARE(ws2.snapshot().interruptedJobIds.size(), 2);
    QVERIFY(ws2.snapshot().interruptedJobIds.contains(QStringLiteral("job-001")));
    QVERIFY(ws2.snapshot().interruptedJobIds.contains(QStringLiteral("job-002")));
}

void TstCrashRecovery::cleanWorkspaceAfterNormalShutdown()
{
    // Normal shutdown: workspace is cleared before exit
    WorkspaceState ws(m_db);
    ws.load();
    ws.markWindowOpen(QStringLiteral("CheckInWindow"));
    ws.addInterruptedJob(QStringLiteral("some-job"));

    // Simulate graceful shutdown: clear state
    ws.markWindowClosed(QStringLiteral("CheckInWindow"));
    ws.clearInterruptedJobs();

    WorkspaceState ws2(m_db);
    QVERIFY(ws2.load());
    QVERIFY(ws2.snapshot().openWindows.isEmpty());
    QVERIFY(ws2.snapshot().interruptedJobIds.isEmpty());
}

QTEST_MAIN(TstCrashRecovery)
#include "tst_crash_recovery.moc"
