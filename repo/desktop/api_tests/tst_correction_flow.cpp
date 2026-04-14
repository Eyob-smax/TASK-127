// tst_correction_flow.cpp — ProctorOps
// Integration tests for the correction workflow: request → step-up → approve → apply,
// rejection with audit, double reversal prevention, step-up requirement enforcement,
// and complete audit trail with before/after payloads.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QCryptographicHash>

#include "services/CheckInService.h"
#include "services/AuthService.h"
#include "repositories/MemberRepository.h"
#include "repositories/CheckInRepository.h"
#include "repositories/AuditRepository.h"
#include "repositories/UserRepository.h"
#include "crypto/AesGcmCipher.h"
#include "utils/Validation.h"
#include "models/CommonTypes.h"

class TstCorrectionFlow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void test_fullCorrectionFlow();
    void test_rejectionWithAudit();
    void test_doubleReversalPrevention();
    void test_stepUpRequirement();
    void test_auditTrailCompleteness();

private:
    void applySchema();
    QString setupCheckedInMember(CheckInService& svc, const QString& memberId);
    QString getDeductionEventId(const QString& memberUuid);
    QString createStepUpWindow(const QString& userId);

    QSqlDatabase m_db;
    int m_dbIndex = 0;

    static const QString s_operatorId;
    static const QString s_adminId;
    static const QByteArray s_masterKey;
};

const QString TstCorrectionFlow::s_operatorId = QStringLiteral("op-001");
const QString TstCorrectionFlow::s_adminId    = QStringLiteral("admin-001");
const QByteArray TstCorrectionFlow::s_masterKey = QByteArray(32, 'k');

namespace {
struct CheckInHarness {
    UserRepository userRepo;
    AuditService auditService;
    AuthService authService;
    CheckInService service;

    CheckInHarness(QSqlDatabase& db,
                   IMemberRepository& memberRepo,
                   ICheckInRepository& checkInRepo,
                   AuditRepository& auditRepo,
                   AesGcmCipher& cipher)
        : userRepo(db)
        , auditService(auditRepo, cipher)
        , authService(userRepo, auditRepo)
        , service(memberRepo, checkInRepo, authService, auditService, cipher, db)
    {
    }
};
}

void TstCorrectionFlow::initTestCase() {}
void TstCorrectionFlow::cleanupTestCase() {}

void TstCorrectionFlow::init()
{
    QString connName = QStringLiteral("tst_corr_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON;"));
    applySchema();

    // Create users
    auto createUser = [&](const QString& id, const QString& name, const QString& role) {
        q.prepare(QStringLiteral("INSERT INTO users (id, username, role, status, created_at, updated_at) VALUES (?, ?, ?, 'Active', ?, ?)"));
        q.addBindValue(id);
        q.addBindValue(name);
        q.addBindValue(role);
        QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        q.addBindValue(now);
        q.addBindValue(now);
        q.exec();
    };
    createUser(s_operatorId, QStringLiteral("operator"), QStringLiteral("FRONT_DESK_OPERATOR"));
    createUser(s_adminId, QStringLiteral("admin"), QStringLiteral("SECURITY_ADMINISTRATOR"));
}

