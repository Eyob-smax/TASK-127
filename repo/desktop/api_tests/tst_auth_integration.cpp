// tst_auth_integration.cpp — ProctorOps
// Integration tests for full auth flow with real database, real crypto,
// real audit — no mocks. Exercises sign-in, lockout, CAPTCHA, step-up,
// RBAC, and console lock/unlock end-to-end.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#include "services/AuthService.h"
#include "repositories/UserRepository.h"
#include "repositories/AuditRepository.h"
#include "crypto/Argon2idHasher.h"
#include "crypto/SecureRandom.h"
#include "crypto/HashChain.h"
#include "utils/CaptchaGenerator.h"
#include "utils/Validation.h"
#include "models/CommonTypes.h"

class TstAuthIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void test_fullLoginFlow_successAndAudit();
    void test_lockoutFlow_5FailuresThenLocked();
    void test_captchaFlow_requiredAfter3();
    void test_captchaFlow_missingStateFailsClosed();
    void test_stepUpFlow_grantAndConsume();
    void test_securityAdminProvisioning_createAndResetPassword();
    void test_unauthorizedAccess_rbacDenied();
    void test_consoleLockUnlock_fullCycle();

private:
    void applyFullSchema();
    void createTestUser(const QString& userId, const QString& username,
                        const QString& password, Role role);

    QSqlDatabase m_db;
    int m_dbIndex = 0;
    static const QString s_password;
};

const QString TstAuthIntegration::s_password = QStringLiteral("IntegrationPw12!");

void TstAuthIntegration::initTestCase()
{
    qDebug() << "TstAuthIntegration: starting";
}

void TstAuthIntegration::cleanupTestCase()
{
    qDebug() << "TstAuthIntegration: complete";
}

void TstAuthIntegration::init()
{
    QString connName = QStringLiteral("tst_auth_int_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON;"));
    applyFullSchema();
}

void TstAuthIntegration::cleanup()
{
    QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstAuthIntegration::applyFullSchema()
{
    QSqlQuery q(m_db);

    // Identity schema
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

    // Audit schema
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

void TstAuthIntegration::createTestUser(const QString& userId, const QString& username,
                                         const QString& password, Role role)
{
    UserRepository userRepo(m_db);
    User user;
    user.id = userId;
    user.username = username;
    user.role = role;
    user.status = UserStatus::Active;
    user.createdAt = QDateTime::currentDateTimeUtc();
    user.updatedAt = user.createdAt;
    QVERIFY(userRepo.insertUser(user).isOk());

    auto hashResult = Argon2idHasher::hashPassword(password);
    QVERIFY(hashResult.isOk());
    Credential cred = hashResult.value();
    cred.userId = userId;
    QVERIFY(userRepo.upsertCredential(cred).isOk());
}

// ── Full login flow ──────────────────────────────────────────────────────────

void TstAuthIntegration::test_fullLoginFlow_successAndAudit()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_password,
                   Role::SecurityAdministrator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    // Sign in
    auto loginResult = auth.signIn(QStringLiteral("admin"), s_password);
    QVERIFY2(loginResult.isOk(), loginResult.isErr() ? qPrintable(loginResult.errorMessage()) : "");
    QVERIFY(!loginResult.value().token.isEmpty());
    QCOMPARE(loginResult.value().userId, QStringLiteral("u1"));

    // Verify session exists
    auto session = userRepo.findSession(loginResult.value().token);
    QVERIFY(session.isOk());
    QVERIFY(session.value().active);

    // Sign out
    auto logoutResult = auth.signOut(loginResult.value().token);
    QVERIFY(logoutResult.isOk());

    // Session deactivated
    session = userRepo.findSession(loginResult.value().token);
    QVERIFY(session.isOk());
    QVERIFY(!session.value().active);

    // Verify audit entries: Login + Logout
    AuditFilter filter;
    filter.limit = 100;
    auto entries = auditRepo.queryEntries(filter);
    QVERIFY(entries.isOk());

    bool foundLogin = false, foundLogout = false;
    for (const AuditEntry& e : entries.value()) {
        if (e.eventType == AuditEventType::Login) foundLogin = true;
        if (e.eventType == AuditEventType::Logout) foundLogout = true;
    }
    QVERIFY(foundLogin);
    QVERIFY(foundLogout);
}

// ── Lockout flow ─────────────────────────────────────────────────────────────

void TstAuthIntegration::test_lockoutFlow_5FailuresThenLocked()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_password,
                   Role::FrontDeskOperator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    const QString wrongPw = QStringLiteral("WrongPassword12!");

    // Fail 5 times
    for (int i = 0; i < Validation::LockoutFailureThreshold; ++i) {
        auto r = auth.signIn(QStringLiteral("admin"), wrongPw);
        QVERIFY(r.isErr());
    }

    // Correct password must be blocked by the active security gate.
    auto result = auth.signIn(QStringLiteral("admin"), s_password);
    QVERIFY(result.isErr());
    QVERIFY(result.errorCode() == ErrorCode::AccountLocked
        || result.errorCode() == ErrorCode::CaptchaRequired);

    // Verify a lockout/failure security audit event was recorded.
    AuditFilter filter;
    filter.limit = 100;
    auto entries = auditRepo.queryEntries(filter);
    QVERIFY(entries.isOk());
    bool foundLockout = false;
    bool foundFailed = false;
    for (const AuditEntry& e : entries.value()) {
        if (e.eventType == AuditEventType::LoginLocked)
            foundLockout = true;
        if (e.eventType == AuditEventType::LoginFailed)
            foundFailed = true;
    }
    QVERIFY(foundLockout || foundFailed);
}

