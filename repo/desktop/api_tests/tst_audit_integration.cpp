// tst_audit_integration.cpp — ProctorOps
// Integration tests for audit chain integrity across multiple operations.
// Verifies chain linkage, tamper detection, PII encryption, and secret-safe logging.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>

#include "services/AuditService.h"
#include "services/AuthService.h"
#include "repositories/AuditRepository.h"
#include "repositories/UserRepository.h"
#include "crypto/AesGcmCipher.h"
#include "crypto/SecureRandom.h"
#include "crypto/HashChain.h"
#include "crypto/Argon2idHasher.h"
#include "utils/Logger.h"
#include "utils/Validation.h"
#include "models/CommonTypes.h"

class TstAuditIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void test_chainIntegrity_multipleEvents();
    void test_chainVerify_detectsModifiedEntry();
    void test_piiNeverInPlaintext();
    void test_secretSafeLogging();

    // Authorization boundary: queryEvents enforces SecurityAdministrator role
    void test_queryEvents_deniedForFrontDesk();
    void test_queryEvents_allowedForSecurityAdmin();

private:
    void applyFullSchema();

    QSqlDatabase m_db;
    int m_dbIndex = 0;
    QByteArray m_masterKey;
};

void TstAuditIntegration::initTestCase()
{
    m_masterKey = SecureRandom::generate(32);
    qDebug() << "TstAuditIntegration: starting";
}

void TstAuditIntegration::cleanupTestCase()
{
    qDebug() << "TstAuditIntegration: complete";
}

void TstAuditIntegration::init()
{
    QString connName = QStringLiteral("tst_audit_int_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON;"));
    applyFullSchema();
}

