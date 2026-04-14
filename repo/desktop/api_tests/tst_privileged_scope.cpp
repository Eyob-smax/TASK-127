// tst_privileged_scope.cpp — ProctorOps
// Integration tests for privileged-action scope enforcement across service boundaries.
//
// Exercises full multi-step workflows with real database, real crypto, real audit chain.
// Makes compliance-critical authorization boundaries reviewer-visible:
//   - Export/deletion state-machine enforcement with full audit trail
//   - Correction flow authorization and double-reversal prevention
//   - Sync key revocation blocks subsequent imports
//   - Update apply records install history with audit events
//   - Masked PII fields in export files
//   - Audit tombstones retained after deletion for compliance evidence

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QUuid>

#include "repositories/AuditRepository.h"
#include "repositories/MemberRepository.h"
#include "repositories/UserRepository.h"
#include "repositories/CheckInRepository.h"
#include "repositories/SyncRepository.h"
#include "repositories/UpdateRepository.h"
#include "services/AuditService.h"
#include "services/AuthService.h"
#include "services/DataSubjectService.h"
#include "services/CheckInService.h"
#include "services/SyncService.h"
#include "services/UpdateService.h"
#include "services/PackageVerifier.h"
#include "crypto/AesGcmCipher.h"
#include "crypto/Argon2idHasher.h"
#include "crypto/Ed25519Signer.h"
#include "crypto/Ed25519Verifier.h"
#include "utils/Migration.h"
#include "utils/Validation.h"
#include "models/CommonTypes.h"
#include "models/Audit.h"
#include "models/Update.h"
#include "models/Sync.h"

static void runMigrations(QSqlDatabase& db)
{
    Migration runner(db, QStringLiteral(SOURCE_ROOT "/database/migrations"));
    const auto result = runner.applyPending();
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

static QString sha256Hex(const QByteArray& data)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

class TstPrivilegedScope : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ── Export flow integration ──────────────────────────────────────────
    void test_exportRequest_fullFlow_withAudit();
    void test_exportRequest_cannotFulfillRejected();
    void test_exportRequest_rejectsNonExistentMember();
    void test_maskedFieldsInExportFile();

    // ── Deletion flow integration ───────────────────────────────────────
    void test_deletionRequest_fullThreeStepFlow();
    void test_deletionRequest_cannotCompleteWithoutApproval();
    void test_auditTombstonesRetainedAfterDeletion();

    // ── Correction flow integration ─────────────────────────────────────
    void test_correctionFlow_fullAuthorization();
    void test_correctionFlow_doubleReversalBlocked();

    // ── Sync key revocation ─────────────────────────────────────────────
    void test_syncKeyRevocation_blocksImport();

    // ── Update install history ──────────────────────────────────────────
    void test_updateApply_recordsInstallHistory();

    // ── Audit chain verification authorization ─────────────────────────
    void test_verifyChain_deniedForNonAdmin();
    void test_verifyChain_allowedForSecurityAdmin();

private:
    void insertUser(const QString& userId, const QString& username, const QString& role);
    void insertMember(const QString& id, const QString& memberId, const QString& name);
    QString createStepUpWindow(const QString& userId);

    QSqlDatabase m_db;
    int m_dbIndex = 0;

    static const QByteArray s_masterKey;
};

const QByteArray TstPrivilegedScope::s_masterKey = QByteArray(32, '\x70');

void TstPrivilegedScope::initTestCase()
{
    qDebug() << "TstPrivilegedScope (integration): starting test suite";
}

void TstPrivilegedScope::cleanupTestCase()
{
    qDebug() << "TstPrivilegedScope (integration): test suite complete";
}

void TstPrivilegedScope::init()
{
    QString connName = QStringLiteral("tst_priv_api_%1").arg(m_dbIndex++);
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

void TstPrivilegedScope::insertUser(const QString& userId, const QString& username,
                                     const QString& role)
{
    QSqlQuery q(m_db);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO users (id, username, role, status, created_at, updated_at) "
        "VALUES (?, ?, ?, 'Active', ?, ?)"));
    q.addBindValue(userId);
    q.addBindValue(username);
    q.addBindValue(role);
    q.addBindValue(now);
    q.addBindValue(now);
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
}

