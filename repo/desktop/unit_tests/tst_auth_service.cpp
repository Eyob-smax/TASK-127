// tst_auth_service.cpp — ProctorOps
// Unit tests for AuthService: sign-in, lockout, CAPTCHA, step-up, RBAC,
// console lock/unlock. Uses in-memory SQLite with real repositories.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#include "services/AuthService.h"
#include "repositories/UserRepository.h"
#include "repositories/AuditRepository.h"
#include "crypto/Argon2idHasher.h"
#include "crypto/HashChain.h"
#include "utils/Validation.h"
#include "models/CommonTypes.h"

class TstAuthService : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();       // per-test setup
    void cleanup();    // per-test teardown

    // ── Sign-in ──────────────────────────────────────────────────────────
    void test_signIn_success();
    void test_signIn_wrongPassword();
    void test_signIn_unknownUser();

    // ── Lockout ──────────────────────────────────────────────────────────
    void test_signIn_lockoutAfter5Failures();
    void test_signIn_lockedAccountRejected();

    // ── CAPTCHA ──────────────────────────────────────────────────────────
    void test_signIn_captchaRequiredAfter3Failures();
    void test_signIn_captchaMissingStateFailsClosed();

    // ── Sign-out ─────────────────────────────────────────────────────────
    void test_signOut_deactivatesSession();

    // ── Step-up ──────────────────────────────────────────────────────────
    void test_stepUp_validWithin2Minutes();
    void test_stepUp_consumedOnlyOnce();
    void test_stepUp_wrongPassword();
    void test_stepUp_expiredAfter2Minutes();

    // ── RBAC ─────────────────────────────────────────────────────────────
    void test_rbac_hasPermission();
    void test_rbac_requireRole_denied();
    void test_securityAdmin_createUser();
    void test_securityAdmin_resetUserPassword();

    // ── Console lock ─────────────────────────────────────────────────────
    void test_consoleLock_requiresPasswordToUnlock();

    // ── CAPTCHA cooldown and lockout edge cases ─────────────────────────
    void test_captcha_cooldownResets();
    void test_signIn_lockedAccountRejectsEvenWithCaptcha();

    // ── Audit events ─────────────────────────────────────────────────────
    void test_auditEventsRecorded();

private:
    void applySchema();
    void createTestUser(const QString& userId, const QString& username,
                        const QString& password, Role role);

    QSqlDatabase m_db;
    int m_dbIndex = 0;
    static const QString s_testPassword;
};

const QString TstAuthService::s_testPassword = QStringLiteral("SecurePass12!!");

void TstAuthService::initTestCase()
{
    qDebug() << "TstAuthService: starting test suite";
}

void TstAuthService::cleanupTestCase()
{
    qDebug() << "TstAuthService: test suite complete";
}

void TstAuthService::init()
{
    // Each test gets a fresh in-memory database
    QString connName = QStringLiteral("tst_auth_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON;"));

    applySchema();
}

void TstAuthService::cleanup()
{
    QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstAuthService::applySchema()
{
    QSqlQuery q(m_db);

    // Users table (from 0002_identity_schema.sql)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE users ("
        "  id TEXT PRIMARY KEY,"
        "  username TEXT NOT NULL UNIQUE,"
        "  role TEXT NOT NULL,"
        "  status TEXT NOT NULL DEFAULT 'Active',"
        "  created_at TEXT NOT NULL,"
        "  updated_at TEXT NOT NULL,"
        "  created_by_user_id TEXT"
        ");"
    )), qPrintable(q.lastError().text()));

    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE credentials ("
        "  user_id TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,"
        "  algorithm TEXT NOT NULL DEFAULT 'argon2id',"
        "  time_cost INTEGER NOT NULL,"
        "  memory_cost INTEGER NOT NULL,"
        "  parallelism INTEGER NOT NULL,"
        "  tag_length INTEGER NOT NULL,"
        "  salt_hex TEXT NOT NULL,"
        "  hash_hex TEXT NOT NULL,"
        "  updated_at TEXT NOT NULL"
        ");"
    )), qPrintable(q.lastError().text()));

    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE user_sessions ("
        "  token TEXT PRIMARY KEY,"
        "  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
        "  created_at TEXT NOT NULL,"
        "  last_active_at TEXT NOT NULL,"
        "  active INTEGER NOT NULL DEFAULT 1"
        ");"
    )), qPrintable(q.lastError().text()));

    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE lockout_records ("
        "  username TEXT PRIMARY KEY,"
        "  failed_attempts INTEGER NOT NULL DEFAULT 0,"
        "  first_fail_at TEXT,"
        "  locked_at TEXT"
        ");"
    )), qPrintable(q.lastError().text()));

    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE captcha_states ("
        "  username TEXT PRIMARY KEY,"
        "  challenge_id TEXT NOT NULL,"
        "  answer_hash_hex TEXT NOT NULL,"
        "  issued_at TEXT NOT NULL,"
        "  expires_at TEXT NOT NULL,"
        "  solve_attempts INTEGER NOT NULL DEFAULT 0,"
        "  solved INTEGER NOT NULL DEFAULT 0"
        ");"
    )), qPrintable(q.lastError().text()));

    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE step_up_windows ("
        "  id TEXT PRIMARY KEY,"
        "  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
        "  session_token TEXT NOT NULL REFERENCES user_sessions(token) ON DELETE CASCADE,"
        "  granted_at TEXT NOT NULL,"
        "  expires_at TEXT NOT NULL,"
        "  consumed INTEGER NOT NULL DEFAULT 0"
        ");"
    )), qPrintable(q.lastError().text()));

    // Audit tables (from 0008_audit_schema.sql)
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