void TstCorrectionFlow::cleanup()
{
    QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstCorrectionFlow::applySchema()
{
    QSqlQuery q(m_db);
    QVERIFY2(q.exec(QStringLiteral("CREATE TABLE users (id TEXT PRIMARY KEY, username TEXT NOT NULL UNIQUE, role TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'Active', created_at TEXT NOT NULL, updated_at TEXT NOT NULL, created_by_user_id TEXT);")), qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral("CREATE TABLE user_sessions (token TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id), created_at TEXT NOT NULL, last_active_at TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1);")), qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral("CREATE TABLE step_up_windows (id TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id), session_token TEXT NOT NULL REFERENCES user_sessions(token), granted_at TEXT NOT NULL, expires_at TEXT NOT NULL, consumed INTEGER NOT NULL DEFAULT 0);")), qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral("CREATE TABLE members (id TEXT PRIMARY KEY, member_id TEXT NOT NULL UNIQUE, member_id_hash TEXT, barcode_encrypted TEXT NOT NULL, mobile_encrypted TEXT NOT NULL, name_encrypted TEXT NOT NULL, deleted INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL, updated_at TEXT NOT NULL);")), qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral("CREATE TABLE term_cards (id TEXT PRIMARY KEY, member_id TEXT NOT NULL REFERENCES members(id) ON DELETE CASCADE, term_start TEXT NOT NULL, term_end TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1, created_at TEXT NOT NULL, CONSTRAINT chk_term_dates CHECK (term_end > term_start));")), qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral("CREATE TABLE punch_cards (id TEXT PRIMARY KEY, member_id TEXT NOT NULL REFERENCES members(id) ON DELETE CASCADE, product_code TEXT NOT NULL, initial_balance INTEGER NOT NULL CHECK (initial_balance >= 0), current_balance INTEGER NOT NULL CHECK (current_balance >= 0), created_at TEXT NOT NULL, updated_at TEXT NOT NULL);")), qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral("CREATE TABLE member_freeze_records (id TEXT PRIMARY KEY, member_id TEXT NOT NULL REFERENCES members(id) ON DELETE CASCADE, reason TEXT NOT NULL, frozen_by_user_id TEXT NOT NULL REFERENCES users(id), frozen_at TEXT NOT NULL, thawed_by_user_id TEXT, thawed_at TEXT);")), qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral("CREATE TABLE checkin_attempts (id TEXT PRIMARY KEY, member_id TEXT NOT NULL REFERENCES members(id), session_id TEXT NOT NULL, operator_user_id TEXT NOT NULL REFERENCES users(id), status TEXT NOT NULL, attempted_at TEXT NOT NULL, deduction_event_id TEXT, failure_reason TEXT);")), qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral("CREATE INDEX idx_checkin_dedup ON checkin_attempts (member_id, session_id, attempted_at, status);")), qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral("CREATE TABLE deduction_events (id TEXT PRIMARY KEY, member_id TEXT NOT NULL REFERENCES members(id), punch_card_id TEXT NOT NULL REFERENCES punch_cards(id), checkin_attempt_id TEXT NOT NULL REFERENCES checkin_attempts(id), sessions_deducted INTEGER NOT NULL DEFAULT 1 CHECK (sessions_deducted > 0), balance_before INTEGER NOT NULL CHECK (balance_before >= 0), balance_after INTEGER NOT NULL CHECK (balance_after >= 0), deducted_at TEXT NOT NULL, reversed_by_correction_id TEXT, CONSTRAINT chk_balance CHECK (balance_after = balance_before - sessions_deducted));")), qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral("CREATE TABLE correction_requests (id TEXT PRIMARY KEY, deduction_event_id TEXT NOT NULL REFERENCES deduction_events(id), requested_by_user_id TEXT NOT NULL REFERENCES users(id), rationale TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'Pending', created_at TEXT NOT NULL);")), qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral("CREATE TABLE correction_approvals (correction_request_id TEXT PRIMARY KEY REFERENCES correction_requests(id), approved_by_user_id TEXT NOT NULL REFERENCES users(id), step_up_window_id TEXT NOT NULL REFERENCES step_up_windows(id), rationale TEXT NOT NULL, approved_at TEXT NOT NULL, before_payload_json TEXT NOT NULL, after_payload_json TEXT NOT NULL);")), qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral("CREATE TABLE audit_entries (id TEXT PRIMARY KEY, timestamp TEXT NOT NULL, actor_user_id TEXT NOT NULL, event_type TEXT NOT NULL, entity_type TEXT NOT NULL, entity_id TEXT NOT NULL, before_payload_json TEXT NOT NULL DEFAULT '{}', after_payload_json TEXT NOT NULL DEFAULT '{}', previous_entry_hash TEXT NOT NULL, entry_hash TEXT NOT NULL);")), qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral("CREATE TABLE audit_chain_head (id INTEGER PRIMARY KEY CHECK (id = 1), last_entry_id TEXT, last_entry_hash TEXT NOT NULL DEFAULT '');")), qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral("INSERT INTO audit_chain_head (id, last_entry_id, last_entry_hash) VALUES (1, NULL, '');")), qPrintable(q.lastError().text()));
}