void TstPrivilegedScope::insertMember(const QString& id, const QString& memberId,
                                       const QString& name)
{
    AesGcmCipher cipher(s_masterKey);
    auto encName = cipher.encrypt(name, QByteArrayLiteral("member.name"));
    QVERIFY(encName.isOk());

    auto encMobile = cipher.encrypt(QStringLiteral("(555) 000-0000"),
                                    QByteArrayLiteral("member.mobile"));
    QVERIFY(encMobile.isOk());
    auto encBarcode = cipher.encrypt(QStringLiteral("BC-") + memberId,
                                     QByteArrayLiteral("member.barcode"));
    QVERIFY(encBarcode.isOk());

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO members (id, member_id, name_encrypted, mobile_encrypted, "
        "barcode_encrypted, deleted, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, 0, datetime('now'), datetime('now'))"));
    q.addBindValue(id);
    q.addBindValue(memberId);
    q.addBindValue(QString::fromLatin1(encName.value().toBase64()));
    q.addBindValue(QString::fromLatin1(encMobile.value().toBase64()));
    q.addBindValue(QString::fromLatin1(encBarcode.value().toBase64()));
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
}

QString TstPrivilegedScope::createStepUpWindow(const QString& userId)
{
    const QString sessionToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString stepUpId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QString expiresAt = QDateTime::currentDateTimeUtc()
                                  .addSecs(Validation::StepUpWindowSeconds)
                                  .toString(Qt::ISODateWithMs);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO user_sessions (token, user_id, created_at, last_active_at, active) "
        "VALUES (?, ?, ?, ?, 1)"));
    q.addBindValue(sessionToken);
    q.addBindValue(userId);
    q.addBindValue(now);
    q.addBindValue(now);
    if (!q.exec()) {
        qWarning() << "createStepUpWindow user_sessions insert failed:" << q.lastError().text();
        return QString{};
    }

    q.prepare(QStringLiteral(
        "INSERT INTO step_up_windows (id, user_id, session_token, granted_at, expires_at, consumed) "
        "VALUES (?, ?, ?, ?, ?, 0)"));
    q.addBindValue(stepUpId);
    q.addBindValue(userId);
    q.addBindValue(sessionToken);
    q.addBindValue(now);
    q.addBindValue(expiresAt);
    if (!q.exec()) {
        qWarning() << "createStepUpWindow step_up_windows insert failed:" << q.lastError().text();
        return QString{};
    }

    return stepUpId;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Export flow integration
// ═══════════════════════════════════════════════════════════════════════════════