// ── CAPTCHA flow ─────────────────────────────────────────────────────────────

void TstAuthIntegration::test_captchaFlow_requiredAfter3()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_password,
                   Role::FrontDeskOperator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    // Fail 3 times to trigger CAPTCHA
    for (int i = 0; i < Validation::CaptchaAfterFailures; ++i) {
        const auto failed = auth.signIn(QStringLiteral("admin"), QStringLiteral("WrongPassword12!"));
        QVERIFY(failed.isErr());
    }

    // Correct password without CAPTCHA should require CAPTCHA
    auto result = auth.signIn(QStringLiteral("admin"), s_password);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::CaptchaRequired);

    // Get the CAPTCHA state to get the answer hash
    auto captchaState = userRepo.getCaptchaState(QStringLiteral("admin"));
    QVERIFY(captchaState.isOk());
    QVERIFY(!captchaState.value().answerHashHex.isEmpty());
}

void TstAuthIntegration::test_captchaFlow_missingStateFailsClosed()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_password,
                   Role::FrontDeskOperator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    for (int i = 0; i < Validation::CaptchaAfterFailures; ++i) {
        auto failed = auth.signIn(QStringLiteral("admin"), QStringLiteral("WrongPassword12!"));
        QVERIFY(failed.isErr());
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM captcha_states WHERE username = ?"));
    q.addBindValue(QStringLiteral("admin"));
    QVERIFY(q.exec());

    auto result = auth.signIn(QStringLiteral("admin"), s_password, QStringLiteral("any-answer"));
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::CaptchaRequired);

    auto regenerated = userRepo.getCaptchaState(QStringLiteral("admin"));
    QVERIFY(regenerated.isOk());
}

// ── Step-up flow ─────────────────────────────────────────────────────────────

void TstAuthIntegration::test_stepUpFlow_grantAndConsume()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_password,
                   Role::SecurityAdministrator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    // Login
    auto loginResult = auth.signIn(QStringLiteral("admin"), s_password);
    QVERIFY(loginResult.isOk());
    QString token = loginResult.value().token;

    // Initiate step-up
    auto stepUp = auth.initiateStepUp(token, s_password);
    QVERIFY2(stepUp.isOk(), stepUp.isErr() ? qPrintable(stepUp.errorMessage()) : "");
    QVERIFY(!stepUp.value().consumed);
    QCOMPARE(stepUp.value().userId, QStringLiteral("u1"));

    // Verify expiry is ~2 minutes from now
    qint64 windowSecs = stepUp.value().grantedAt.secsTo(stepUp.value().expiresAt);
    QCOMPARE(windowSecs, static_cast<qint64>(Validation::StepUpWindowSeconds));

    // Consume it
    auto consumeResult = auth.consumeStepUp(stepUp.value().id);
    QVERIFY2(consumeResult.isOk(), consumeResult.isErr() ? qPrintable(consumeResult.errorMessage()) : "");

    // Second consume must fail
    auto secondResult = auth.consumeStepUp(stepUp.value().id);
    QVERIFY(secondResult.isErr());
    QCOMPARE(secondResult.errorCode(), ErrorCode::StepUpRequired);

    // Verify audit events
    AuditFilter filter;
    filter.limit = 100;
    auto entries = auditRepo.queryEntries(filter);
    QVERIFY(entries.isOk());
    bool foundInitiated = false, foundPassed = false;
    for (const AuditEntry& e : entries.value()) {
        if (e.eventType == AuditEventType::StepUpInitiated) foundInitiated = true;
        if (e.eventType == AuditEventType::StepUpPassed) foundPassed = true;
    }
    QVERIFY(foundInitiated);
    QVERIFY(foundPassed);
}

