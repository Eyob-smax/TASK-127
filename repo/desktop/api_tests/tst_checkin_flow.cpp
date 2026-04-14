// tst_checkin_flow.cpp — ProctorOps
// Integration tests for full check-in workflow: barcode→deduction flow,
// freeze blocking, duplicate suppression, term card validation,
// punch card exhaustion, and multi-check-in balance tracking.

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

class TstCheckInFlow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void test_fullBarcodeDeductionFlow();
    void test_freezeBlockingWithAudit();
    void test_duplicateSuppressionWithinWindow();
    void test_termCardValidation();
    void test_punchCardExhaustion();
    void test_multipleCheckInsTrackBalance();

private:
    void applySchema();
    void createTestUser(const QString& userId);
    QString createMember(const QString& memberId, const QString& barcode, const QString& mobile);
    void createTermCard(const QString& memberUuid, int daysFromNow);
    void createPunchCard(const QString& memberUuid, int balance);
    void freezeMember(const QString& memberUuid);

    QSqlDatabase m_db;
    int m_dbIndex = 0;
    static const QString s_operatorId;
    static const QByteArray s_masterKey;
};

const QString TstCheckInFlow::s_operatorId = QStringLiteral("op-001");
const QByteArray TstCheckInFlow::s_masterKey = QByteArray(32, 'k');

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

void TstCheckInFlow::initTestCase() {}
void TstCheckInFlow::cleanupTestCase() {}

void TstCheckInFlow::init()
{
    QString connName = QStringLiteral("tst_ciflow_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON;"));
    applySchema();
    createTestUser(s_operatorId);
}

void TstCheckInFlow::cleanup()
{
    QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstCheckInFlow::applySchema()
{
    QSqlQuery q(m_db);
    QVERIFY2(q.exec(QStringLiteral("CREATE TABLE users (id TEXT PRIMARY KEY, username TEXT NOT NULL UNIQUE, role TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'Active', created_at TEXT NOT NULL, updated_at TEXT NOT NULL, created_by_user_id TEXT);")), qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral("CREATE TABLE step_up_windows (id TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id), created_at TEXT NOT NULL, consumed INTEGER NOT NULL DEFAULT 0);")), qPrintable(q.lastError().text()));
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

void TstCheckInFlow::createTestUser(const QString& userId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO users (id, username, role, status, created_at, updated_at) VALUES (?, ?, 'FRONT_DESK_OPERATOR', 'Active', ?, ?)"));
    q.addBindValue(userId);
    q.addBindValue(QStringLiteral("operator"));
    QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    q.addBindValue(now);
    q.addBindValue(now);
    q.exec();
}

QString TstCheckInFlow::createMember(const QString& memberId, const QString& barcode, const QString& mobile)
{
    AesGcmCipher cipher(s_masterKey);
    auto nameEnc = cipher.encrypt(QStringLiteral("Test Member"), QByteArrayLiteral("member.name"));
    auto barcodeEnc = cipher.encrypt(barcode, QByteArrayLiteral("member.barcode"));
    auto mobileEnc = cipher.encrypt(mobile, QByteArrayLiteral("member.mobile"));

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
    return uuid;
}

void TstCheckInFlow::createTermCard(const QString& memberUuid, int daysFromNow)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO term_cards (id, member_id, term_start, term_end, active, created_at) VALUES (?, ?, ?, ?, 1, ?)"));
    q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.addBindValue(memberUuid);
    q.addBindValue(QDate::currentDate().addDays(-30).toString(Qt::ISODate));
    q.addBindValue(QDate::currentDate().addDays(daysFromNow).toString(Qt::ISODate));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    q.exec();
}

void TstCheckInFlow::createPunchCard(const QString& memberUuid, int balance)
{
    QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO punch_cards (id, member_id, product_code, initial_balance, current_balance, created_at, updated_at) VALUES (?, ?, 'STANDARD', ?, ?, ?, ?)"));
    q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.addBindValue(memberUuid);
    q.addBindValue(balance);
    q.addBindValue(balance);
    q.addBindValue(now);
    q.addBindValue(now);
    q.exec();
}

void TstCheckInFlow::freezeMember(const QString& memberUuid)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO member_freeze_records (id, member_id, reason, frozen_by_user_id, frozen_at) VALUES (?, ?, 'Fraud', ?, ?)"));
    q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.addBindValue(memberUuid);
    q.addBindValue(s_operatorId);
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    q.exec();
}