void TstPrivilegedScope::test_exportRequest_fullFlow_withAudit()
{
    insertUser(QStringLiteral("u-admin"), QStringLiteral("admin"),
               QStringLiteral("SECURITY_ADMINISTRATOR"));
    insertMember(QStringLiteral("m-exp-full"), QStringLiteral("MBR-EXP"),
                 QStringLiteral("Alice Exportable"));

    AuditRepository auditRepo(m_db);
    MemberRepository memberRepo(m_db);
    UserRepository userRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    DataSubjectService dss(auditRepo, memberRepo, authSvc, auditSvc, cipher);

    // Create export request
    auto reqRes = dss.createExportRequest(QStringLiteral("m-exp-full"),
                                           QStringLiteral("Member data access request"),
                                           QStringLiteral("u-admin"));
    QVERIFY2(reqRes.isOk(), reqRes.isErr() ? qPrintable(reqRes.errorMessage()) : "");
    QCOMPARE(reqRes.value().status, QStringLiteral("PENDING"));

    // Fulfill with output file
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString outPath = tmp.path() + QStringLiteral("/export.json");

    auto fulfillRes = dss.fulfillExportRequest(reqRes.value().id, outPath,
                                                QStringLiteral("u-admin"),
                                                createStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY2(fulfillRes.isOk(), fulfillRes.isErr() ? qPrintable(fulfillRes.errorMessage()) : "");
    QCOMPARE(fulfillRes.value().status, QStringLiteral("COMPLETED"));

    // Verify export file exists with watermark and masked PII
    QVERIFY(QFile::exists(outPath));
    QFile f(outPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(f.readAll());
    QVERIFY2(content.contains(QStringLiteral("AUTHORIZED_EXPORT_ONLY")),
             "Export file must contain watermark");

    // Verify audit trail recorded (at least 2 events: creation + fulfillment)
    AuditFilter filter;
    filter.limit = 100;
    auto auditList = auditRepo.queryEntries(filter);
    QVERIFY(auditList.isOk());
    QVERIFY2(auditList.value().size() >= 2,
             "Audit trail must contain at least creation and fulfillment events");
}

void TstPrivilegedScope::test_exportRequest_rejectsNonExistentMember()
{
    insertUser(QStringLiteral("u-admin"), QStringLiteral("admin"),
               QStringLiteral("SECURITY_ADMINISTRATOR"));
    // Intentionally do NOT insert a member — the ID does not exist.

    AuditRepository  auditRepo(m_db);
    MemberRepository memberRepo(m_db);
    UserRepository   userRepo(m_db);
    AesGcmCipher     cipher(s_masterKey);
    AuditService     auditSvc(auditRepo, cipher);
    AuthService      authSvc(userRepo, auditRepo);
    DataSubjectService dss(auditRepo, memberRepo, authSvc, auditSvc, cipher);

    auto reqRes = dss.createExportRequest(QStringLiteral("nonexistent-member-id"),
                                           QStringLiteral("Export of unknown member"),
                                           QStringLiteral("u-admin"));
    QVERIFY2(reqRes.isErr(),
             "createExportRequest must fail for non-existent member");
}

void TstPrivilegedScope::test_exportRequest_cannotFulfillRejected()
{
    insertUser(QStringLiteral("u-admin"), QStringLiteral("admin"),
               QStringLiteral("SECURITY_ADMINISTRATOR"));
    insertMember(QStringLiteral("m-exp-rej"), QStringLiteral("MBR-REJX"),
                 QStringLiteral("Bob Rejected"));

    AuditRepository auditRepo(m_db);
    MemberRepository memberRepo(m_db);
    UserRepository userRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    DataSubjectService dss(auditRepo, memberRepo, authSvc, auditSvc, cipher);

    auto reqRes = dss.createExportRequest(QStringLiteral("m-exp-rej"),
                                           QStringLiteral("Request to reject"),
                                           QStringLiteral("u-admin"));
    QVERIFY(reqRes.isOk());

    const auto rejectRes = dss.rejectExportRequest(reqRes.value().id,
                                                   QStringLiteral("u-admin"),
                                                   createStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY2(rejectRes.isOk(), rejectRes.isErr() ? qPrintable(rejectRes.errorMessage()) : "");

    // Fulfillment must fail — no file should be written
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString outPath = tmp.path() + QStringLiteral("/should-not-exist.json");

    auto fulfillRes = dss.fulfillExportRequest(reqRes.value().id, outPath,
                                                QStringLiteral("u-admin"),
                                                createStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY(fulfillRes.isErr());
    QCOMPARE(fulfillRes.errorCode(), ErrorCode::InvalidState);
    QVERIFY2(!QFile::exists(outPath), "No export file should be written for rejected request");
}

void TstPrivilegedScope::test_maskedFieldsInExportFile()
{
    insertUser(QStringLiteral("u-admin"), QStringLiteral("admin"),
               QStringLiteral("SECURITY_ADMINISTRATOR"));
    insertMember(QStringLiteral("m-exp-mask"), QStringLiteral("MBR-MASK"),
                 QStringLiteral("Cathy Masked"));

    AuditRepository auditRepo(m_db);
    MemberRepository memberRepo(m_db);
    UserRepository userRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    DataSubjectService dss(auditRepo, memberRepo, authSvc, auditSvc, cipher);

    auto reqRes = dss.createExportRequest(QStringLiteral("m-exp-mask"),
                                           QStringLiteral("Export for masking test"),
                                           QStringLiteral("u-admin"));
    QVERIFY(reqRes.isOk());

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString outPath = tmp.path() + QStringLiteral("/masked-export.json");

    auto fulfillRes = dss.fulfillExportRequest(reqRes.value().id, outPath,
                                                QStringLiteral("u-admin"),
                                                createStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY2(fulfillRes.isOk(), fulfillRes.isErr() ? qPrintable(fulfillRes.errorMessage()) : "");

    QFile f(outPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(f.readAll());

    // Export must NOT contain raw mobile digits
    QVERIFY2(!content.contains(QStringLiteral("(555) 000-0000")),
             "Export file must not contain raw mobile number");

    // Watermark must be present
    QVERIFY(content.contains(QStringLiteral("AUTHORIZED_EXPORT_ONLY")));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Deletion flow integration
// ═══════════════════════════════════════════════════════════════════════════════

void TstPrivilegedScope::test_deletionRequest_fullThreeStepFlow()
{
    insertUser(QStringLiteral("u-admin"), QStringLiteral("admin"),
               QStringLiteral("SECURITY_ADMINISTRATOR"));
    insertMember(QStringLiteral("m-del-full"), QStringLiteral("MBR-DEL1"),
                 QStringLiteral("Delete Target"));

    AuditRepository auditRepo(m_db);
    MemberRepository memberRepo(m_db);
    UserRepository userRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    DataSubjectService dss(auditRepo, memberRepo, authSvc, auditSvc, cipher);

    // Step 1: Create deletion request
    auto reqRes = dss.createDeletionRequest(QStringLiteral("m-del-full"),
                                             QStringLiteral("Member requested erasure"),
                                             QStringLiteral("u-admin"));
    QVERIFY2(reqRes.isOk(), reqRes.isErr() ? qPrintable(reqRes.errorMessage()) : "");
    QCOMPARE(reqRes.value().status, QStringLiteral("PENDING"));

    // Step 2: Approve
    auto approveRes = dss.approveDeletionRequest(reqRes.value().id,
                                                  QStringLiteral("u-admin"),
                                                  createStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY2(approveRes.isOk(), approveRes.isErr() ? qPrintable(approveRes.errorMessage()) : "");
    QCOMPARE(approveRes.value().status, QStringLiteral("APPROVED"));

    // Step 3: Complete (anonymize)
    auto completeRes = dss.completeDeletion(reqRes.value().id,
                                            QStringLiteral("u-admin"),
                                            createStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY2(completeRes.isOk(), completeRes.isErr() ? qPrintable(completeRes.errorMessage()) : "");

    // Verify member is anonymized (deleted=1)
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT deleted FROM members WHERE id = ?"));
    q.addBindValue(QStringLiteral("m-del-full"));
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 1);

    // Audit trail should have >= 3 events (create, approve, complete)
    AuditFilter filter;
    filter.limit = 100;
    auto auditList = auditRepo.queryEntries(filter);
    QVERIFY(auditList.isOk());
    QVERIFY2(auditList.value().size() >= 3,
             "Deletion flow must produce at least 3 audit events");
}

void TstPrivilegedScope::test_deletionRequest_cannotCompleteWithoutApproval()
{
    insertUser(QStringLiteral("u-admin"), QStringLiteral("admin"),
               QStringLiteral("SECURITY_ADMINISTRATOR"));
    insertMember(QStringLiteral("m-del-skip"), QStringLiteral("MBR-SKIP"),
                 QStringLiteral("Skip Approval Target"));

    AuditRepository auditRepo(m_db);
    MemberRepository memberRepo(m_db);
    UserRepository userRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    DataSubjectService dss(auditRepo, memberRepo, authSvc, auditSvc, cipher);

    auto reqRes = dss.createDeletionRequest(QStringLiteral("m-del-skip"),
                                             QStringLiteral("Erasure request"),
                                             QStringLiteral("u-admin"));
    QVERIFY(reqRes.isOk());

    // Skip approval, attempt completion directly
    auto completeRes = dss.completeDeletion(reqRes.value().id,
                                            QStringLiteral("u-admin"),
                                            createStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY(completeRes.isErr());
    QCOMPARE(completeRes.errorCode(), ErrorCode::InvalidState);
}

void TstPrivilegedScope::test_auditTombstonesRetainedAfterDeletion()
{
    insertUser(QStringLiteral("u-admin"), QStringLiteral("admin"),
               QStringLiteral("SECURITY_ADMINISTRATOR"));
    insertMember(QStringLiteral("m-del-tomb"), QStringLiteral("MBR-TOMB"),
                 QStringLiteral("Tombstone Target"));

    AuditRepository auditRepo(m_db);
    MemberRepository memberRepo(m_db);
    UserRepository userRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    DataSubjectService dss(auditRepo, memberRepo, authSvc, auditSvc, cipher);

    // Full deletion flow
    auto reqRes = dss.createDeletionRequest(QStringLiteral("m-del-tomb"),
                                             QStringLiteral("Erasure for tombstone test"),
                                             QStringLiteral("u-admin"));
    QVERIFY(reqRes.isOk());
    const auto approveRes = dss.approveDeletionRequest(reqRes.value().id,
                                                       QStringLiteral("u-admin"),
                                                       createStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY2(approveRes.isOk(), approveRes.isErr() ? qPrintable(approveRes.errorMessage()) : "");
    const auto completeRes = dss.completeDeletion(reqRes.value().id,
                                                  QStringLiteral("u-admin"),
                                                  createStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY2(completeRes.isOk(), completeRes.isErr() ? qPrintable(completeRes.errorMessage()) : "");

    // Audit entries must still be queryable — compliance evidence chain retained
    AuditFilter filter;
    filter.limit = 100;
    auto auditList = auditRepo.queryEntries(filter);
    QVERIFY(auditList.isOk());
    QVERIFY2(!auditList.value().isEmpty(),
             "Audit tombstones must be retained after member deletion");

    // The audit trail should contain deletion-related events
    bool foundDeletionEvent = false;
    for (const auto& entry : auditList.value()) {
        if (entry.entityId == reqRes.value().id ||
            entry.entityId == QStringLiteral("m-del-tomb")) {
            foundDeletionEvent = true;
            break;
        }
    }
    QVERIFY2(foundDeletionEvent,
             "Audit entries for the deletion request must be retained as tombstones");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Correction flow integration
// ═══════════════════════════════════════════════════════════════════════════════

void TstPrivilegedScope::test_correctionFlow_fullAuthorization()
{
    insertUser(QStringLiteral("u-operator"), QStringLiteral("operator"),
               QStringLiteral("FRONT_DESK_OPERATOR"));
    insertUser(QStringLiteral("u-admin"), QStringLiteral("admin"),
               QStringLiteral("SECURITY_ADMINISTRATOR"));

    // Insert member with term card and punch card for check-in
    AesGcmCipher cipher(s_masterKey);
    auto encBarcode = cipher.encrypt(QStringLiteral("BC-CORR-001"),
                                     QByteArrayLiteral("member.barcode"));
    QVERIFY(encBarcode.isOk());
    auto encName = cipher.encrypt(QStringLiteral("Correction Member"),
                                  QByteArrayLiteral("member.name"));
    QVERIFY(encName.isOk());
    auto encMobile = cipher.encrypt(QStringLiteral("(555) 777-8888"),
                                    QByteArrayLiteral("member.mobile"));
    QVERIFY(encMobile.isOk());

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO members (id, member_id, name_encrypted, mobile_encrypted, "
        "barcode_encrypted, deleted, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, 0, datetime('now'), datetime('now'))"));
    q.addBindValue(QStringLiteral("m-corr"));
    q.addBindValue(QStringLiteral("MBR-CORR"));
    q.addBindValue(QString::fromLatin1(encName.value().toBase64()));
    q.addBindValue(QString::fromLatin1(encMobile.value().toBase64()));
    q.addBindValue(QString::fromLatin1(encBarcode.value().toBase64()));
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));

    // Insert term card (active, future expiry)
    q.prepare(QStringLiteral(
        "INSERT INTO term_cards (id, member_id, term_start, term_end, active, created_at) "
        "VALUES (?, ?, date('now', '-30 days'), date('now', '+30 days'), 1, datetime('now'))"));
    q.addBindValue(QStringLiteral("tc-corr"));
    q.addBindValue(QStringLiteral("m-corr"));
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));

    // Insert punch card with balance
    q.prepare(QStringLiteral(
        "INSERT INTO punch_cards (id, member_id, product_code, initial_balance, current_balance, "
        "created_at, updated_at) VALUES (?, ?, 'STD', 20, 15, datetime('now'), datetime('now'))"));
    q.addBindValue(QStringLiteral("pc-corr"));
    q.addBindValue(QStringLiteral("m-corr"));
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));

    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    UserRepository userRepo(m_db);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    CheckInService checkInSvc(memberRepo, checkInRepo, authSvc, auditSvc, cipher, m_db);

    // Perform check-in (creates a deduction event)
    MemberIdentifier correctionMember;
    correctionMember.type = MemberIdentifier::Type::MemberId;
    correctionMember.value = QStringLiteral("MBR-CORR");
    auto checkInRes = checkInSvc.checkIn(correctionMember,
                                         QStringLiteral("SESSION-CORR"),
                                         QStringLiteral("u-operator"));
    QVERIFY2(checkInRes.isOk(), checkInRes.isErr() ? qPrintable(checkInRes.errorMessage()) : "");

    // Find the deduction event
    q.prepare(QStringLiteral(
        "SELECT id FROM deduction_events WHERE member_id = ? ORDER BY deducted_at DESC LIMIT 1"));
    q.addBindValue(QStringLiteral("m-corr"));
    QVERIFY(q.exec() && q.next());
    const QString deductionId = q.value(0).toString();

    // Request correction
    auto corrReq = checkInSvc.requestCorrection(deductionId,
                                                  QStringLiteral("Wrong member scanned"),
                                                  QStringLiteral("u-operator"));
    QVERIFY2(corrReq.isOk(), corrReq.isErr() ? qPrintable(corrReq.errorMessage()) : "");

    // Approve correction (admin + step-up)
    auto approveRes = checkInSvc.approveCorrection(corrReq.value().id,
                                                    QStringLiteral("Verified wrong scan"),
                                                    QStringLiteral("u-admin"),
                                                    createStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY2(approveRes.isOk(), approveRes.isErr() ? qPrintable(approveRes.errorMessage()) : "");

    // Apply correction — should restore punch balance
    auto applyRes = checkInSvc.applyCorrection(corrReq.value().id,
                                               QStringLiteral("u-admin"),
                                               createStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY2(applyRes.isOk(), applyRes.isErr() ? qPrintable(applyRes.errorMessage()) : "");

    // Verify punch balance restored
    q.prepare(QStringLiteral("SELECT current_balance FROM punch_cards WHERE member_id = ?"));
    q.addBindValue(QStringLiteral("m-corr"));
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 15);  // Balance restored to original

    // Audit trail should be complete
    AuditFilter filter;
    filter.limit = 100;
    auto auditList = auditRepo.queryEntries(filter);
    QVERIFY(auditList.isOk());
    QVERIFY2(auditList.value().size() >= 3,
             "Correction flow must produce check-in, correction, and apply audit events");
}

void TstPrivilegedScope::test_correctionFlow_doubleReversalBlocked()
{
    insertUser(QStringLiteral("u-operator"), QStringLiteral("operator"),
               QStringLiteral("FRONT_DESK_OPERATOR"));
    insertUser(QStringLiteral("u-admin"), QStringLiteral("admin"),
               QStringLiteral("SECURITY_ADMINISTRATOR"));

    AesGcmCipher cipher(s_masterKey);
    auto encName = cipher.encrypt(QStringLiteral("Double Rev Member"),
                                  QByteArrayLiteral("member.name"));
    QVERIFY(encName.isOk());
    auto encMobile = cipher.encrypt(QStringLiteral("(555) 666-7777"),
                                    QByteArrayLiteral("member.mobile"));
    QVERIFY(encMobile.isOk());

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO members (id, member_id, name_encrypted, mobile_encrypted, "
        "barcode_encrypted, deleted, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, '', 0, datetime('now'), datetime('now'))"));
    q.addBindValue(QStringLiteral("m-dbl"));
    q.addBindValue(QStringLiteral("MBR-DBL"));
    q.addBindValue(QString::fromLatin1(encName.value().toBase64()));
    q.addBindValue(QString::fromLatin1(encMobile.value().toBase64()));
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));

    // Insert term card and punch card
    q.prepare(QStringLiteral(
        "INSERT INTO term_cards (id, member_id, term_start, term_end, active, created_at) "
        "VALUES (?, ?, date('now', '-30 days'), date('now', '+30 days'), 1, datetime('now'))"));
    q.addBindValue(QStringLiteral("tc-dbl"));
    q.addBindValue(QStringLiteral("m-dbl"));
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));

    q.prepare(QStringLiteral(
        "INSERT INTO punch_cards (id, member_id, product_code, initial_balance, current_balance, "
        "created_at, updated_at) VALUES (?, ?, 'STD', 20, 10, datetime('now'), datetime('now'))"));
    q.addBindValue(QStringLiteral("pc-dbl"));
    q.addBindValue(QStringLiteral("m-dbl"));
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));

    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    UserRepository userRepo(m_db);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    CheckInService checkInSvc(memberRepo, checkInRepo, authSvc, auditSvc, cipher, m_db);

    // Check in and get deduction
    MemberIdentifier duplicateMember;
    duplicateMember.type = MemberIdentifier::Type::MemberId;
    duplicateMember.value = QStringLiteral("MBR-DBL");
    auto checkInRes = checkInSvc.checkIn(duplicateMember,
                                         QStringLiteral("SESSION-DBL"),
                                         QStringLiteral("u-operator"));
    QVERIFY2(checkInRes.isOk(), checkInRes.isErr() ? qPrintable(checkInRes.errorMessage()) : "");

    q.prepare(QStringLiteral(
        "SELECT id FROM deduction_events WHERE member_id = ? ORDER BY deducted_at DESC LIMIT 1"));
    q.addBindValue(QStringLiteral("m-dbl"));
    QVERIFY(q.exec() && q.next());
    const QString deductionId = q.value(0).toString();

    // First correction: request → approve → apply (should succeed)
    auto corrReq = checkInSvc.requestCorrection(deductionId,
                                                  QStringLiteral("Wrong scan"),
                                                  QStringLiteral("u-operator"));
    QVERIFY(corrReq.isOk());
    QVERIFY(checkInSvc.approveCorrection(corrReq.value().id,
                                          QStringLiteral("OK"),
                                          QStringLiteral("u-admin"),
                                          createStepUpWindow(QStringLiteral("u-admin"))).isOk());
    QVERIFY(checkInSvc.applyCorrection(corrReq.value().id,
                                       QStringLiteral("u-admin"),
                                       createStepUpWindow(QStringLiteral("u-admin"))).isOk());

    // Second correction on same deduction must fail — double reversal blocked
    auto secondReq = checkInSvc.requestCorrection(deductionId,
                                                    QStringLiteral("Second attempt"),
                                                    QStringLiteral("u-operator"));
    QVERIFY2(secondReq.isErr(),
             "Second correction request on same deduction must be blocked");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Sync key revocation
