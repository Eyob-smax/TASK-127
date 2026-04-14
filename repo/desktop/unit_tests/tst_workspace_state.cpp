// tst_workspace_state.cpp — ProctorOps
// Unit tests for WorkspaceState: load, save, window tracking, action markers,
// interrupted job IDs, singleton constraint, and JSON round-trip fidelity.

#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include "app/WorkspaceState.h"

static bool setupSchema(QSqlDatabase& db)
{
    QSqlQuery q(db);
    return q.exec(QStringLiteral(R"(
        CREATE TABLE IF NOT EXISTS workspace_state (
            id                  INTEGER PRIMARY KEY CHECK (id = 1),
            open_windows        TEXT    NOT NULL DEFAULT '[]',
            pending_actions     TEXT    NOT NULL DEFAULT '[]',
            interrupted_job_ids TEXT    NOT NULL DEFAULT '[]',
            updated_at          TEXT    NOT NULL
        )
    )"));
}

class TstWorkspaceState : public QObject {
    Q_OBJECT

private:
    QSqlDatabase m_db;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    void loadEmptySnapshotOnFreshDb();
    void saveAndReloadOpenWindows();
    void markWindowOpenAndClosed();
    void closingUnknownWindowIsNoop();
    void addAndRemovePendingAction();
    void removingUnknownActionIsNoop();
    void addAndClearInterruptedJobs();
    void multipleWindowsTrackedCorrectly();
    void duplicateWindowNotAddedTwice();
    void saveIsSingletonRow();
    void pendingActionsRoundTrip();
    void interruptedJobsRoundTrip();
};

void TstWorkspaceState::initTestCase()
{
    m_db = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"), QStringLiteral("tst_ws"));
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(m_db.open());
    QVERIFY(setupSchema(m_db));
}

void TstWorkspaceState::cleanupTestCase()
{
    m_db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("tst_ws"));
}

void TstWorkspaceState::cleanup()
{
    QSqlQuery(m_db).exec(QStringLiteral("DELETE FROM workspace_state"));
}

void TstWorkspaceState::loadEmptySnapshotOnFreshDb()
{
    WorkspaceState ws(m_db);
    QVERIFY(ws.load());
    QVERIFY(ws.snapshot().openWindows.isEmpty());
    QVERIFY(ws.snapshot().pendingActionMarkers.isEmpty());
    QVERIFY(ws.snapshot().interruptedJobIds.isEmpty());
}

void TstWorkspaceState::saveAndReloadOpenWindows()
{
    {
        WorkspaceState ws(m_db);
        ws.load();
        ws.markWindowOpen(QStringLiteral("CheckInWindow"));
        ws.markWindowOpen(QStringLiteral("AuditViewerWindow"));
    }
    WorkspaceState ws2(m_db);
    QVERIFY(ws2.load());
    QCOMPARE(ws2.snapshot().openWindows.size(), 2);
    QVERIFY(ws2.snapshot().openWindows.contains(QStringLiteral("CheckInWindow")));
    QVERIFY(ws2.snapshot().openWindows.contains(QStringLiteral("AuditViewerWindow")));
}

void TstWorkspaceState::markWindowOpenAndClosed()
{
    WorkspaceState ws(m_db);
    ws.load();
    ws.markWindowOpen(QStringLiteral("CheckInWindow"));
    QCOMPARE(ws.snapshot().openWindows.size(), 1);
    ws.markWindowClosed(QStringLiteral("CheckInWindow"));
    QVERIFY(ws.snapshot().openWindows.isEmpty());
}

void TstWorkspaceState::closingUnknownWindowIsNoop()
{
    WorkspaceState ws(m_db);
    ws.load();
    ws.markWindowOpen(QStringLiteral("A"));
    ws.markWindowClosed(QStringLiteral("NonExistent"));
    QCOMPARE(ws.snapshot().openWindows.size(), 1);
}