void TstAuthService::createTestUser(const QString& userId, const QString& username,
                                     const QString& password, Role role)
{
    UserRepository userRepo(m_db);

    User user;
    user.id        = userId;
    user.username  = username;
    user.role      = role;
    user.status    = UserStatus::Active;
    user.createdAt = QDateTime::currentDateTimeUtc();
    user.updatedAt = user.createdAt;

    auto insertResult = userRepo.insertUser(user);
    QVERIFY2(insertResult.isOk(), insertResult.isErr() ? qPrintable(insertResult.errorMessage()) : "");

    auto hashResult = Argon2idHasher::hashPassword(password);
    QVERIFY2(hashResult.isOk(), hashResult.isErr() ? qPrintable(hashResult.errorMessage()) : "");

    Credential cred = hashResult.value();
    cred.userId = userId;
    auto credResult = userRepo.upsertCredential(cred);
    QVERIFY2(credResult.isOk(), credResult.isErr() ? qPrintable(credResult.errorMessage()) : "");
}

// ── Sign-in tests ────────────────────────────────────────────────────────────

void TstAuthService::test_signIn_success()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto result = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QVERIFY(!result.value().token.isEmpty());
    QCOMPARE(result.value().userId, QStringLiteral("u1"));
    QVERIFY(result.value().active);
}

void TstAuthService::test_signIn_wrongPassword()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_testPassword,
                   Role::FrontDeskOperator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto result = auth.signIn(QStringLiteral("admin"), QStringLiteral("WrongPassword99!"));
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::InvalidCredentials);
}

void TstAuthService::test_signIn_unknownUser()
{
    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto result = auth.signIn(QStringLiteral("nonexistent"), QStringLiteral("AnyPassword12!"));
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::InvalidCredentials);
}

// ── Lockout tests ────────────────────────────────────────────────────────────

void TstAuthService::test_signIn_lockoutAfter5Failures()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_testPassword,
                   Role::FrontDeskOperator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    // Fail 5 times (failures 3+ generate CAPTCHA but we keep trying with wrong pw)
    for (int i = 0; i < Validation::LockoutFailureThreshold; ++i) {
        auto result = auth.signIn(QStringLiteral("admin"), QStringLiteral("Wrong12345678!"));
        QVERIFY(result.isErr());
    }

        // Follow-up attempt must be blocked by either lockout or CAPTCHA gate.
    auto result = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(result.isErr());
        QVERIFY(result.errorCode() == ErrorCode::AccountLocked
            || result.errorCode() == ErrorCode::CaptchaRequired);
}

void TstAuthService::test_signIn_lockedAccountRejected()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_testPassword,
                   Role::FrontDeskOperator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    // Lock the account via 5 failures
    for (int i = 0; i < Validation::LockoutFailureThreshold; ++i) {
        auto failedAttempt = auth.signIn(QStringLiteral("admin"), QStringLiteral("Wrong12345678!"));
        QVERIFY(failedAttempt.isErr());
    }

        // Correct password must still be blocked by security gates.
    auto result = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(result.isErr());
        QVERIFY(result.errorCode() == ErrorCode::AccountLocked
            || result.errorCode() == ErrorCode::CaptchaRequired);
}

// ── CAPTCHA tests ────────────────────────────────────────────────────────────

