// tst_privileged_scope.cpp — ProctorOps
// Privileged-action scope enforcement unit tests.
//
// Systematically verifies authorization boundaries across three enforcement layers:
//   1. AuthService — RBAC role hierarchy, step-up window mechanics
//   2. Services — state-machine transitions, validation constraints
//   3. Utilities — MaskingPolicy, ClipboardGuard defense-in-depth PII controls
//
// These tests make compliance-critical scope boundaries statically auditable.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QClipboard>
#include <QApplication>
#include <QTemporaryDir>
#include <QUuid>

#include "services/AuthService.h"
#include "services/DataSubjectService.h"
#include "services/AuditService.h"
#include "services/UpdateService.h"
#include "services/PackageVerifier.h"
#include "repositories/UserRepository.h"
#include "repositories/AuditRepository.h"
#include "repositories/MemberRepository.h"
#include "repositories/SyncRepository.h"
#include "repositories/UpdateRepository.h"
#include "crypto/Argon2idHasher.h"
#include "crypto/AesGcmCipher.h"
#include "utils/MaskingPolicy.h"
#include "utils/ClipboardGuard.h"
#include "utils/Validation.h"
#include "utils/Migration.h"
#include "models/CommonTypes.h"

static void runMigrations(QSqlDatabase& db)
{
    Migration runner(db, QStringLiteral(SOURCE_ROOT "/database/migrations"));
    const auto result = runner.applyPending();
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

class TstPrivilegedScope : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ── AuthService RBAC policy enforcement ─────────────────────────────
    void test_rbac_operatorDeniedContentManager();
    void test_rbac_operatorDeniedSecurityAdmin();
    void test_rbac_proctorDeniedSecurityAdmin();
    void test_rbac_contentManagerDeniedSecurityAdmin();
    void test_rbac_securityAdminHasAllPermissions();
    void test_rbac_requireRole_deniesLowerRole();

    // ── AuthService step-up mechanics ───────────────────────────────────
    void test_stepUp_expiredWindowRejected();
    void test_stepUp_wrongPasswordRejected();
    void test_stepUp_consumeOnlyOnce();
    void test_stepUp_windowDuration_2Minutes();

    // ── Masking and PII defense-in-depth ────────────────────────────────
    void test_maskingPolicy_allFieldsRequireStepUp();
    void test_maskingPolicy_mobileMasksAllButLast4();
    void test_clipboardGuard_neverExposesPlaintextPII();
    void test_clipboardGuard_redactedWritesRedactedTag();

    // ── Service-layer state-machine enforcement ─────────────────────────
    void test_exportRequest_cannotFulfillFromRejected();
    void test_exportRequest_cannotFulfillFromCompleted();
    void test_deletionRequest_cannotApproveFromRejected();
    void test_deletionRequest_cannotCompleteFromPending();

    // ── Validation constraints on privileged operations ─────────────────
    void test_rollback_emptyRationale_rejected();
    void test_exportRequest_emptyRationale_rejected();
    void test_deletionRequest_emptyRationale_rejected();

private:
    void createTestUser(const QString& userId, const QString& username,
                        const QString& password, Role role);
    QString issueStepUpWindow(const QString& userId);

    QSqlDatabase m_db;
    int m_dbIndex = 0;

    static const QString s_testPassword;
    static const QByteArray s_masterKey;
};

const QString TstPrivilegedScope::s_testPassword = QStringLiteral("SecurePass12!!");
const QByteArray TstPrivilegedScope::s_masterKey = QByteArray(32, '\x50');

void TstPrivilegedScope::initTestCase()
{
    qDebug() << "TstPrivilegedScope: starting test suite";
}

void TstPrivilegedScope::cleanupTestCase()
{
    qDebug() << "TstPrivilegedScope: test suite complete";
}

void TstPrivilegedScope::init()
{
    QString connName = QStringLiteral("tst_priv_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON;"));

    runMigrations(m_db);
}

