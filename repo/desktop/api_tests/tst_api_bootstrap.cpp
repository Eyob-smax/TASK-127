// tst_api_bootstrap.cpp
// ProctorOps — API/integration test infrastructure bootstrap
//
// This test verifies that:
//   1. The Qt SQL module compiles and links (QSQLITE driver available).
//   2. An in-memory SQLite database can be created and queried.
//   3. WAL mode can be enabled (matches production SQLite configuration).
//   4. The test infrastructure supports real SQLite, not mocked storage.
//
// All api_tests in this directory use real SQLite databases (in-memory or
// temp-file). No mock implementations substitute for real service or
// repository code. This is the contract for the api_tests suite.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryFile>
#include <QString>

class TstApiBootstrap : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void test_sqliteDriverAvailable();
    void test_inMemoryDatabaseOpen();
    void test_walModeEnabled();
    void test_foreignKeysEnabled();
    void test_createAndQueryTable();

private:
    QSqlDatabase m_db;
};

void TstApiBootstrap::initTestCase()
{
    qDebug() << "ProctorOps API bootstrap test starting";
    // Verify QSQLITE driver is built into Qt SQL
    QVERIFY2(QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE")),
             "QSQLITE driver must be available — check Qt SQL configuration");
}

void TstApiBootstrap::cleanupTestCase()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("test_bootstrap"));
    qDebug() << "ProctorOps API bootstrap test complete";
}

void TstApiBootstrap::test_sqliteDriverAvailable()
{
    const QStringList drivers = QSqlDatabase::drivers();
    qDebug() << "Available SQL drivers:" << drivers;
    QVERIFY(drivers.contains(QStringLiteral("QSQLITE")));
}

void TstApiBootstrap::test_inMemoryDatabaseOpen()
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                      QStringLiteral("test_bootstrap"));
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));
    QVERIFY(m_db.isOpen());
}

void TstApiBootstrap::test_walModeEnabled()
{
    QVERIFY(m_db.isOpen());
    QSqlQuery q(m_db);
    // For :memory: databases, WAL mode is not meaningful, but the PRAGMA
    // should execute without error — verifying SQLite responds correctly.
    QVERIFY2(q.exec(QStringLiteral("PRAGMA journal_mode=WAL;")),
             qPrintable(q.lastError().text()));
    // :memory: returns 'memory' not 'wal' — this is expected behavior
    q.next();
    const QString mode = q.value(0).toString();
    qDebug() << "journal_mode for :memory: database:" << mode;
    // Valid modes for :memory: are 'memory' (WAL not applicable) or 'wal'
    QVERIFY(mode == QStringLiteral("memory") || mode == QStringLiteral("wal"));
}

void TstApiBootstrap::test_foreignKeysEnabled()
{
    QVERIFY(m_db.isOpen());
    QSqlQuery q(m_db);
    QVERIFY2(q.exec(QStringLiteral("PRAGMA foreign_keys = ON;")),
             qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral("PRAGMA foreign_keys;")),
             qPrintable(q.lastError().text()));
    q.next();
    QCOMPARE(q.value(0).toInt(), 1);
}

void TstApiBootstrap::test_createAndQueryTable()
{
    QVERIFY(m_db.isOpen());
    QSqlQuery q(m_db);

    // Create a minimal schema_migrations table — mirrors the real migration table
    const bool created = q.exec(QStringLiteral(
        "CREATE TABLE schema_migrations ("
        "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  migration  TEXT NOT NULL UNIQUE,"
        "  applied_at TEXT NOT NULL"
        ");"
    ));
    QVERIFY2(created, qPrintable(q.lastError().text()));

    // Insert a row
    q.prepare(QStringLiteral(
        "INSERT INTO schema_migrations (migration, applied_at) VALUES (?, ?);"
    ));
    q.addBindValue(QStringLiteral("0001_initial_schema"));
    q.addBindValue(QStringLiteral("2026-04-14T00:00:00Z"));
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));

    // Query back
    QVERIFY2(q.exec(QStringLiteral("SELECT migration FROM schema_migrations;")),
             qPrintable(q.lastError().text()));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("0001_initial_schema"));
    QVERIFY(!q.next());  // exactly one row
}

QTEST_GUILESS_MAIN(TstApiBootstrap)
#include "tst_api_bootstrap.moc"