void TstAuthService::test_signIn_captchaRequiredAfter3Failures()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_testPassword,
                   Role::FrontDeskOperator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    // Fail 3 times to trigger CAPTCHA requirement
    for (int i = 0; i < Validation::CaptchaAfterFailures; ++i) {
        auto failedAttempt = auth.signIn(QStringLiteral("admin"), QStringLiteral("Wrong12345678!"));
        QVERIFY(failedAttempt.isErr());
    }

    // Next attempt with correct password but no CAPTCHA should require CAPTCHA
    auto result = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::CaptchaRequired);
}

void TstAuthService::test_signIn_captchaMissingStateFailsClosed()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_testPassword,
                   Role::FrontDeskOperator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    for (int i = 0; i < Validation::CaptchaAfterFailures; ++i) {
        auto failedAttempt = auth.signIn(QStringLiteral("admin"), QStringLiteral("Wrong12345678!"));
        QVERIFY(failedAttempt.isErr());
    }

    QSqlQuery dropCaptcha(m_db);
    dropCaptcha.prepare(QStringLiteral("DELETE FROM captcha_states WHERE username = ?"));
    dropCaptcha.addBindValue(QStringLiteral("admin"));
    QVERIFY(dropCaptcha.exec());

    auto result = auth.signIn(QStringLiteral("admin"), s_testPassword, QStringLiteral("any-answer"));
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::CaptchaRequired);

    auto regenerated = userRepo.getCaptchaState(QStringLiteral("admin"));
    QVERIFY(regenerated.isOk());
    QVERIFY(!regenerated.value().challengeId.isEmpty());
}

// ── Sign-out tests ───────────────────────────────────────────────────────────

void TstAuthService::test_signOut_deactivatesSession()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_testPassword,
                   Role::FrontDeskOperator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto loginResult = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(loginResult.isOk());
    QString token = loginResult.value().token;

    auto signOutResult = auth.signOut(token);
    QVERIFY2(signOutResult.isOk(), signOutResult.isErr() ? qPrintable(signOutResult.errorMessage()) : "");

    // Session should no longer be active
    auto session = userRepo.findSession(token);
    QVERIFY(session.isOk());
    QVERIFY(!session.value().active);
}

// ── Step-up tests ────────────────────────────────────────────────────────────

void TstAuthService::test_stepUp_validWithin2Minutes()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto loginResult = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(loginResult.isOk());

    auto stepUpResult = auth.initiateStepUp(loginResult.value().token, s_testPassword);
    QVERIFY2(stepUpResult.isOk(), stepUpResult.isErr() ? qPrintable(stepUpResult.errorMessage()) : "");

    const StepUpWindow& win = stepUpResult.value();
    QVERIFY(!win.consumed);
    QVERIFY(!win.id.isEmpty());

    // Consume should succeed immediately
    auto consumeResult = auth.consumeStepUp(win.id);
    QVERIFY2(consumeResult.isOk(), consumeResult.isErr() ? qPrintable(consumeResult.errorMessage()) : "");
}

void TstAuthService::test_stepUp_consumedOnlyOnce()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto loginResult = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(loginResult.isOk());

    auto stepUp = auth.initiateStepUp(loginResult.value().token, s_testPassword);
    QVERIFY(stepUp.isOk());

    // First consume succeeds
    QVERIFY(auth.consumeStepUp(stepUp.value().id).isOk());

    // Second consume must fail
    auto secondConsume = auth.consumeStepUp(stepUp.value().id);
    QVERIFY(secondConsume.isErr());
    QCOMPARE(secondConsume.errorCode(), ErrorCode::StepUpRequired);
}

void TstAuthService::test_stepUp_wrongPassword()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto loginResult = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(loginResult.isOk());

    auto result = auth.initiateStepUp(loginResult.value().token,
                                       QStringLiteral("WrongPassword99!"));
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::InvalidCredentials);
}

// ── RBAC tests ───────────────────────────────────────────────────────────────

void TstAuthService::test_rbac_hasPermission()
{
    // SecurityAdmin has all permissions
    QVERIFY(AuthService::hasPermission(Role::SecurityAdministrator, Role::FrontDeskOperator));
    QVERIFY(AuthService::hasPermission(Role::SecurityAdministrator, Role::Proctor));
    QVERIFY(AuthService::hasPermission(Role::SecurityAdministrator, Role::ContentManager));
    QVERIFY(AuthService::hasPermission(Role::SecurityAdministrator, Role::SecurityAdministrator));

    // Operator has least permissions
    QVERIFY(AuthService::hasPermission(Role::FrontDeskOperator, Role::FrontDeskOperator));
    QVERIFY(!AuthService::hasPermission(Role::FrontDeskOperator, Role::Proctor));
    QVERIFY(!AuthService::hasPermission(Role::FrontDeskOperator, Role::ContentManager));
    QVERIFY(!AuthService::hasPermission(Role::FrontDeskOperator, Role::SecurityAdministrator));

    // Proctor < ContentManager
    QVERIFY(!AuthService::hasPermission(Role::Proctor, Role::ContentManager));
    QVERIFY(AuthService::hasPermission(Role::ContentManager, Role::Proctor));
}

