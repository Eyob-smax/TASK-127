// tst_audit_service.cpp — ProctorOps
// Focused unit tests for AuditService behavior not covered by chain tests.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonObject>

#include "services/AuditService.h"
#include "repositories/AuditRepository.h"
#include "crypto/AesGcmCipher.h"

class TstAuditService : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void test_record_wrapperDelegatesToRecordEvent();
    void test_verifyChain_respectsLimit();
    void test_purgeAuditEntries_requiresAuthService();

private:
    void applyAuditSchema();

    QSqlDatabase m_db;
    int m_dbIndex = 0;
};

void TstAuditService::init()
{
    const QString connName = QStringLiteral("tst_audit_service_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));

    QSqlQuery q(m_db);
    QVERIFY2(q.exec(QStringLiteral("PRAGMA foreign_keys = ON;")), qPrintable(q.lastError().text()));
    applyAuditSchema();
}

void TstAuditService::cleanup()
{
    const QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstAuditService::applyAuditSchema()
{
    QSqlQuery q(m_db);

    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE audit_entries ("
        "  id TEXT PRIMARY KEY,"
        "  timestamp TEXT NOT NULL,"
        "  actor_user_id TEXT NOT NULL,"
        "  event_type TEXT NOT NULL,"
        "  entity_type TEXT NOT NULL,"
        "  entity_id TEXT NOT NULL,"
        "  before_payload_json TEXT NOT NULL DEFAULT '{}',"
        "  after_payload_json TEXT NOT NULL DEFAULT '{}',"
        "  previous_entry_hash TEXT NOT NULL,"
        "  entry_hash TEXT NOT NULL"
        ")")), qPrintable(q.lastError().text()));

    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE audit_chain_head ("
        "  id INTEGER PRIMARY KEY CHECK (id = 1),"
        "  last_entry_id TEXT,"
        "  last_entry_hash TEXT NOT NULL DEFAULT ''"
        ")")), qPrintable(q.lastError().text()));

    QVERIFY2(q.exec(QStringLiteral(
        "INSERT INTO audit_chain_head (id, last_entry_id, last_entry_hash) VALUES (1, NULL, '')"
    )), qPrintable(q.lastError().text()));
}

void TstAuditService::test_record_wrapperDelegatesToRecordEvent()
{
    AuditRepository repo(m_db);
    QByteArray key(32, 'a');
    AesGcmCipher cipher(key);
    AuditService service(repo, cipher);

    QJsonObject after;
    after[QStringLiteral("member_id")] = QStringLiteral("M-1001");
    after[QStringLiteral("notes")] = QStringLiteral("non-pii");

    auto res = service.record(AuditEventType::UserCreated,
                              QStringLiteral("User"),
                              QStringLiteral("user-1"),
                              QStringLiteral("actor-1"),
                              QJsonObject{},
                              after);
    QVERIFY2(res.isOk(), res.isErr() ? qPrintable(res.errorMessage()) : "");

    AuditFilter filter;
    filter.limit = 10;
    auto rows = repo.queryEntries(filter);
    QVERIFY(rows.isOk());
    QCOMPARE(rows.value().size(), 1);

    const AuditEntry& entry = rows.value().first();
    QCOMPARE(entry.actorUserId, QStringLiteral("actor-1"));
    QCOMPARE(entry.entityType, QStringLiteral("User"));
    QCOMPARE(entry.entityId, QStringLiteral("user-1"));
    QVERIFY(entry.afterPayloadJson.contains(QStringLiteral("notes")));
    QVERIFY(!entry.afterPayloadJson.contains(QStringLiteral("M-1001")));
}

void TstAuditService::test_verifyChain_respectsLimit()
{
    AuditRepository repo(m_db);
    QByteArray key(32, 'b');
    AesGcmCipher cipher(key);
    AuditService service(repo, cipher);

    for (int i = 0; i < 3; ++i) {
        auto res = service.recordEvent(QStringLiteral("actor-1"),
                                       AuditEventType::Login,
                                       QStringLiteral("Session"),
                                       QStringLiteral("sess-%1").arg(i));
        QVERIFY2(res.isOk(), res.isErr() ? qPrintable(res.errorMessage()) : "");
    }

    auto verify = service.verifyChain(QStringLiteral("actor-1"), 2);
    QVERIFY2(verify.isOk(), verify.isErr() ? qPrintable(verify.errorMessage()) : "");
    QVERIFY(verify.value().integrityOk);
    QCOMPARE(verify.value().entriesVerified, 2);
}

void TstAuditService::test_purgeAuditEntries_requiresAuthService()
{
    AuditRepository repo(m_db);
    QByteArray key(32, 'c');
    AesGcmCipher cipher(key);
    AuditService service(repo, cipher);

    auto purge = service.purgeAuditEntries(QStringLiteral("actor-1"),
                                           QStringLiteral("step-up-id"),
                                           QDateTime::currentDateTimeUtc().addYears(-2));
    QVERIFY(purge.isErr());
    QCOMPARE(purge.errorCode(), ErrorCode::InternalError);
}

QTEST_MAIN(TstAuditService)
#include "tst_audit_service.moc"
