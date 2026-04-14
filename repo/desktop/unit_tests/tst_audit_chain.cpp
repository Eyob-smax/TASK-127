// tst_audit_chain.cpp — ProctorOps
// Unit tests for AuditService + AuditRepository + HashChain.
// Verifies hash chain linkage, PII encryption in payloads, chain verification,
// and append-only semantics.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>

#include "services/AuditService.h"
#include "repositories/AuditRepository.h"
#include "crypto/AesGcmCipher.h"
#include "crypto/SecureRandom.h"
#include "crypto/HashChain.h"
#include "models/Audit.h"
#include "models/CommonTypes.h"

class TstAuditChain : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ── Chain linkage ────────────────────────────────────────────────────
    void test_firstEntry_previousHashEmpty();
    void test_secondEntry_chainsToFirst();
    void test_entryHash_deterministic();

    // ── Chain verification ───────────────────────────────────────────────
    void test_verifyChain_intact();
    void test_verifyChain_detectsTampering();

    // ── PII encryption ───────────────────────────────────────────────────
    void test_piiEncryptedInPayload();

    // ── Chain head ───────────────────────────────────────────────────────
    void test_chainHead_updatedOnInsert();

private:
    void applyAuditSchema();

    QSqlDatabase m_db;
    int m_dbIndex = 0;
    QByteArray m_testKey;
};

void TstAuditChain::initTestCase()
{
    m_testKey = SecureRandom::generate(32);
    qDebug() << "TstAuditChain: starting test suite";
}

void TstAuditChain::cleanupTestCase()
{
    qDebug() << "TstAuditChain: test suite complete";
}

void TstAuditChain::init()
{
    QString connName = QStringLiteral("tst_audit_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON;"));

    applyAuditSchema();
}

void TstAuditChain::cleanup()
{
    QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstAuditChain::applyAuditSchema()
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
        ");"
    )), qPrintable(q.lastError().text()));

    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE audit_chain_head ("
        "  id INTEGER PRIMARY KEY CHECK (id = 1),"
        "  last_entry_id TEXT,"
        "  last_entry_hash TEXT NOT NULL DEFAULT ''"
        ");"
    )), qPrintable(q.lastError().text()));

    QVERIFY2(q.exec(QStringLiteral(
        "INSERT INTO audit_chain_head (id, last_entry_id, last_entry_hash) VALUES (1, NULL, '');"
    )), qPrintable(q.lastError().text()));
}

// ── Chain linkage tests ──────────────────────────────────────────────────────

void TstAuditChain::test_firstEntry_previousHashEmpty()
{
    AuditRepository repo(m_db);
    AesGcmCipher cipher(m_testKey);
    AuditService service(repo, cipher);

    auto result = service.recordEvent(
        QStringLiteral("user-001"), AuditEventType::Login,
        QStringLiteral("User"), QStringLiteral("user-001"));
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");

    // Retrieve and verify
    AuditFilter filter;
    filter.limit = 10;
    auto entries = repo.queryEntries(filter);
    QVERIFY(entries.isOk());
    QCOMPARE(entries.value().size(), 1);
    QVERIFY(entries.value().first().previousEntryHash.isEmpty());
}

void TstAuditChain::test_secondEntry_chainsToFirst()
{
    AuditRepository repo(m_db);
    AesGcmCipher cipher(m_testKey);
    AuditService service(repo, cipher);

    // Insert first
    auto firstInsert = service.recordEvent(QStringLiteral("user-001"), AuditEventType::Login,
                                           QStringLiteral("User"), QStringLiteral("user-001"));
    QVERIFY2(firstInsert.isOk(), firstInsert.isErr() ? qPrintable(firstInsert.errorMessage()) : "");

    // Insert second
    auto secondInsert = service.recordEvent(QStringLiteral("user-001"), AuditEventType::Logout,
                                            QStringLiteral("Session"), QStringLiteral("sess-001"));
    QVERIFY2(secondInsert.isOk(), secondInsert.isErr() ? qPrintable(secondInsert.errorMessage()) : "");

    AuditFilter filter;
    filter.limit = 10;
    auto entries = repo.queryEntries(filter);
    QVERIFY(entries.isOk());
    QCOMPARE(entries.value().size(), 2);

    const AuditEntry& first = entries.value().at(0);
    const AuditEntry& second = entries.value().at(1);

    // Second entry's previousEntryHash must equal first entry's entryHash
    QCOMPARE(second.previousEntryHash, first.entryHash);
}

void TstAuditChain::test_entryHash_deterministic()
{
    AuditRepository repo(m_db);
    AesGcmCipher cipher(m_testKey);
    AuditService service(repo, cipher);

    auto insert = service.recordEvent(QStringLiteral("user-001"), AuditEventType::UserCreated,
                                      QStringLiteral("User"), QStringLiteral("user-002"));
    QVERIFY2(insert.isOk(), insert.isErr() ? qPrintable(insert.errorMessage()) : "");

    AuditFilter filter;
    filter.limit = 1;
    auto entries = repo.queryEntries(filter);
    QVERIFY(entries.isOk());
    QCOMPARE(entries.value().size(), 1);

    const AuditEntry& entry = entries.value().first();
    // Recompute the hash and verify it matches
    QString recomputed = HashChain::computeEntryHash(entry);
    QCOMPARE(recomputed, entry.entryHash);
}

// ── Chain verification tests ─────────────────────────────────────────────────