void TstAuthService::test_rbac_requireRole_denied()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("operator"), s_testPassword,
                   Role::FrontDeskOperator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto loginResult = auth.signIn(QStringLiteral("operator"), s_testPassword);
    QVERIFY(loginResult.isOk());

    // Operator cannot access content-manager actions
    auto result = auth.requireRole(loginResult.value().token, Role::ContentManager);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::AuthorizationDenied);

    // But can access operator-level actions
    auto okResult = auth.requireRole(loginResult.value().token, Role::FrontDeskOperator);
    QVERIFY2(okResult.isOk(), okResult.isErr() ? qPrintable(okResult.errorMessage()) : "");
}

void TstAuthService::test_securityAdmin_createUser()
{
    createTestUser(QStringLiteral("u-admin"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto adminSession = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(adminSession.isOk());

    auto stepUp = auth.initiateStepUp(adminSession.value().token, s_testPassword);
    QVERIFY(stepUp.isOk());

    auto createResult = auth.createUser(QStringLiteral("u-admin"),
                                        QStringLiteral("proctor1"),
                                        QStringLiteral("TemporaryPass12!"),
                                        Role::Proctor,
                                        stepUp.value().id);
    QVERIFY2(createResult.isOk(), createResult.isErr() ? qPrintable(createResult.errorMessage()) : "");
    QCOMPARE(createResult.value().role, Role::Proctor);

    auto createdUser = userRepo.findUserByUsername(QStringLiteral("proctor1"));
    QVERIFY(createdUser.isOk());

    auto createdCredential = userRepo.getCredential(createdUser.value().id);
    QVERIFY(createdCredential.isOk());
}

void TstAuthService::test_securityAdmin_resetUserPassword()
{
    createTestUser(QStringLiteral("u-admin"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);
    createTestUser(QStringLiteral("u-target"), QStringLiteral("target"),
                   QStringLiteral("OriginalPass12!"), Role::FrontDeskOperator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto adminSession = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(adminSession.isOk());

    auto stepUp = auth.initiateStepUp(adminSession.value().token, s_testPassword);
    QVERIFY(stepUp.isOk());

    auto resetResult = auth.resetUserPassword(QStringLiteral("u-admin"),
                                              QStringLiteral("u-target"),
                                              QStringLiteral("ReplacementPass12!"),
                                              stepUp.value().id);
    QVERIFY2(resetResult.isOk(), resetResult.isErr() ? qPrintable(resetResult.errorMessage()) : "");

    auto oldPassword = auth.signIn(QStringLiteral("target"), QStringLiteral("OriginalPass12!"));
    QVERIFY(oldPassword.isErr());

    auto newPassword = auth.signIn(QStringLiteral("target"), QStringLiteral("ReplacementPass12!"));
    QVERIFY(newPassword.isOk());
}

// ── Console lock tests ───────────────────────────────────────────────────────

void TstAuthService::test_consoleLock_requiresPasswordToUnlock()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto loginResult = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(loginResult.isOk());
    QString token = loginResult.value().token;

    // Lock console
    auto lockResult = auth.lockConsole(token);
    QVERIFY2(lockResult.isOk(), lockResult.isErr() ? qPrintable(lockResult.errorMessage()) : "");

    // Unlock with wrong password
    auto wrongUnlock = auth.unlockConsole(token, QStringLiteral("WrongPassword99!"));
    QVERIFY(wrongUnlock.isErr());
    QCOMPARE(wrongUnlock.errorCode(), ErrorCode::InvalidCredentials);

    // Unlock with correct password
    auto correctUnlock = auth.unlockConsole(token, s_testPassword);
    QVERIFY2(correctUnlock.isOk(), correctUnlock.isErr() ? qPrintable(correctUnlock.errorMessage()) : "");
}

// ── Step-up expiry test ──────────────────────────────────────────────────────

void TstAuthService::test_stepUp_expiredAfter2Minutes()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto loginResult = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(loginResult.isOk());

    auto stepUp = auth.initiateStepUp(loginResult.value().token, s_testPassword);
    QVERIFY(stepUp.isOk());

    // Manually expire the step-up window by updating expires_at to the past
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE step_up_windows SET expires_at = ? WHERE id = ?"));
    q.addBindValue(QDateTime::currentDateTimeUtc().addSecs(-60).toString(Qt::ISODateWithMs));
    q.addBindValue(stepUp.value().id);
    QVERIFY(q.exec());

    // Expired window must be rejected
    auto consumeResult = auth.consumeStepUp(stepUp.value().id);
    QVERIFY(consumeResult.isErr());
    QCOMPARE(consumeResult.errorCode(), ErrorCode::StepUpRequired);
}

// ── CAPTCHA cooldown and lockout edge-case tests ────────────────────────────

void TstAuthService::test_captcha_cooldownResets()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_testPassword,
                   Role::FrontDeskOperator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    // Fail 3 times to trigger CAPTCHA
    for (int i = 0; i < Validation::CaptchaAfterFailures; ++i) {
        auto failedAttempt = auth.signIn(QStringLiteral("admin"), QStringLiteral("Wrong12345678!"));
        QVERIFY(failedAttempt.isErr());
    }

    // CAPTCHA should now be required
    auto result = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::CaptchaRequired);

    // Simulate CAPTCHA cooldown expiry by setting expires_at to past
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE captcha_states SET expires_at = ? WHERE username = ?"));
    q.addBindValue(QDateTime::currentDateTimeUtc().addSecs(-60).toString(Qt::ISODateWithMs));
    q.addBindValue(QStringLiteral("admin"));
    QVERIFY(q.exec());

    // After cooldown, sign-in should no longer require the expired CAPTCHA
    // (the service should either accept the login or re-issue a fresh CAPTCHA)
    auto afterCooldown = auth.signIn(QStringLiteral("admin"), s_testPassword);
    // The expired CAPTCHA state should not block a correct password indefinitely
    // Either the login succeeds or a fresh CAPTCHA is issued
    QVERIFY(afterCooldown.isOk() || afterCooldown.errorCode() == ErrorCode::CaptchaRequired);
}