void TstPrivilegedScope::cleanup()
{
    QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstPrivilegedScope::createTestUser(const QString& userId, const QString& username,
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

QString TstPrivilegedScope::issueStepUpWindow(const QString& userId)
{
    const QString sessionToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString stepUpId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QString expiresAt = QDateTime::currentDateTimeUtc().addSecs(120).toString(Qt::ISODateWithMs);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO user_sessions (token, user_id, created_at, last_active_at, active) "
                             "VALUES (?, ?, ?, ?, 1)"));
    q.addBindValue(sessionToken);
    q.addBindValue(userId);
    q.addBindValue(now);
    q.addBindValue(now);
    if (!q.exec())
        return {};

    q.prepare(QStringLiteral("INSERT INTO step_up_windows (id, user_id, session_token, granted_at, expires_at, consumed) "
                             "VALUES (?, ?, ?, ?, ?, 0)"));
    q.addBindValue(stepUpId);
    q.addBindValue(userId);
    q.addBindValue(sessionToken);
    q.addBindValue(now);
    q.addBindValue(expiresAt);
    if (!q.exec())
        return {};

    return stepUpId;
}

// ═══════════════════════════════════════════════════════════════════════════════
// AuthService RBAC policy enforcement
// ═══════════════════════════════════════════════════════════════════════════════

void TstPrivilegedScope::test_rbac_operatorDeniedContentManager()
{
    QVERIFY(!AuthService::hasPermission(Role::FrontDeskOperator, Role::ContentManager));
}

void TstPrivilegedScope::test_rbac_operatorDeniedSecurityAdmin()
{
    QVERIFY(!AuthService::hasPermission(Role::FrontDeskOperator, Role::SecurityAdministrator));
}

void TstPrivilegedScope::test_rbac_proctorDeniedSecurityAdmin()
{
    QVERIFY(!AuthService::hasPermission(Role::Proctor, Role::SecurityAdministrator));
}

void TstPrivilegedScope::test_rbac_contentManagerDeniedSecurityAdmin()
{
    QVERIFY(!AuthService::hasPermission(Role::ContentManager, Role::SecurityAdministrator));
}

void TstPrivilegedScope::test_rbac_securityAdminHasAllPermissions()
{
    QVERIFY(AuthService::hasPermission(Role::SecurityAdministrator, Role::FrontDeskOperator));
    QVERIFY(AuthService::hasPermission(Role::SecurityAdministrator, Role::Proctor));
    QVERIFY(AuthService::hasPermission(Role::SecurityAdministrator, Role::ContentManager));
    QVERIFY(AuthService::hasPermission(Role::SecurityAdministrator, Role::SecurityAdministrator));
}