QString TstCorrectionFlow::setupCheckedInMember(CheckInService& svc, const QString& memberId)
{
    AesGcmCipher cipher(s_masterKey);
    auto nameEnc = cipher.encrypt(QStringLiteral("Test"), QByteArrayLiteral("member.name"));
    auto barcodeEnc = cipher.encrypt(QStringLiteral("BC") + memberId, QByteArrayLiteral("member.barcode"));
    auto mobileEnc = cipher.encrypt(QStringLiteral("(555) 100-1000"), QByteArrayLiteral("member.mobile"));

    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO members (id, member_id, member_id_hash, barcode_encrypted, mobile_encrypted, name_encrypted, deleted, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, 0, ?, ?)"));
    q.addBindValue(uuid);
    q.addBindValue(memberId);
    const QString memberIdHash = QString::fromLatin1(
        QCryptographicHash::hash(memberId.trimmed().toUtf8(), QCryptographicHash::Sha256).toHex());
    q.addBindValue(memberIdHash);
    q.addBindValue(QString::fromLatin1(barcodeEnc.value().toBase64()));
    q.addBindValue(QString::fromLatin1(mobileEnc.value().toBase64()));
    q.addBindValue(QString::fromLatin1(nameEnc.value().toBase64()));
    q.addBindValue(now);
    q.addBindValue(now);
    q.exec();

    // Term card
    q.prepare(QStringLiteral("INSERT INTO term_cards (id, member_id, term_start, term_end, active, created_at) VALUES (?, ?, ?, ?, 1, ?)"));
    q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.addBindValue(uuid);
    q.addBindValue(QDate::currentDate().addDays(-30).toString(Qt::ISODate));
    q.addBindValue(QDate::currentDate().addDays(60).toString(Qt::ISODate));
    q.addBindValue(now);
    q.exec();

    // Punch card with balance 10
    q.prepare(QStringLiteral("INSERT INTO punch_cards (id, member_id, product_code, initial_balance, current_balance, created_at, updated_at) VALUES (?, ?, 'STD', 10, 10, ?, ?)"));
    q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.addBindValue(uuid);
    q.addBindValue(now);
    q.addBindValue(now);
    q.exec();

    // Check in
    MemberIdentifier mid;
    mid.type = MemberIdentifier::Type::MemberId;
    mid.value = memberId;
    const auto checkInRes = svc.checkIn(mid, QStringLiteral("SESSION-CORR"), s_operatorId);
    if (checkInRes.isErr()) {
        qWarning() << "setupCheckedInMember checkIn failed:" << checkInRes.errorMessage();
        return QString{};
    }

    return uuid;
}

QString TstCorrectionFlow::getDeductionEventId(const QString& memberUuid)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM deduction_events WHERE member_id = ?"));
    q.addBindValue(memberUuid);
    q.exec();
    q.next();
    return q.value(0).toString();
}

QString TstCorrectionFlow::createStepUpWindow(const QString& userId)
{
    const QString sessionToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QString expiresAt = QDateTime::currentDateTimeUtc()
                                  .addSecs(Validation::StepUpWindowSeconds)
                                  .toString(Qt::ISODateWithMs);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO user_sessions (token, user_id, created_at, last_active_at, active) VALUES (?, ?, ?, ?, 1)"));
    q.addBindValue(sessionToken);
    q.addBindValue(userId);
    q.addBindValue(now);
    q.addBindValue(now);
    q.exec();

    q.prepare(QStringLiteral("INSERT INTO step_up_windows (id, user_id, session_token, granted_at, expires_at, consumed) VALUES (?, ?, ?, ?, ?, 0)"));
    q.addBindValue(id);
    q.addBindValue(userId);
    q.addBindValue(sessionToken);
    q.addBindValue(now);
    q.addBindValue(expiresAt);
    q.exec();
    return id;
}

// ── Tests ───────────────────────────────────────────────────────────────────────

void TstCorrectionFlow::test_fullCorrectionFlow()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = setupCheckedInMember(svc, QStringLiteral("C001"));
    QString deductionId = getDeductionEventId(uuid);

    // 1. Request correction
    auto req = svc.requestCorrection(deductionId, QStringLiteral("Wrong member"), s_operatorId);
    QVERIFY2(req.isOk(), req.isErr() ? qPrintable(req.errorMessage()) : "");
    QCOMPARE(req.value().status, CorrectionStatus::Pending);

    // 2. Approve with step-up
    QString stepUpId = createStepUpWindow(s_adminId);
    auto approve = svc.approveCorrection(req.value().id, QStringLiteral("Confirmed wrong"), s_adminId, stepUpId);
    QVERIFY2(approve.isOk(), approve.isErr() ? qPrintable(approve.errorMessage()) : "");

    // 3. Apply correction
    auto apply = svc.applyCorrection(req.value().id,
                                     s_adminId,
                                     createStepUpWindow(s_adminId));
    QVERIFY2(apply.isOk(), apply.isErr() ? qPrintable(apply.errorMessage()) : "");

    // 4. Verify balance restored (10 → 9 → 10)
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT current_balance FROM punch_cards WHERE member_id = ?"));
    q.addBindValue(uuid);
    q.exec();
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 10);

    // 5. Verify deduction marked reversed
    q.prepare(QStringLiteral("SELECT reversed_by_correction_id FROM deduction_events WHERE id = ?"));
    q.addBindValue(deductionId);
    q.exec();
    QVERIFY(q.next());
    QVERIFY(!q.value(0).toString().isEmpty());
}