void TstAuditChain::test_verifyChain_intact()
{
    AuditRepository repo(m_db);
    AesGcmCipher cipher(m_testKey);
    AuditService service(repo, cipher);

    // Create a chain of 5 events
    for (int i = 0; i < 5; ++i) {
        auto insert = service.recordEvent(QStringLiteral("user-001"),
                                          AuditEventType::Login,
                                          QStringLiteral("User"),
                                          QStringLiteral("user-001"));
        QVERIFY2(insert.isOk(), insert.isErr() ? qPrintable(insert.errorMessage()) : "");
    }

    auto report = service.verifyChain(QStringLiteral("user-001"), 5);
    QVERIFY2(report.isOk(), report.isErr() ? qPrintable(report.errorMessage()) : "");
    QVERIFY(report.value().integrityOk);
    QCOMPARE(report.value().entriesVerified, 5);
    QVERIFY(report.value().firstBrokenEntryId.isEmpty());
}

void TstAuditChain::test_verifyChain_detectsTampering()
{
    AuditRepository repo(m_db);
    AesGcmCipher cipher(m_testKey);
    AuditService service(repo, cipher);

    // Create a chain of 3 events
    for (int i = 0; i < 3; ++i) {
        auto insert = service.recordEvent(QStringLiteral("user-001"),
                                          AuditEventType::Login,
                                          QStringLiteral("User"),
                                          QStringLiteral("user-001"));
        QVERIFY2(insert.isOk(), insert.isErr() ? qPrintable(insert.errorMessage()) : "");
    }

    // Tamper with the second entry directly in the database
    AuditFilter filter;
    filter.limit = 10;
    auto entries = repo.queryEntries(filter);
    QVERIFY(entries.isOk());
    QVERIFY(entries.value().size() >= 2);

    QString secondId = entries.value().at(1).id;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE audit_entries SET after_payload_json = '{\"tampered\":true}' WHERE id = ?"));
    q.addBindValue(secondId);
    QVERIFY(q.exec());

    // Verification should detect the tampering
    auto report = service.verifyChain(QStringLiteral("user-001"), 10);
    QVERIFY(report.isOk());
    QVERIFY(!report.value().integrityOk);
    QCOMPARE(report.value().firstBrokenEntryId, secondId);
}

// ── PII encryption tests ─────────────────────────────────────────────────────

void TstAuditChain::test_piiEncryptedInPayload()
{
    AuditRepository repo(m_db);
    AesGcmCipher cipher(m_testKey);
    AuditService service(repo, cipher);

    QJsonObject afterPayload;
    afterPayload[QStringLiteral("member_id")] = QStringLiteral("MID-001");
    afterPayload[QStringLiteral("mobile")] = QStringLiteral("(555) 123-4567");
    afterPayload[QStringLiteral("barcode")] = QStringLiteral("ABCDE12345");
    afterPayload[QStringLiteral("name")] = QStringLiteral("John Smith");
    afterPayload[QStringLiteral("notes")] = QStringLiteral("Public info"); // not PII

    auto result = service.recordEvent(
        QStringLiteral("user-001"), AuditEventType::UserCreated,
        QStringLiteral("User"), QStringLiteral("user-002"),
        QJsonObject{}, afterPayload);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");

    // Read raw from DB
    AuditFilter filter;
    filter.limit = 1;
    auto entries = repo.queryEntries(filter);
    QVERIFY(entries.isOk());
    QCOMPARE(entries.value().size(), 1);

    const QString& rawAfter = entries.value().first().afterPayloadJson;

    // Parse stored payload
    QJsonDocument doc = QJsonDocument::fromJson(rawAfter.toUtf8());
    QVERIFY(doc.isObject());
    QJsonObject stored = doc.object();

    // PII fields should NOT contain plaintext
    QVERIFY2(stored[QStringLiteral("member_id")].toString() != QStringLiteral("MID-001"),
             "Member ID must be encrypted in audit payload");
    QVERIFY2(stored[QStringLiteral("mobile")].toString() != QStringLiteral("(555) 123-4567"),
             "Mobile must be encrypted in audit payload");
    QVERIFY2(stored[QStringLiteral("barcode")].toString() != QStringLiteral("ABCDE12345"),
             "Barcode must be encrypted in audit payload");
    QVERIFY2(stored[QStringLiteral("name")].toString() != QStringLiteral("John Smith"),
             "Name must be encrypted in audit payload");

    // Non-PII field should be preserved as-is
    QCOMPARE(stored[QStringLiteral("notes")].toString(), QStringLiteral("Public info"));
}

// ── Chain head tests ─────────────────────────────────────────────────────────

void TstAuditChain::test_chainHead_updatedOnInsert()
{
    AuditRepository repo(m_db);
    AesGcmCipher cipher(m_testKey);
    AuditService service(repo, cipher);

    // Initial chain head should be empty
    auto head0 = repo.getChainHeadHash();
    QVERIFY(head0.isOk());
    QVERIFY(head0.value().isEmpty());

    // After first insert
    auto firstInsert = service.recordEvent(QStringLiteral("user-001"), AuditEventType::Login,
                                           QStringLiteral("User"), QStringLiteral("user-001"));
    QVERIFY2(firstInsert.isOk(), firstInsert.isErr() ? qPrintable(firstInsert.errorMessage()) : "");

    auto head1 = repo.getChainHeadHash();
    QVERIFY(head1.isOk());
    QVERIFY(!head1.value().isEmpty());
    QCOMPARE(head1.value().length(), 64); // SHA-256 hex

    // After second insert, head should change
    auto secondInsert = service.recordEvent(QStringLiteral("user-001"), AuditEventType::Logout,
                                            QStringLiteral("Session"), QStringLiteral("sess-001"));
    QVERIFY2(secondInsert.isOk(), secondInsert.isErr() ? qPrintable(secondInsert.errorMessage()) : "");

    auto head2 = repo.getChainHeadHash();
    QVERIFY(head2.isOk());
    QVERIFY(head1.value() != head2.value());
}

QTEST_GUILESS_MAIN(TstAuditChain)
#include "tst_audit_chain.moc"