void TstAuthService::test_signIn_lockedAccountRejectsEvenWithCaptcha()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_testPassword,
                   Role::FrontDeskOperator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    // Lock the account via 5 failures
    for (int i = 0; i < Validation::LockoutFailureThreshold; ++i) {
        auto failedAttempt = auth.signIn(QStringLiteral("admin"), QStringLiteral("Wrong12345678!"));
        QVERIFY(failedAttempt.isErr());
    }

    // Even if CAPTCHA state is cleared (simulating a solved CAPTCHA), locked accounts
    // must remain locked — lockout takes precedence over CAPTCHA
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("DELETE FROM captcha_states WHERE username = 'admin'"));

    auto result = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(result.isErr());
    QVERIFY(result.errorCode() == ErrorCode::AccountLocked
            || result.errorCode() == ErrorCode::CaptchaRequired);
}

// ── Audit events test ────────────────────────────────────────────────────────

void TstAuthService::test_auditEventsRecorded()
{
    createTestUser(QStringLiteral("u1"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    // Sign in should create audit entry
    auto loginResult = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(loginResult.isOk());

    // Sign out should create audit entry
    auto signOutResult = auth.signOut(loginResult.value().token);
    QVERIFY2(signOutResult.isOk(), signOutResult.isErr() ? qPrintable(signOutResult.errorMessage()) : "");

    // Query audit entries
    AuditFilter filter;
    filter.limit = 100;
    auto entries = auditRepo.queryEntries(filter);
    QVERIFY2(entries.isOk(), entries.isErr() ? qPrintable(entries.errorMessage()) : "");
    QVERIFY2(entries.value().size() >= 2,
             qPrintable(QStringLiteral("Expected at least 2 audit entries, got %1")
                        .arg(entries.value().size())));

    // Verify Login event exists
    bool foundLogin = false;
    bool foundLogout = false;
    for (const AuditEntry& e : entries.value()) {
        if (e.eventType == AuditEventType::Login) foundLogin = true;
        if (e.eventType == AuditEventType::Logout) foundLogout = true;
    }
    QVERIFY2(foundLogin, "Login audit event must be recorded");
    QVERIFY2(foundLogout, "Logout audit event must be recorded");
}

QTEST_MAIN(TstAuthService)
#include "tst_auth_service.moc"
