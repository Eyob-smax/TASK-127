// tst_performance_observer.cpp — ProctorOps
// Unit tests for PerformanceObserver (cold-start and memory sampling recorder).
//
// PerformanceObserver writes rows to the performance_log table via a supplied
// QSqlDatabase. We drive it with an in-memory SQLite connection and an event
// loop where needed for the QTimer-based memory sampler.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSignalSpy>
#include <QCoreApplication>

#include "utils/PerformanceObserver.h"

class TstPerformanceObserver : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void test_recordColdStart_insertsRow();
    void test_recordColdStart_withinTargetLogsWithinTargetVerdict();
    void test_recordColdStart_exceedsTargetLogsExceedsTargetVerdict();
    void test_startMemoryObservation_recordsAtLeastOneSample();
    void test_stopMemoryObservation_writesShutdownEvent();
    void test_stopMemoryObservation_afterStopNoMoreSamples();
    void test_currentRssBytes_returnsNonNegative();

private:
    void applyPerformanceSchema();
    int countRows(const QString& eventType) const;

    QSqlDatabase m_db;
    int          m_dbIndex{0};
};

// ── init / cleanup ───────────────────────────────────────────────────────────

void TstPerformanceObserver::init()
{
    const QString connName =
        QStringLiteral("tst_perf_observer_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));
    applyPerformanceSchema();
}

void TstPerformanceObserver::cleanup()
{
    const QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstPerformanceObserver::applyPerformanceSchema()
{
    QSqlQuery q(m_db);
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE performance_log ("
        "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  event_type  TEXT    NOT NULL,"
        "  value_ms    INTEGER,"
        "  value_bytes INTEGER,"
        "  recorded_at TEXT    NOT NULL"
        ");"
    )), qPrintable(q.lastError().text()));
}

int TstPerformanceObserver::countRows(const QString& eventType) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM performance_log WHERE event_type = ?"));
    q.addBindValue(eventType);
    if (!q.exec() || !q.next())
        return -1;
    return q.value(0).toInt();
}

// ── recordColdStart ──────────────────────────────────────────────────────────

void TstPerformanceObserver::test_recordColdStart_insertsRow()
{
    PerformanceObserver observer(m_db);
    observer.recordColdStart(1234);

    QSqlQuery q(m_db);
    QVERIFY(q.exec(QStringLiteral(
        "SELECT event_type, value_ms, value_bytes, recorded_at "
        "FROM performance_log")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("cold_start"));
    QCOMPARE(q.value(1).toLongLong(), qint64{1234});
    QVERIFY(q.value(2).isNull()); // no RSS bytes for cold_start
    QVERIFY(!q.value(3).toString().isEmpty());
    QVERIFY(!q.next()); // only one row
}

void TstPerformanceObserver::test_recordColdStart_withinTargetLogsWithinTargetVerdict()
{
    // Exercises the `withinTarget == true` branch. Observable effect: the row
    // lands in performance_log regardless of verdict; we check that a value
    // strictly below the 3000 ms target writes exactly one cold_start row.
    PerformanceObserver observer(m_db);
    observer.recordColdStart(500);
    QCOMPARE(countRows(QStringLiteral("cold_start")), 1);
}

void TstPerformanceObserver::test_recordColdStart_exceedsTargetLogsExceedsTargetVerdict()
{
    // Exercises the `withinTarget == false` branch (elapsedMs >= 3000 ms).
    PerformanceObserver observer(m_db);
    observer.recordColdStart(4500);
    QCOMPARE(countRows(QStringLiteral("cold_start")), 1);
}

// ── startMemoryObservation / sample timer ────────────────────────────────────

void TstPerformanceObserver::test_startMemoryObservation_recordsAtLeastOneSample()
{
    PerformanceObserver observer(m_db);
    // Very short interval so at least one tick fires within the waitFor window.
    observer.startMemoryObservation(10);

    // Pump the event loop until a memory_sample row appears or we time out.
    QTRY_VERIFY_WITH_TIMEOUT(countRows(QStringLiteral("memory_sample")) >= 1, 2000);

    observer.stopMemoryObservation();
}

void TstPerformanceObserver::test_stopMemoryObservation_writesShutdownEvent()
{
    PerformanceObserver observer(m_db);
    observer.startMemoryObservation(10);
    observer.stopMemoryObservation();
    QCOMPARE(countRows(QStringLiteral("shutdown")), 1);
}

void TstPerformanceObserver::test_stopMemoryObservation_afterStopNoMoreSamples()
{
    PerformanceObserver observer(m_db);
    observer.startMemoryObservation(10);
    // Let at least one sample land.
    QTRY_VERIFY_WITH_TIMEOUT(countRows(QStringLiteral("memory_sample")) >= 1, 2000);
    observer.stopMemoryObservation();

    const int samplesAfterStop = countRows(QStringLiteral("memory_sample"));
    // Pump the event loop a bit more; no further samples should land.
    QTest::qWait(100);
    QCOMPARE(countRows(QStringLiteral("memory_sample")), samplesAfterStop);
}

// ── currentRssBytes ──────────────────────────────────────────────────────────

void TstPerformanceObserver::test_currentRssBytes_returnsNonNegative()
{
    // Platform-dependent — on Linux/Windows this returns a positive value; on
    // unknown platforms it returns 0. Either is acceptable; we only assert it
    // never returns a negative number (would indicate an integer overflow bug).
    const qint64 rss = PerformanceObserver::currentRssBytes();
    QVERIFY(rss >= 0);
}

QTEST_GUILESS_MAIN(TstPerformanceObserver)
#include "tst_performance_observer.moc"