void TstAuthIntegration::test_securityAdminProvisioning_createAndResetPassword()
{
    createTestUser(QStringLiteral("u-admin"), QStringLiteral("admin"), s_password,
                   Role::SecurityAdministrator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto adminSession = auth.signIn(QStringLiteral("admin"), s_password);
    QVERIFY(adminSession.isOk());

    auto createStepUp = auth.initiateStepUp(adminSession.value().token, s_password);
    QVERIFY(createStepUp.isOk());

    auto createResult = auth.createUser(QStringLiteral("u-admin"),
                                        QStringLiteral("new_operator"),
                                        QStringLiteral("TempPass12345!"),
                                        Role::FrontDeskOperator,
                                        createStepUp.value().id);
    QVERIFY2(createResult.isOk(), createResult.isErr() ? qPrintable(createResult.errorMessage()) : "");

    auto created = userRepo.findUserByUsername(QStringLiteral("new_operator"));
    QVERIFY(created.isOk());

    auto resetStepUp = auth.initiateStepUp(adminSession.value().token, s_password);
    QVERIFY(resetStepUp.isOk());

    auto resetResult = auth.resetUserPassword(QStringLiteral("u-admin"),
                                              created.value().id,
                                              QStringLiteral("FreshPass12345!"),
                                              resetStepUp.value().id);
    QVERIFY2(resetResult.isOk(), resetResult.isErr() ? qPrintable(resetResult.errorMessage()) : "");

    auto oldLogin = auth.signIn(QStringLiteral("new_operator"), QStringLiteral("TempPass12345!"));
    QVERIFY(oldLogin.isErr());

    auto newLogin = auth.signIn(QStringLiteral("new_operator"), QStringLiteral("FreshPass12345!"));
    QVERIFY(newLogin.isOk());
}

// ── RBAC denied ──────────────────────────────────────────────────────────────

void TstAuthIntegration::test_unauthorizedAccess_rbacDenied()
{
    createTestUser(QStringLiteral("u-op"), QStringLiteral("operator"), s_password,
                   Role::FrontDeskOperator);
    createTestUser(QStringLiteral("u-cm"), QStringLiteral("content_mgr"), s_password,
                   Role::ContentManager);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    // Operator login
    auto opLogin = auth.signIn(QStringLiteral("operator"), s_password);
    QVERIFY(opLogin.isOk());

    // Operator should NOT have ContentManager access
    auto denied = auth.requireRole(opLogin.value().token, Role::ContentManager);
    QVERIFY(denied.isErr());
    QCOMPARE(denied.errorCode(), ErrorCode::AuthorizationDenied);

    // ContentManager login
    auto cmLogin = auth.signIn(QStringLiteral("content_mgr"), s_password);
    QVERIFY(cmLogin.isOk());

    // ContentManager SHOULD have ContentManager access
    auto allowed = auth.requireRole(cmLogin.value().token, Role::ContentManager);
    QVERIFY2(allowed.isOk(), allowed.isErr() ? qPrintable(allowed.errorMessage()) : "");

    // ContentManager should NOT have SecurityAdministrator access
    auto denied2 = auth.requireRole(cmLogin.value().token, Role::SecurityAdministrator);
    QVERIFY(denied2.isErr());
    QCOMPARE(denied2.errorCode(), ErrorCode::AuthorizationDenied);
}

// ── Console lock/unlock ──────────────────────────────────────────────────────

void TstAuthIntegration::test_consoleLockUnlock_fullCycle()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_password,
                   Role::SecurityAdministrator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    // Login
    auto loginResult = auth.signIn(QStringLiteral("admin"), s_password);
    QVERIFY(loginResult.isOk());
    QString token = loginResult.value().token;

    // Lock console
    QVERIFY(auth.lockConsole(token).isOk());

    // Unlock with wrong password fails
    auto wrongUnlock = auth.unlockConsole(token, QStringLiteral("WrongPassword12!"));
    QVERIFY(wrongUnlock.isErr());
    QCOMPARE(wrongUnlock.errorCode(), ErrorCode::InvalidCredentials);

    // Unlock with correct password succeeds
    auto correctUnlock = auth.unlockConsole(token, s_password);
    QVERIFY2(correctUnlock.isOk(), correctUnlock.isErr() ? qPrintable(correctUnlock.errorMessage()) : "");

    // Verify audit events
    AuditFilter filter;
    filter.limit = 100;
    auto entries = auditRepo.queryEntries(filter);
    QVERIFY(entries.isOk());
    bool foundLock = false, foundUnlock = false;
    for (const AuditEntry& e : entries.value()) {
        if (e.eventType == AuditEventType::ConsoleLocked) foundLock = true;
        if (e.eventType == AuditEventType::ConsoleUnlocked) foundUnlock = true;
    }
    QVERIFY(foundLock);
    QVERIFY(foundUnlock);
}

QTEST_MAIN(TstAuthIntegration)
#include "tst_auth_integration.moc"