// ── Integration tests ───────────────────────────────────────────────────────────

void TstCheckInFlow::test_fullBarcodeDeductionFlow()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createMember(QStringLiteral("F001"), QStringLiteral("BCFLOW001"), QStringLiteral("(555) 100-2000"));
    createTermCard(uuid, 60);
    createPunchCard(uuid, 10);

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("F001");

    auto result = svc.checkIn(id, QStringLiteral("SESSION-FLOW"), s_operatorId);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QCOMPARE(result.value().remainingBalance, 9);

    // Verify audit trail
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM audit_entries WHERE event_type = 'CHECKIN_SUCCESS'"));
    QVERIFY(q.next());
    QVERIFY(q.value(0).toInt() >= 1);

    // Verify deduction event created
    q.exec(QStringLiteral("SELECT COUNT(*) FROM deduction_events WHERE member_id = '") + uuid + QStringLiteral("'"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
}

void TstCheckInFlow::test_freezeBlockingWithAudit()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createMember(QStringLiteral("F002"), QStringLiteral("BCFZ002"), QStringLiteral("(555) 200-3000"));
    createTermCard(uuid, 60);
    createPunchCard(uuid, 10);
    freezeMember(uuid);

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("F002");

    auto result = svc.checkIn(id, QStringLiteral("SESSION-FREEZE"), s_operatorId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::AccountFrozen);

    // Verify audit
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM audit_entries WHERE event_type = 'CHECKIN_FROZEN_BLOCKED'"));
    QVERIFY(q.next());
    QVERIFY(q.value(0).toInt() >= 1);
}

void TstCheckInFlow::test_duplicateSuppressionWithinWindow()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createMember(QStringLiteral("F003"), QStringLiteral("BCDUP003"), QStringLiteral("(555) 300-4000"));
    createTermCard(uuid, 60);
    createPunchCard(uuid, 10);

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("F003");

    auto first = svc.checkIn(id, QStringLiteral("SESSION-DUP"), s_operatorId);
    QVERIFY(first.isOk());
    QCOMPARE(first.value().remainingBalance, 9);

    auto second = svc.checkIn(id, QStringLiteral("SESSION-DUP"), s_operatorId);
    QVERIFY(second.isErr());
    QCOMPARE(second.errorCode(), ErrorCode::DuplicateCheckIn);

    // Balance should only be deducted once
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT current_balance FROM punch_cards WHERE member_id = ?"));
    q.addBindValue(uuid);
    q.exec();
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 9);
}

void TstCheckInFlow::test_termCardValidation()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    // Member with expired term card
    QString uuid = createMember(QStringLiteral("F004"), QStringLiteral("BCTERM004"), QStringLiteral("(555) 400-5000"));
    createTermCard(uuid, -1); // expired yesterday
    createPunchCard(uuid, 10);

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("F004");

    auto result = svc.checkIn(id, QStringLiteral("SESSION-TERM"), s_operatorId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::TermCardExpired);
}

void TstCheckInFlow::test_punchCardExhaustion()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createMember(QStringLiteral("F005"), QStringLiteral("BCEX005"), QStringLiteral("(555) 500-6000"));
    createTermCard(uuid, 60);
    createPunchCard(uuid, 0); // exhausted

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("F005");

    auto result = svc.checkIn(id, QStringLiteral("SESSION-EX"), s_operatorId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::PunchCardExhausted);
}

void TstCheckInFlow::test_multipleCheckInsTrackBalance()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createMember(QStringLiteral("F006"), QStringLiteral("BCMUL006"), QStringLiteral("(555) 600-7000"));
    createTermCard(uuid, 60);
    createPunchCard(uuid, 3);

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("F006");

    // Three check-ins on different sessions
    for (int i = 1; i <= 3; ++i) {
        auto result = svc.checkIn(id, QStringLiteral("SESSION-%1").arg(i), s_operatorId);
        QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
        QCOMPARE(result.value().remainingBalance, 3 - i);
    }

    // Balance should be 0
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT current_balance FROM punch_cards WHERE member_id = ?"));
    q.addBindValue(uuid);
    q.exec();
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    // 4th check-in should fail
    auto exhausted = svc.checkIn(id, QStringLiteral("SESSION-4"), s_operatorId);
    QVERIFY(exhausted.isErr());
    QCOMPARE(exhausted.errorCode(), ErrorCode::PunchCardExhausted);
}

QTEST_MAIN(TstCheckInFlow)
#include "tst_checkin_flow.moc"