// ═══════════════════════════════════════════════════════════════════════════════

void TstPrivilegedScope::test_syncKeyRevocation_blocksImport()
{
    insertUser(QStringLiteral("u-admin"), QStringLiteral("admin"),
               QStringLiteral("SECURITY_ADMINISTRATOR"));

    AesGcmCipher cipher(s_masterKey);
    SyncRepository syncRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    UserRepository userRepo(m_db);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    PackageVerifier verifier(syncRepo);
    SyncService syncSvc(syncRepo, checkInRepo, auditRepo, authSvc, auditSvc, verifier);

    // Generate a key pair and register in trust store
    auto keyRes = Ed25519Signer::generateKeyPair();
    QVERIFY(keyRes.isOk());
    const auto& [privKey, pubKey] = keyRes.value();
    const QString pubKeyHex = QString::fromLatin1(pubKey.toHex());

    auto impKeyRes = syncSvc.importSigningKey(QStringLiteral("Revocation Test Key"),
                                               pubKeyHex, QDateTime(),
                                               QStringLiteral("u-admin"),
                                               createStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY(impKeyRes.isOk());
    const QString keyId = impKeyRes.value().id;

    // Revoke the key immediately
    const auto revokeRes = syncSvc.revokeSigningKey(keyId,
                                                    QStringLiteral("u-admin"),
                                                    createStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY2(revokeRes.isOk(), revokeRes.isErr() ? qPrintable(revokeRes.errorMessage()) : "");

    // Build a validly-signed sync package using the now-revoked key
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString pkgDir = tmp.path() + QStringLiteral("/revoked-pkg");
    QDir().mkpath(pkgDir);

    QJsonObject body;
    body[QStringLiteral("package_id")]     = QStringLiteral("revoked-sync-pkg");
    body[QStringLiteral("source_desk_id")] = QStringLiteral("desk-revoked");
    body[QStringLiteral("signer_key_id")]  = keyId;
    body[QStringLiteral("entities")]       = QJsonArray();

    const QByteArray bodyBytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
    auto signRes = Ed25519Signer::sign(bodyBytes, privKey);
    QVERIFY(signRes.isOk());

    QJsonObject manifest = body;
    manifest[QStringLiteral("signature")] = QString::fromLatin1(signRes.value().toHex());

    QFile mf(pkgDir + QStringLiteral("/manifest.json"));
    QVERIFY(mf.open(QIODevice::WriteOnly));
    mf.write(QJsonDocument(manifest).toJson());
    mf.close();

    // Import must fail because the signing key was revoked
    auto importRes = syncSvc.importPackage(pkgDir, QStringLiteral("u-admin"));
    QVERIFY(importRes.isErr());
    QCOMPARE(importRes.errorCode(), ErrorCode::TrustStoreMiss);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Update install history
// ═══════════════════════════════════════════════════════════════════════════════

void TstPrivilegedScope::test_updateApply_recordsInstallHistory()
{
    insertUser(QStringLiteral("u-admin"), QStringLiteral("admin"),
               QStringLiteral("SECURITY_ADMINISTRATOR"));

    AesGcmCipher cipher(s_masterKey);
    SyncRepository syncRepo(m_db);
    UpdateRepository updateRepo(m_db);
    AuditRepository auditRepo(m_db);
    UserRepository userRepo(m_db);
    AuditService auditSvc(auditRepo, cipher);
    AuthService authSvc(userRepo, auditRepo);
    PackageVerifier verifier(syncRepo);
    UpdateService updateSvc(updateRepo, syncRepo, authSvc, verifier, auditSvc);

    // Generate a signing key pair and register
    auto keyRes = Ed25519Signer::generateKeyPair();
    QVERIFY(keyRes.isOk());
    const auto& [privKey, pubKey] = keyRes.value();
    const QString pubKeyHex = QString::fromLatin1(pubKey.toHex());

    CheckInRepository tempCheckInRepo(m_db);
    UserRepository tempUserRepo(m_db);
    AuthService tempAuthSvc(tempUserRepo, auditRepo);
    SyncService syncSvc(syncRepo, tempCheckInRepo, auditRepo, tempAuthSvc, auditSvc, verifier);
    auto impKeyRes = syncSvc.importSigningKey(QStringLiteral("Update Test Key"),
                                               pubKeyHex, QDateTime(),
                                               QStringLiteral("u-admin"),
                                               createStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY(impKeyRes.isOk());
    const QString signerKeyId = impKeyRes.value().id;

    // Build a signed .proctorpkg package
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString pkgDir = tmp.path() + QStringLiteral("/pkg-4.0.0");
    QDir().mkpath(pkgDir);

    const QByteArray exeContent = QByteArrayLiteral("ProctorOps binary v4.0.0");
    QFile cf(pkgDir + QStringLiteral("/proctorops.exe"));
    QVERIFY(cf.open(QIODevice::WriteOnly));
    cf.write(exeContent);
    cf.close();

    QJsonObject manifestBody;
    manifestBody[QStringLiteral("package_id")]      = QStringLiteral("pkg-4.0.0");
    manifestBody[QStringLiteral("version")]         = QStringLiteral("4.0.0");
    manifestBody[QStringLiteral("target_platform")] = QStringLiteral("windows-x86_64");
    manifestBody[QStringLiteral("description")]     = QStringLiteral("Integration test update");
    manifestBody[QStringLiteral("signer_key_id")]   = signerKeyId;

    QJsonArray components;
    QJsonObject comp;
    comp[QStringLiteral("name")]    = QStringLiteral("proctorops.exe");
    comp[QStringLiteral("version")] = QStringLiteral("4.0.0");
    comp[QStringLiteral("sha256")]  = sha256Hex(exeContent);
    comp[QStringLiteral("file")]    = QStringLiteral("proctorops.exe");
    components.append(comp);
    manifestBody[QStringLiteral("components")] = components;

    const QByteArray bodyBytes = QJsonDocument(manifestBody).toJson(QJsonDocument::Compact);
    auto signRes = Ed25519Signer::sign(bodyBytes, privKey);
    QVERIFY(signRes.isOk());

    QJsonObject manifest = manifestBody;
    manifest[QStringLiteral("signature")] = QString::fromLatin1(signRes.value().toHex());

    QFile mf(pkgDir + QStringLiteral("/update-manifest.json"));
    QVERIFY(mf.open(QIODevice::WriteOnly));
    mf.write(QJsonDocument(manifest).toJson());
    mf.close();

    // Import package
    auto importRes = updateSvc.importPackage(pkgDir, QStringLiteral("u-admin"));
    QVERIFY2(importRes.isOk(), importRes.isErr() ? qPrintable(importRes.errorMessage()) : "");
    QCOMPARE(importRes.value().status, UpdatePackageStatus::Staged);

    // Apply package
    auto applyRes = updateSvc.applyPackage(importRes.value().id,
                                            QStringLiteral("3.9.0"),
                                            QStringLiteral("u-admin"),
                                            createStepUpWindow(QStringLiteral("u-admin")));
    QVERIFY2(applyRes.isOk(), applyRes.isErr() ? qPrintable(applyRes.errorMessage()) : "");

    // Verify install history entry
    auto histRes = updateSvc.listInstallHistory(QStringLiteral("u-admin"));
    QVERIFY(histRes.isOk());
    bool found = false;
    for (const InstallHistoryEntry& h : histRes.value()) {
        if (h.packageId == importRes.value().id) {
            found = true;
            QCOMPARE(h.fromVersion, QStringLiteral("3.9.0"));
            QCOMPARE(h.toVersion,   QStringLiteral("4.0.0"));
            QCOMPARE(h.appliedByUserId, QStringLiteral("u-admin"));
        }
    }
    QVERIFY2(found, "Install history must contain an entry for the applied update");

    // Audit events should have been recorded
    AuditFilter filter;
    filter.limit = 100;
    auto auditList = auditRepo.queryEntries(filter);
    QVERIFY(auditList.isOk());
    QVERIFY2(auditList.value().size() >= 2,
             "Update flow must produce at least import and apply audit events");
}

// ── Audit chain verification authorization ──────────────────────────────────

void TstPrivilegedScope::test_verifyChain_deniedForNonAdmin()
{
    // Create a non-admin user (FrontDeskOperator)
    insertUser(QStringLiteral("op-001"), QStringLiteral("operator1"),
               QStringLiteral("FrontDeskOperator"));

    AuditRepository auditRepo(m_db);
    UserRepository  userRepo(m_db);
    AesGcmCipher    cipher(s_masterKey);
    AuditService    auditService(auditRepo, cipher);
    AuthService     authService(userRepo, auditRepo);

    // Wire auth service for RBAC enforcement
    auditService.setAuthService(&authService);

    // Record a test event so the chain is non-empty
    auto recordRes = auditService.recordEvent(
        QStringLiteral("op-001"), AuditEventType::Login,
        QStringLiteral("User"), QStringLiteral("op-001"));
    QVERIFY(recordRes.isOk());

    // Non-admin user must be denied chain verification
    auto result = auditService.verifyChain(QStringLiteral("op-001"), 100);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::AuthorizationDenied);
}

void TstPrivilegedScope::test_verifyChain_allowedForSecurityAdmin()
{
    // Create a SecurityAdministrator user
    insertUser(QStringLiteral("sa-001"), QStringLiteral("secadmin1"),
               QStringLiteral("SecurityAdministrator"));

    AuditRepository auditRepo(m_db);
    UserRepository  userRepo(m_db);
    AesGcmCipher    cipher(s_masterKey);
    AuditService    auditService(auditRepo, cipher);
    AuthService     authService(userRepo, auditRepo);

    auditService.setAuthService(&authService);

    auto recordRes = auditService.recordEvent(
        QStringLiteral("sa-001"), AuditEventType::Login,
        QStringLiteral("User"), QStringLiteral("sa-001"));
    QVERIFY(recordRes.isOk());

    // SecurityAdministrator must be allowed
    auto result = auditService.verifyChain(QStringLiteral("sa-001"), 100);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QVERIFY(result.value().integrityOk);
}

QTEST_MAIN(TstPrivilegedScope)
#include "tst_privileged_scope.moc"