void TstWorkspaceState::addAndRemovePendingAction()
{
    WorkspaceState ws(m_db);
    ws.load();
    ws.addPendingAction(QStringLiteral("correction:request:req-001"));
    QCOMPARE(ws.snapshot().pendingActionMarkers.size(), 1);
    ws.removePendingAction(QStringLiteral("correction:request:req-001"));
    QVERIFY(ws.snapshot().pendingActionMarkers.isEmpty());
}

void TstWorkspaceState::removingUnknownActionIsNoop()
{
    WorkspaceState ws(m_db);
    ws.load();
    ws.addPendingAction(QStringLiteral("known"));
    ws.removePendingAction(QStringLiteral("unknown"));
    QCOMPARE(ws.snapshot().pendingActionMarkers.size(), 1);
}

void TstWorkspaceState::addAndClearInterruptedJobs()
{
    WorkspaceState ws(m_db);
    ws.load();
    ws.addInterruptedJob(QStringLiteral("job-001"));
    ws.addInterruptedJob(QStringLiteral("job-002"));
    ws.addInterruptedJob(QStringLiteral("job-003"));
    QCOMPARE(ws.snapshot().interruptedJobIds.size(), 3);
    ws.clearInterruptedJobs();
    QVERIFY(ws.snapshot().interruptedJobIds.isEmpty());
}

void TstWorkspaceState::multipleWindowsTrackedCorrectly()
{
    WorkspaceState ws(m_db);
    ws.load();
    ws.markWindowOpen(QStringLiteral("A"));
    ws.markWindowOpen(QStringLiteral("B"));
    ws.markWindowOpen(QStringLiteral("C"));
    ws.markWindowClosed(QStringLiteral("B"));
    QCOMPARE(ws.snapshot().openWindows.size(), 2);
    QVERIFY(!ws.snapshot().openWindows.contains(QStringLiteral("B")));
    QVERIFY(ws.snapshot().openWindows.contains(QStringLiteral("A")));
    QVERIFY(ws.snapshot().openWindows.contains(QStringLiteral("C")));
}

void TstWorkspaceState::duplicateWindowNotAddedTwice()
{
    WorkspaceState ws(m_db);
    ws.load();
    ws.markWindowOpen(QStringLiteral("CheckInWindow"));
    ws.markWindowOpen(QStringLiteral("CheckInWindow"));
    QCOMPARE(ws.snapshot().openWindows.size(), 1);
}

void TstWorkspaceState::saveIsSingletonRow()
{
    WorkspaceState ws(m_db);
    ws.load();
    ws.markWindowOpen(QStringLiteral("W1"));
    ws.markWindowOpen(QStringLiteral("W2"));
    ws.markWindowClosed(QStringLiteral("W1"));
    // Should still be only one row in workspace_state
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM workspace_state"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
}

void TstWorkspaceState::pendingActionsRoundTrip()
{
    const QStringList markers = {
        QStringLiteral("correction:request:req-001"),
        QStringLiteral("ingestion:job:job-abc"),
        QStringLiteral("export:request:exp-xyz")
    };
    {
        WorkspaceState ws(m_db);
        ws.load();
        for (const QString& m : markers) ws.addPendingAction(m);
    }
    WorkspaceState ws2(m_db);
    QVERIFY(ws2.load());
    QCOMPARE(ws2.snapshot().pendingActionMarkers.size(), markers.size());
    for (const QString& m : markers)
        QVERIFY(ws2.snapshot().pendingActionMarkers.contains(m));
}

void TstWorkspaceState::interruptedJobsRoundTrip()
{
    const QStringList jobs = {
        QStringLiteral("job-001"),
        QStringLiteral("job-002"),
        QStringLiteral("job-003")
    };
    {
        WorkspaceState ws(m_db);
        ws.load();
        for (const QString& j : jobs) ws.addInterruptedJob(j);
    }
    WorkspaceState ws2(m_db);
    QVERIFY(ws2.load());
    QCOMPARE(ws2.snapshot().interruptedJobIds.size(), jobs.size());
    for (const QString& j : jobs)
        QVERIFY(ws2.snapshot().interruptedJobIds.contains(j));
}

QTEST_MAIN(TstWorkspaceState)
#include "tst_workspace_state.moc"