void TstPrivilegedScope::test_rbac_requireRole_deniesLowerRole()
{
    createTestUser(QStringLiteral("u-op"), QStringLiteral("operator"), s_testPassword,
                   Role::FrontDeskOperator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto loginResult = auth.signIn(QStringLiteral("operator"), s_testPassword);
    QVERIFY2(loginResult.isOk(), loginResult.isErr() ? qPrintable(loginResult.errorMessage()) : "");

    auto roleCheck = auth.requireRole(loginResult.value().token, Role::SecurityAdministrator);
    QVERIFY(roleCheck.isErr());
    QCOMPARE(roleCheck.errorCode(), ErrorCode::AuthorizationDenied);
}

// ═══════════════════════════════════════════════════════════════════════════════
// AuthService step-up mechanics
// ═══════════════════════════════════════════════════════════════════════════════

void TstPrivilegedScope::test_stepUp_expiredWindowRejected()
{
    createTestUser(QStringLiteral("u-admin"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto loginResult = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(loginResult.isOk());

    auto stepUp = auth.initiateStepUp(loginResult.value().token, s_testPassword);
    QVERIFY(stepUp.isOk());

    // Manually expire the step-up window
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE step_up_windows SET expires_at = ? WHERE id = ?"));
    q.addBindValue(QDateTime::currentDateTimeUtc().addSecs(-60).toString(Qt::ISODateWithMs));
    q.addBindValue(stepUp.value().id);
    QVERIFY(q.exec());

    auto consumeResult = auth.consumeStepUp(stepUp.value().id);
    QVERIFY(consumeResult.isErr());
    QCOMPARE(consumeResult.errorCode(), ErrorCode::StepUpRequired);
}

void TstPrivilegedScope::test_stepUp_wrongPasswordRejected()
{
    createTestUser(QStringLiteral("u-admin"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto loginResult = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(loginResult.isOk());

    auto stepUp = auth.initiateStepUp(loginResult.value().token, QStringLiteral("WrongPass99!!"));
    QVERIFY(stepUp.isErr());
    QCOMPARE(stepUp.errorCode(), ErrorCode::InvalidCredentials);
}

void TstPrivilegedScope::test_stepUp_consumeOnlyOnce()
{
    createTestUser(QStringLiteral("u-admin"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto loginResult = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(loginResult.isOk());

    auto stepUp = auth.initiateStepUp(loginResult.value().token, s_testPassword);
    QVERIFY(stepUp.isOk());

    // First consume succeeds
    auto first = auth.consumeStepUp(stepUp.value().id);
    QVERIFY2(first.isOk(), first.isErr() ? qPrintable(first.errorMessage()) : "");

    // Second consume must fail — single-use
    auto second = auth.consumeStepUp(stepUp.value().id);
    QVERIFY(second.isErr());
    QCOMPARE(second.errorCode(), ErrorCode::StepUpRequired);
}

void TstPrivilegedScope::test_stepUp_windowDuration_2Minutes()
{
    createTestUser(QStringLiteral("u-admin"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    UserRepository userRepo(m_db);
    AuditRepository auditRepo(m_db);
    AuthService auth(userRepo, auditRepo);

    auto loginResult = auth.signIn(QStringLiteral("admin"), s_testPassword);
    QVERIFY(loginResult.isOk());

    auto stepUp = auth.initiateStepUp(loginResult.value().token, s_testPassword);
    QVERIFY(stepUp.isOk());

    // Verify the window duration is exactly StepUpWindowSeconds (120s)
    qint64 durationSecs = stepUp.value().grantedAt.secsTo(stepUp.value().expiresAt);
    QCOMPARE(durationSecs, static_cast<qint64>(Validation::StepUpWindowSeconds));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Masking and PII defense-in-depth
// ═══════════════════════════════════════════════════════════════════════════════

void TstPrivilegedScope::test_maskingPolicy_allFieldsRequireStepUp()
{
    // MaskingPolicy::requiresStepUp() must always return true — full reveal
    // of any PII field type is gated behind step-up re-authentication
    QVERIFY(MaskingPolicy::requiresStepUp());
}

void TstPrivilegedScope::test_maskingPolicy_mobileMasksAllButLast4()
{
    const QString raw = QStringLiteral("(555) 123-4567");
    const QString masked = MaskingPolicy::maskMobile(raw);

    // Last 4 digits must be visible
    QVERIFY(masked.endsWith(QStringLiteral("4567")));

    // Raw digits before last 4 must not appear in masked output
    QVERIFY(!masked.contains(QStringLiteral("5551")));
    QVERIFY(!masked.contains(QStringLiteral("123")));

    // Must contain mask characters
    QVERIFY(masked.contains(QStringLiteral("*")));
}

void TstPrivilegedScope::test_clipboardGuard_neverExposesPlaintextPII()
{
    const QString raw = QStringLiteral("(555) 999-8888");
    ClipboardGuard::copyMasked(raw);

    QClipboard* clip = QApplication::clipboard();
    QVERIFY(clip != nullptr);

    const QString clipText = clip->text();
    // Clipboard must NOT contain the full raw value
    QVERIFY2(clipText != raw,
             "Clipboard must never contain plaintext PII after copyMasked");

    // Clipboard should contain a masked version (with asterisks)
    QVERIFY(clipText.contains(QStringLiteral("*")));
}

void TstPrivilegedScope::test_clipboardGuard_redactedWritesRedactedTag()
{
    const QString raw = QStringLiteral("Sensitive-Data-12345");
    ClipboardGuard::copyRedacted(raw);

    QClipboard* clip = QApplication::clipboard();
    QVERIFY(clip != nullptr);

    const QString clipText = clip->text();
    QCOMPARE(clipText, QStringLiteral("[REDACTED]"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Service-layer state-machine enforcement
// ═══════════════════════════════════════════════════════════════════════════════

void TstPrivilegedScope::test_exportRequest_cannotFulfillFromRejected()
{
    AuditRepository auditRepo(m_db);
    MemberRepository memberRepo(m_db);
    UserRepository userRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    DataSubjectService dss(auditRepo, memberRepo, authSvc, auditSvc, cipher);

    // Insert a test member
    auto encName = cipher.encrypt(QStringLiteral("Jane Doe"));
    QVERIFY(encName.isOk());

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO members (id, member_id, name_encrypted, mobile_encrypted, "
        "barcode_encrypted, deleted, created_at, updated_at) "
        "VALUES (?, ?, ?, '', '', 0, datetime('now'), datetime('now'))"));
    q.addBindValue(QStringLiteral("m-exp-rej"));
    q.addBindValue(QStringLiteral("MBR-REJ"));
    q.addBindValue(encName.value());
    QVERIFY(q.exec());

    // Insert actor user
    createTestUser(QStringLiteral("u-admin"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    // Create and reject export request
    auto reqRes = dss.createExportRequest(QStringLiteral("m-exp-rej"),
                                           QStringLiteral("Access request"),
                                           QStringLiteral("u-admin"));
    QVERIFY(reqRes.isOk());
    QVERIFY(dss.rejectExportRequest(reqRes.value().id,
                                    QStringLiteral("u-admin"),
                                    issueStepUpWindow(QStringLiteral("u-admin"))).isOk());

    // Attempt to fulfill the rejected request
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto fulfillRes = dss.fulfillExportRequest(reqRes.value().id,
                                                tmp.path() + QStringLiteral("/out.json"),
                                                QStringLiteral("u-admin"),
                                                issueStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY(fulfillRes.isErr());
    QCOMPARE(fulfillRes.errorCode(), ErrorCode::InvalidState);
}

void TstPrivilegedScope::test_exportRequest_cannotFulfillFromCompleted()
{
    AuditRepository auditRepo(m_db);
    MemberRepository memberRepo(m_db);
    UserRepository userRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    DataSubjectService dss(auditRepo, memberRepo, authSvc, auditSvc, cipher);

    auto encName = cipher.encrypt(QStringLiteral("Bob Test"));
    QVERIFY(encName.isOk());

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO members (id, member_id, name_encrypted, mobile_encrypted, "
        "barcode_encrypted, deleted, created_at, updated_at) "
        "VALUES (?, ?, ?, '', '', 0, datetime('now'), datetime('now'))"));
    q.addBindValue(QStringLiteral("m-exp-comp"));
    q.addBindValue(QStringLiteral("MBR-COMP"));
    q.addBindValue(encName.value());
    QVERIFY(q.exec());

    createTestUser(QStringLiteral("u-admin"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    auto reqRes = dss.createExportRequest(QStringLiteral("m-exp-comp"),
                                           QStringLiteral("First access"),
                                           QStringLiteral("u-admin"));
    QVERIFY(reqRes.isOk());

    // Fulfill once — should succeed
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto firstFulfill = dss.fulfillExportRequest(reqRes.value().id,
                                                   tmp.path() + QStringLiteral("/export1.json"),
                                                   QStringLiteral("u-admin"),
                                                   issueStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY2(firstFulfill.isOk(), firstFulfill.isErr() ? qPrintable(firstFulfill.errorMessage()) : "");

    // Second fulfill on completed request must fail
    auto secondFulfill = dss.fulfillExportRequest(reqRes.value().id,
                                                    tmp.path() + QStringLiteral("/export2.json"),
                                                    QStringLiteral("u-admin"),
                                                    issueStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY(secondFulfill.isErr());
    QCOMPARE(secondFulfill.errorCode(), ErrorCode::InvalidState);
}

void TstPrivilegedScope::test_deletionRequest_cannotApproveFromRejected()
{
    AuditRepository auditRepo(m_db);
    MemberRepository memberRepo(m_db);
    UserRepository userRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    DataSubjectService dss(auditRepo, memberRepo, authSvc, auditSvc, cipher);

    auto encName = cipher.encrypt(QStringLiteral("Del Target"));
    QVERIFY(encName.isOk());

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO members (id, member_id, name_encrypted, mobile_encrypted, "
        "barcode_encrypted, deleted, created_at, updated_at) "
        "VALUES (?, ?, ?, '', '', 0, datetime('now'), datetime('now'))"));
    q.addBindValue(QStringLiteral("m-del-rej"));
    q.addBindValue(QStringLiteral("MBR-DELR"));
    q.addBindValue(encName.value());
    QVERIFY(q.exec());

    createTestUser(QStringLiteral("u-admin"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    auto reqRes = dss.createDeletionRequest(QStringLiteral("m-del-rej"),
                                             QStringLiteral("Erasure request"),
                                             QStringLiteral("u-admin"));
    QVERIFY(reqRes.isOk());

    // Reject first
    QVERIFY(dss.rejectDeletionRequest(reqRes.value().id,
                                      QStringLiteral("u-admin"),
                                      issueStepUpWindow(QStringLiteral("u-admin"))).isOk());

    // Attempt to approve after rejection — must fail
    auto approveRes = dss.approveDeletionRequest(reqRes.value().id,
                                                  QStringLiteral("u-admin"),
                                                  issueStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY(approveRes.isErr());
    QCOMPARE(approveRes.errorCode(), ErrorCode::InvalidState);
}

void TstPrivilegedScope::test_deletionRequest_cannotCompleteFromPending()
{
    AuditRepository auditRepo(m_db);
    MemberRepository memberRepo(m_db);
    UserRepository userRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    DataSubjectService dss(auditRepo, memberRepo, authSvc, auditSvc, cipher);

    auto encName = cipher.encrypt(QStringLiteral("Pending Target"));
    QVERIFY(encName.isOk());

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO members (id, member_id, name_encrypted, mobile_encrypted, "
        "barcode_encrypted, deleted, created_at, updated_at) "
        "VALUES (?, ?, ?, '', '', 0, datetime('now'), datetime('now'))"));
    q.addBindValue(QStringLiteral("m-del-pend"));
    q.addBindValue(QStringLiteral("MBR-DELP"));
    q.addBindValue(encName.value());
    QVERIFY(q.exec());

    createTestUser(QStringLiteral("u-admin"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    auto reqRes = dss.createDeletionRequest(QStringLiteral("m-del-pend"),
                                             QStringLiteral("Erasure request"),
                                             QStringLiteral("u-admin"));
    QVERIFY(reqRes.isOk());

    // Skip approval, attempt to complete directly — must fail
    auto completeRes = dss.completeDeletion(reqRes.value().id,
                                            QStringLiteral("u-admin"),
                                            issueStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY(completeRes.isErr());
    QCOMPARE(completeRes.errorCode(), ErrorCode::InvalidState);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Validation constraints on privileged operations
// ═══════════════════════════════════════════════════════════════════════════════

void TstPrivilegedScope::test_rollback_emptyRationale_rejected()
{
    // Setup UpdateService with full dependency chain
    SyncRepository syncRepo(m_db);
    UpdateRepository updateRepo(m_db);
    AuditRepository auditRepo(m_db);
    UserRepository userRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    PackageVerifier verifier(syncRepo);
    UpdateService updateSvc(updateRepo, syncRepo, authSvc, verifier, auditSvc);

    createTestUser(QStringLiteral("u-admin"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    // Attempt rollback with empty rationale
    auto result = updateSvc.rollback(QStringLiteral("nonexistent-install"),
                                      QStringLiteral(""),
                                      QStringLiteral("u-admin"),
                                      issueStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstPrivilegedScope::test_exportRequest_emptyRationale_rejected()
{
    AuditRepository auditRepo(m_db);
    MemberRepository memberRepo(m_db);
    UserRepository userRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    DataSubjectService dss(auditRepo, memberRepo, authSvc, auditSvc, cipher);

    createTestUser(QStringLiteral("u-admin"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    // Whitespace-only rationale must be rejected
    auto result = dss.createExportRequest(QStringLiteral("m-any"),
                                           QStringLiteral("   "),
                                           QStringLiteral("u-admin"));
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstPrivilegedScope::test_deletionRequest_emptyRationale_rejected()
{
    AuditRepository auditRepo(m_db);
    MemberRepository memberRepo(m_db);
    UserRepository userRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    DataSubjectService dss(auditRepo, memberRepo, authSvc, auditSvc, cipher);

    createTestUser(QStringLiteral("u-admin"), QStringLiteral("admin"), s_testPassword,
                   Role::SecurityAdministrator);

    // Empty rationale must be rejected
    auto result = dss.createDeletionRequest(QStringLiteral("m-any"),
                                             QString{},
                                             QStringLiteral("u-admin"));
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

QTEST_MAIN(TstPrivilegedScope)
#include "tst_privileged_scope.moc"