void TstCorrectionFlow::test_rejectionWithAudit()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = setupCheckedInMember(svc, QStringLiteral("C002"));
    QString deductionId = getDeductionEventId(uuid);

    auto req = svc.requestCorrection(deductionId, QStringLiteral("Mistake"), s_operatorId);
    QVERIFY(req.isOk());

    auto reject = svc.rejectCorrection(req.value().id,
                                       QStringLiteral("Not justified"),
                                       s_adminId,
                                       createStepUpWindow(s_adminId));
    QVERIFY2(reject.isOk(), reject.isErr() ? qPrintable(reject.errorMessage()) : "");

    // Verify audit event for rejection
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM audit_entries WHERE event_type = 'CORRECTION_REJECTED'"));
    QVERIFY(q.next());
    QVERIFY(q.value(0).toInt() >= 1);

    // Balance should still be deducted (9)
    q.prepare(QStringLiteral("SELECT current_balance FROM punch_cards WHERE member_id = ?"));
    q.addBindValue(uuid);
    q.exec();
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 9);
}

void TstCorrectionFlow::test_doubleReversalPrevention()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = setupCheckedInMember(svc, QStringLiteral("C003"));
    QString deductionId = getDeductionEventId(uuid);

    // First correction: full flow
    auto req1 = svc.requestCorrection(deductionId, QStringLiteral("First"), s_operatorId);
    QVERIFY(req1.isOk());

    QString stepUpId = createStepUpWindow(s_adminId);
    const auto approve1 = svc.approveCorrection(req1.value().id, QStringLiteral("OK"), s_adminId, stepUpId);
    QVERIFY2(approve1.isOk(), approve1.isErr() ? qPrintable(approve1.errorMessage()) : "");
    const auto apply1 = svc.applyCorrection(req1.value().id,
                                            s_adminId,
                                            createStepUpWindow(s_adminId));
    QVERIFY2(apply1.isOk(), apply1.isErr() ? qPrintable(apply1.errorMessage()) : "");

    // Second correction request on the same deduction should fail
    auto req2 = svc.requestCorrection(deductionId, QStringLiteral("Double"), s_operatorId);
    QVERIFY(req2.isErr());
}

void TstCorrectionFlow::test_stepUpRequirement()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = setupCheckedInMember(svc, QStringLiteral("C004"));
    QString deductionId = getDeductionEventId(uuid);

    auto req = svc.requestCorrection(deductionId, QStringLiteral("Need step-up"), s_operatorId);
    QVERIFY(req.isOk());

    // Attempt approval with invalid step-up window
    auto approve = svc.approveCorrection(req.value().id, QStringLiteral("No step-up"),
                                           s_adminId, QStringLiteral("nonexistent-window"));
    // Should fail because step-up window doesn't exist
    QVERIFY(approve.isErr());
}

void TstCorrectionFlow::test_auditTrailCompleteness()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = setupCheckedInMember(svc, QStringLiteral("C005"));
    QString deductionId = getDeductionEventId(uuid);

    // Full correction flow
    auto req = svc.requestCorrection(deductionId, QStringLiteral("Audit test"), s_operatorId);
    QVERIFY(req.isOk());

    QString stepUpId = createStepUpWindow(s_adminId);
    const auto approve = svc.approveCorrection(req.value().id, QStringLiteral("Approved"), s_adminId, stepUpId);
    QVERIFY2(approve.isOk(), approve.isErr() ? qPrintable(approve.errorMessage()) : "");
    const auto apply = svc.applyCorrection(req.value().id,
                                           s_adminId,
                                           createStepUpWindow(s_adminId));
    QVERIFY2(apply.isOk(), apply.isErr() ? qPrintable(apply.errorMessage()) : "");

    // Verify complete audit trail
    QSqlQuery q(m_db);

    // CheckInSuccess event
    q.exec(QStringLiteral("SELECT COUNT(*) FROM audit_entries WHERE event_type = 'CHECKIN_SUCCESS'"));
    QVERIFY(q.next());
    QVERIFY(q.value(0).toInt() >= 1);

    // DeductionCreated event
    q.exec(QStringLiteral("SELECT COUNT(*) FROM audit_entries WHERE event_type = 'DEDUCTION_CREATED'"));
    QVERIFY(q.next());
    QVERIFY(q.value(0).toInt() >= 1);

    // CorrectionRequested event
    q.exec(QStringLiteral("SELECT COUNT(*) FROM audit_entries WHERE event_type = 'CORRECTION_REQUESTED'"));
    QVERIFY(q.next());
    QVERIFY(q.value(0).toInt() >= 1);

    // CorrectionApproved event
    q.exec(QStringLiteral("SELECT COUNT(*) FROM audit_entries WHERE event_type = 'CORRECTION_APPROVED'"));
    QVERIFY(q.next());
    QVERIFY(q.value(0).toInt() >= 1);

    // CorrectionApplied event
    q.exec(QStringLiteral("SELECT COUNT(*) FROM audit_entries WHERE event_type = 'CORRECTION_APPLIED'"));
    QVERIFY(q.next());
    QVERIFY(q.value(0).toInt() >= 1);
}

QTEST_MAIN(TstCorrectionFlow)
#include "tst_correction_flow.moc"