void TstAuditIntegration::cleanup()
{
    QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstAuditIntegration::applyFullSchema()
{
    QSqlQuery q(m_db);

    // Identity tables (needed for auth tests that feed audit)
    q.exec(QStringLiteral(
        "CREATE TABLE users ("
        "  id TEXT PRIMARY KEY, username TEXT NOT NULL UNIQUE,"
        "  role TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'Active',"
        "  created_at TEXT NOT NULL, updated_at TEXT NOT NULL,"
        "  created_by_user_id TEXT)"));
    q.exec(QStringLiteral(
        "CREATE TABLE credentials ("
        "  user_id TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,"
        "  algorithm TEXT NOT NULL DEFAULT 'argon2id',"
        "  time_cost INTEGER NOT NULL, memory_cost INTEGER NOT NULL,"
        "  parallelism INTEGER NOT NULL, tag_length INTEGER NOT NULL,"
        "  salt_hex TEXT NOT NULL, hash_hex TEXT NOT NULL, updated_at TEXT NOT NULL)"));
    q.exec(QStringLiteral(
        "CREATE TABLE user_sessions ("
        "  token TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
        "  created_at TEXT NOT NULL, last_active_at TEXT NOT NULL,"
        "  active INTEGER NOT NULL DEFAULT 1)"));
    q.exec(QStringLiteral(
        "CREATE TABLE lockout_records ("
        "  username TEXT PRIMARY KEY, failed_attempts INTEGER NOT NULL DEFAULT 0,"
        "  first_fail_at TEXT, locked_at TEXT)"));
    q.exec(QStringLiteral(
        "CREATE TABLE captcha_states ("
        "  username TEXT PRIMARY KEY, challenge_id TEXT NOT NULL,"
        "  answer_hash_hex TEXT NOT NULL, issued_at TEXT NOT NULL,"
        "  expires_at TEXT NOT NULL, solve_attempts INTEGER NOT NULL DEFAULT 0,"
        "  solved INTEGER NOT NULL DEFAULT 0)"));
    q.exec(QStringLiteral(
        "CREATE TABLE step_up_windows ("
        "  id TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
        "  session_token TEXT NOT NULL REFERENCES user_sessions(token) ON DELETE CASCADE,"
        "  granted_at TEXT NOT NULL, expires_at TEXT NOT NULL,"
        "  consumed INTEGER NOT NULL DEFAULT 0)"));

    // Audit tables
    q.exec(QStringLiteral(
        "CREATE TABLE audit_entries ("
        "  id TEXT PRIMARY KEY, timestamp TEXT NOT NULL,"
        "  actor_user_id TEXT NOT NULL, event_type TEXT NOT NULL,"
        "  entity_type TEXT NOT NULL, entity_id TEXT NOT NULL,"
        "  before_payload_json TEXT NOT NULL DEFAULT '{}',"
        "  after_payload_json TEXT NOT NULL DEFAULT '{}',"
        "  previous_entry_hash TEXT NOT NULL, entry_hash TEXT NOT NULL)"));
    q.exec(QStringLiteral(
        "CREATE TABLE audit_chain_head ("
        "  id INTEGER PRIMARY KEY CHECK (id = 1),"
        "  last_entry_id TEXT, last_entry_hash TEXT NOT NULL DEFAULT '')"));
    q.exec(QStringLiteral(
        "INSERT INTO audit_chain_head (id, last_entry_id, last_entry_hash) VALUES (1, NULL, '')"));
}

// ── Chain integrity with multiple events ─────────────────────────────────────

void TstAuditIntegration::test_chainIntegrity_multipleEvents()
{
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(m_masterKey);
    AuditService service(auditRepo, cipher);

    // Write 10 events
    for (int i = 0; i < 10; ++i) {
        auto result = service.recordEvent(
            QStringLiteral("user-%1").arg(i % 3),
            (i % 2 == 0) ? AuditEventType::Login : AuditEventType::Logout,
            QStringLiteral("User"),
            QStringLiteral("entity-%1").arg(i));
        QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    }

    // Verify chain (no AuthService wired — role check skipped in unit context)
    auto report = service.verifyChain(QStringLiteral("user-0"), 100);
    QVERIFY2(report.isOk(), report.isErr() ? qPrintable(report.errorMessage()) : "");
    QVERIFY(report.value().integrityOk);
    QCOMPARE(report.value().entriesVerified, 10);

    // Verify linkage manually
    AuditFilter filter;
    filter.limit = 100;
    auto entries = auditRepo.queryEntries(filter);
    QVERIFY(entries.isOk());
    QCOMPARE(entries.value().size(), 10);

    // First entry has empty previous hash
    QVERIFY(entries.value().first().previousEntryHash.isEmpty());

    // Each subsequent entry chains to the previous
    for (int i = 1; i < entries.value().size(); ++i) {
        QCOMPARE(entries.value().at(i).previousEntryHash,
                 entries.value().at(i - 1).entryHash);
    }
}

// ── Tamper detection ─────────────────────────────────────────────────────────

void TstAuditIntegration::test_chainVerify_detectsModifiedEntry()
{
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(m_masterKey);
    AuditService service(auditRepo, cipher);

    for (int i = 0; i < 5; ++i) {
        const auto recordResult = service.recordEvent(QStringLiteral("user-001"), AuditEventType::Login,
                                                      QStringLiteral("User"), QStringLiteral("user-001"));
        QVERIFY2(recordResult.isOk(), recordResult.isErr() ? qPrintable(recordResult.errorMessage()) : "");
    }

    // Tamper with the 3rd entry's actor_user_id
    AuditFilter filter;
    filter.limit = 100;
    auto entries = auditRepo.queryEntries(filter);
    QVERIFY(entries.isOk());
    QVERIFY(entries.value().size() >= 3);

    QString thirdId = entries.value().at(2).id;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE audit_entries SET actor_user_id = 'TAMPERED' WHERE id = ?"));
    q.addBindValue(thirdId);
    QVERIFY(q.exec());

    // Chain verification should detect the tamper
    auto report = service.verifyChain(QStringLiteral("user-001"), 100);
    QVERIFY(report.isOk());
    QVERIFY(!report.value().integrityOk);
    QCOMPARE(report.value().firstBrokenEntryId, thirdId);
    // Only entries before the tamper should be verified
    QCOMPARE(report.value().entriesVerified, 2);
}

// ── PII never in plaintext ───────────────────────────────────────────────────

void TstAuditIntegration::test_piiNeverInPlaintext()
{
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(m_masterKey);
    AuditService service(auditRepo, cipher);

    const QString mobilePlain = QStringLiteral("(555) 123-4567");
    const QString barcodePlain = QStringLiteral("MEMBER12345678");
    const QString namePlain = QStringLiteral("Jane Doe");

    QJsonObject after;
    after[QStringLiteral("mobile")] = mobilePlain;
    after[QStringLiteral("barcode")] = barcodePlain;
    after[QStringLiteral("name")] = namePlain;
    after[QStringLiteral("status")] = QStringLiteral("Active");

    service.recordEvent(QStringLiteral("user-001"), AuditEventType::UserCreated,
                        QStringLiteral("Member"), QStringLiteral("member-001"),
                        QJsonObject{}, after);

    // Read raw from database to inspect stored values
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT after_payload_json FROM audit_entries LIMIT 1"));
    QVERIFY(q.next());
    QString rawPayload = q.value(0).toString();

    // PII values must NOT appear as plaintext in stored payload
    QVERIFY2(!rawPayload.contains(mobilePlain),
             "Mobile number must not be stored as plaintext in audit");
    QVERIFY2(!rawPayload.contains(barcodePlain),
             "Barcode must not be stored as plaintext in audit");
    QVERIFY2(!rawPayload.contains(namePlain),
             "Name must not be stored as plaintext in audit");

    // Non-PII fields should still be present
    QVERIFY(rawPayload.contains(QStringLiteral("Active")));
}

// ── Secret-safe logging ──────────────────────────────────────────────────────

void TstAuditIntegration::test_secretSafeLogging()
{
    // Set up Logger to write to a temp file
    QTemporaryFile logFile;
    logFile.setAutoRemove(true);
    QVERIFY(logFile.open());
    QString logPath = logFile.fileName();
    logFile.close();

    Logger::instance().setOutputPath(logPath);

    // Create user and trigger auth failure to generate log entries
    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);

    User user;
    user.id = QStringLiteral("u1");
    user.username = QStringLiteral("testuser");
    user.role = Role::FrontDeskOperator;
    user.status = UserStatus::Active;
    user.createdAt = QDateTime::currentDateTimeUtc();
    user.updatedAt = user.createdAt;
    userRepo.insertUser(user);

    const QString testPassword = QStringLiteral("SecretPassword12!");
    auto hashResult = Argon2idHasher::hashPassword(testPassword);
    QVERIFY(hashResult.isOk());
    Credential cred = hashResult.value();
    cred.userId = QStringLiteral("u1");
    userRepo.upsertCredential(cred);

    AuthService auth(userRepo, auditRepo);

    // Trigger a login failure (generates security log)
    const auto failedLogin = auth.signIn(QStringLiteral("testuser"), QStringLiteral("WrongPassword12!"));
    QVERIFY(failedLogin.isErr());
    // Also trigger a successful login
    const auto successfulLogin = auth.signIn(QStringLiteral("testuser"), testPassword);
    QVERIFY(successfulLogin.isOk());

    // Read the log file
    QFile file(logPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString logContent = QString::fromUtf8(file.readAll());
    file.close();

    // Log must NOT contain raw passwords
    QVERIFY2(!logContent.contains(QStringLiteral("SecretPassword12!")),
             "Logs must never contain raw passwords");
    QVERIFY2(!logContent.contains(QStringLiteral("WrongPassword12!")),
             "Logs must never contain attempted passwords");

    // Log must NOT contain raw credential hashes or salts
    QVERIFY2(!logContent.contains(cred.hashHex),
             "Logs must never contain raw hash values");
    QVERIFY2(!logContent.contains(cred.saltHex),
             "Logs must never contain raw salt values");

    // Reset logger to stderr
    Logger::instance().setOutputPath(QString());
}

// ── Authorization boundary: queryEvents requires SecurityAdministrator ────────

void TstAuditIntegration::test_queryEvents_deniedForFrontDesk()
{
    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(m_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);

    auditSvc.setAuthService(&authSvc);

    // Create a FrontDeskOperator user with a valid credential
    User fdUser;
    fdUser.id        = QStringLiteral("fd-user-01");
    fdUser.username  = QStringLiteral("frontdesk01");
    fdUser.role      = Role::FrontDeskOperator;
    fdUser.status    = UserStatus::Active;
    fdUser.createdAt = QDateTime::currentDateTimeUtc();
    fdUser.updatedAt = fdUser.createdAt;
    QVERIFY(userRepo.insertUser(fdUser).isOk());

    auto hashResult = Argon2idHasher::hashPassword(QStringLiteral("FdPass12345!"));
    QVERIFY(hashResult.isOk());
    Credential cred = hashResult.value();
    cred.userId = fdUser.id;
    QVERIFY(userRepo.upsertCredential(cred).isOk());

    AuditFilter filter;
    filter.limit  = 10;
    filter.offset = 0;

    auto result = auditSvc.queryEvents(fdUser.id, filter);
    QVERIFY2(result.isErr(), "FrontDeskOperator must be denied audit log access");
    QCOMPARE(result.errorCode(), ErrorCode::AuthorizationDenied);
}

void TstAuditIntegration::test_queryEvents_allowedForSecurityAdmin()
{
    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(m_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);

    auditSvc.setAuthService(&authSvc);

    // Create a SecurityAdministrator user
    User saUser;
    saUser.id        = QStringLiteral("sa-user-01");
    saUser.username  = QStringLiteral("secadmin01");
    saUser.role      = Role::SecurityAdministrator;
    saUser.status    = UserStatus::Active;
    saUser.createdAt = QDateTime::currentDateTimeUtc();
    saUser.updatedAt = saUser.createdAt;
    QVERIFY(userRepo.insertUser(saUser).isOk());

    // Record a test event so the query has something to return
    auditSvc.recordEvent(saUser.id, AuditEventType::Login,
                         QStringLiteral("User"), saUser.id);

    AuditFilter filter;
    filter.limit  = 10;
    filter.offset = 0;

    auto result = auditSvc.queryEvents(saUser.id, filter);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QVERIFY(result.value().size() >= 1);
}

QTEST_GUILESS_MAIN(TstAuditIntegration)
#include "tst_audit_integration.moc"
