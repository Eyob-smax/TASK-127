// tst_checkin_service.cpp — ProctorOps
// Unit tests for CheckInService: check-in flow, mobile normalization,
// freeze blocking, term card validation, duplicate suppression,
// atomic deduction, and correction workflow.

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
#include "crypto/HashChain.h"
#include "utils/Validation.h"
#include "models/CommonTypes.h"

class TstCheckInService : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ── Check-in flow ───────────────────────────────────────────────────
    void test_checkIn_success();
    void test_checkIn_memberNotFound();
    void test_checkIn_frozenBlocked();
    void test_checkIn_termCardExpired();
    void test_checkIn_termCardMissing();
    void test_checkIn_punchCardExhausted();
    void test_checkIn_duplicateBlocked();
    void test_checkIn_duplicateAfterWindow();
    void test_checkIn_atomicDeduction();
    void test_checkIn_lookupByMemberId();
    void test_checkIn_lookupByMobile();

    // ── Mobile normalization ────────────────────────────────────────────
    void test_normalizeMobile_valid();
    void test_normalizeMobile_withDashes();
    void test_normalizeMobile_tooShort();
    void test_normalizeMobile_tooLong();

    // ── Corrections ─────────────────────────────────────────────────────
    void test_requestCorrection_success();
    void test_requestCorrection_alreadyReversed();
    void test_rejectCorrection_success();
    void test_applyCorrection_restoresBalance();

private:
    void applySchema();
    QString createTestUser(const QString& userId, const QString& username, const QString& role);
    QString createTestMember(const QString& memberId, const QString& name,
                              const QString& barcode, const QString& mobile);
    void createTermCard(const QString& memberId, int daysFromNow = 30);
    QString createPunchCard(const QString& memberId, int balance);
    void freezeMember(const QString& memberId, const QString& userId);

    QSqlDatabase m_db;
    int m_dbIndex = 0;

    static const QString s_operatorId;
    static const QString s_adminId;
    static const QByteArray s_masterKey;
};

const QString TstCheckInService::s_operatorId = QStringLiteral("operator-001");
const QString TstCheckInService::s_adminId    = QStringLiteral("admin-001");
const QByteArray TstCheckInService::s_masterKey = QByteArray(32, 'k');

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

void TstCheckInService::initTestCase() {}
void TstCheckInService::cleanupTestCase() {}

void TstCheckInService::init()
{
    QString connName = QStringLiteral("tst_ci_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON;"));
    applySchema();
    createTestUser(s_operatorId, QStringLiteral("operator"), QStringLiteral("FRONT_DESK_OPERATOR"));
    createTestUser(s_adminId, QStringLiteral("admin"), QStringLiteral("SECURITY_ADMINISTRATOR"));
}

void TstCheckInService::cleanup()
{
    QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstCheckInService::applySchema()
{
    QSqlQuery q(m_db);

    // Users (0002)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE users ("
        "  id TEXT PRIMARY KEY, username TEXT NOT NULL UNIQUE,"
        "  role TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'Active',"
        "  created_at TEXT NOT NULL, updated_at TEXT NOT NULL, created_by_user_id TEXT);"
    )), qPrintable(q.lastError().text()));

    // Step-up windows (0002)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE user_sessions ("
        "  token TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id),"
        "  created_at TEXT NOT NULL, last_active_at TEXT NOT NULL,"
        "  active INTEGER NOT NULL DEFAULT 1);"
    )), qPrintable(q.lastError().text()));
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE step_up_windows ("
        "  id TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id),"
        "  session_token TEXT NOT NULL REFERENCES user_sessions(token),"
        "  granted_at TEXT NOT NULL, expires_at TEXT NOT NULL,"
        "  consumed INTEGER NOT NULL DEFAULT 0);"
    )), qPrintable(q.lastError().text()));

    // Members (0003)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE members ("
        "  id TEXT PRIMARY KEY, member_id TEXT NOT NULL UNIQUE, member_id_hash TEXT,"
        "  barcode_encrypted TEXT NOT NULL, mobile_encrypted TEXT NOT NULL,"
        "  name_encrypted TEXT NOT NULL, deleted INTEGER NOT NULL DEFAULT 0,"
        "  created_at TEXT NOT NULL, updated_at TEXT NOT NULL);"
    )), qPrintable(q.lastError().text()));

    // Term cards (0003)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE term_cards ("
        "  id TEXT PRIMARY KEY, member_id TEXT NOT NULL REFERENCES members(id) ON DELETE CASCADE,"
        "  term_start TEXT NOT NULL, term_end TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1,"
        "  created_at TEXT NOT NULL, CONSTRAINT chk_term_dates CHECK (term_end > term_start));"
    )), qPrintable(q.lastError().text()));

    // Punch cards (0003)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE punch_cards ("
        "  id TEXT PRIMARY KEY, member_id TEXT NOT NULL REFERENCES members(id) ON DELETE CASCADE,"
        "  product_code TEXT NOT NULL, initial_balance INTEGER NOT NULL CHECK (initial_balance >= 0),"
        "  current_balance INTEGER NOT NULL CHECK (current_balance >= 0),"
        "  created_at TEXT NOT NULL, updated_at TEXT NOT NULL);"
    )), qPrintable(q.lastError().text()));

    // Freeze records (0003)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE member_freeze_records ("
        "  id TEXT PRIMARY KEY, member_id TEXT NOT NULL REFERENCES members(id) ON DELETE CASCADE,"
        "  reason TEXT NOT NULL, frozen_by_user_id TEXT NOT NULL REFERENCES users(id),"
        "  frozen_at TEXT NOT NULL, thawed_by_user_id TEXT, thawed_at TEXT);"
    )), qPrintable(q.lastError().text()));

    // Check-in attempts (0004)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE checkin_attempts ("
        "  id TEXT PRIMARY KEY, member_id TEXT NOT NULL REFERENCES members(id),"
        "  session_id TEXT NOT NULL, operator_user_id TEXT NOT NULL REFERENCES users(id),"
        "  status TEXT NOT NULL, attempted_at TEXT NOT NULL,"
        "  deduction_event_id TEXT, failure_reason TEXT);"
    )), qPrintable(q.lastError().text()));

    QVERIFY2(q.exec(QStringLiteral(
        "CREATE INDEX idx_checkin_dedup ON checkin_attempts (member_id, session_id, attempted_at, status);"
    )), qPrintable(q.lastError().text()));

    // Deduction events (0004)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE deduction_events ("
        "  id TEXT PRIMARY KEY, member_id TEXT NOT NULL REFERENCES members(id),"
        "  punch_card_id TEXT NOT NULL REFERENCES punch_cards(id),"
        "  checkin_attempt_id TEXT NOT NULL REFERENCES checkin_attempts(id),"
        "  sessions_deducted INTEGER NOT NULL DEFAULT 1 CHECK (sessions_deducted > 0),"
        "  balance_before INTEGER NOT NULL CHECK (balance_before >= 0),"
        "  balance_after INTEGER NOT NULL CHECK (balance_after >= 0),"
        "  deducted_at TEXT NOT NULL, reversed_by_correction_id TEXT,"
        "  CONSTRAINT chk_balance CHECK (balance_after = balance_before - sessions_deducted));"
    )), qPrintable(q.lastError().text()));

    // Correction requests (0004)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE correction_requests ("
        "  id TEXT PRIMARY KEY, deduction_event_id TEXT NOT NULL REFERENCES deduction_events(id),"
        "  requested_by_user_id TEXT NOT NULL REFERENCES users(id),"
        "  rationale TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'Pending',"
        "  created_at TEXT NOT NULL);"
    )), qPrintable(q.lastError().text()));

    // Correction approvals (0004)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE correction_approvals ("
        "  correction_request_id TEXT PRIMARY KEY REFERENCES correction_requests(id),"
        "  approved_by_user_id TEXT NOT NULL REFERENCES users(id),"
        "  step_up_window_id TEXT NOT NULL REFERENCES step_up_windows(id),"
        "  rationale TEXT NOT NULL, approved_at TEXT NOT NULL,"
        "  before_payload_json TEXT NOT NULL, after_payload_json TEXT NOT NULL);"
    )), qPrintable(q.lastError().text()));

    // Audit entries (0008)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE audit_entries ("
        "  id TEXT PRIMARY KEY, timestamp TEXT NOT NULL, actor_user_id TEXT NOT NULL,"
        "  event_type TEXT NOT NULL, entity_type TEXT NOT NULL, entity_id TEXT NOT NULL,"
        "  before_payload_json TEXT NOT NULL DEFAULT '{}',"
        "  after_payload_json TEXT NOT NULL DEFAULT '{}',"
        "  previous_entry_hash TEXT NOT NULL, entry_hash TEXT NOT NULL);"
    )), qPrintable(q.lastError().text()));
}

QString TstCheckInService::createTestUser(const QString& userId,
                                            const QString& username,
                                            const QString& role)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO users (id, username, role, status, created_at, updated_at)"
                              " VALUES (?, ?, ?, 'Active', ?, ?)"));
    q.addBindValue(userId);
    q.addBindValue(username);
    q.addBindValue(role);
    QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    q.addBindValue(now);
    q.addBindValue(now);
    q.exec();
    return userId;
}

QString TstCheckInService::createTestMember(const QString& memberId,
                                              const QString& name,
                                              const QString& barcode,
                                              const QString& mobile)
{
    AesGcmCipher cipher(s_masterKey);

    auto nameEnc    = cipher.encrypt(name, QByteArrayLiteral("member.name"));
    auto barcodeEnc = cipher.encrypt(barcode, QByteArrayLiteral("member.barcode"));
    auto mobileEnc  = cipher.encrypt(mobile, QByteArrayLiteral("member.mobile"));

    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO members (id, member_id, member_id_hash, barcode_encrypted, mobile_encrypted,"
                              " name_encrypted, deleted, created_at, updated_at)"
                              " VALUES (?, ?, ?, ?, ?, ?, 0, ?, ?)"));
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

void TstCheckInService::createTermCard(const QString& memberUuid, int daysFromNow)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO term_cards (id, member_id, term_start, term_end, active, created_at)"
                              " VALUES (?, ?, ?, ?, 1, ?)"));
    q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.addBindValue(memberUuid);
    q.addBindValue(QDate::currentDate().addDays(-30).toString(Qt::ISODate));
    q.addBindValue(QDate::currentDate().addDays(daysFromNow).toString(Qt::ISODate));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    q.exec();
}

QString TstCheckInService::createPunchCard(const QString& memberUuid, int balance)
{
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO punch_cards (id, member_id, product_code,"
                              " initial_balance, current_balance, created_at, updated_at)"
                              " VALUES (?, ?, 'STANDARD', ?, ?, ?, ?)"));
    q.addBindValue(id);
    q.addBindValue(memberUuid);
    q.addBindValue(balance);
    q.addBindValue(balance);
    q.addBindValue(now);
    q.addBindValue(now);
    q.exec();
    return id;
}

void TstCheckInService::freezeMember(const QString& memberUuid, const QString& userId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO member_freeze_records"
                              " (id, member_id, reason, frozen_by_user_id, frozen_at)"
                              " VALUES (?, ?, 'Suspected fraud', ?, ?)"));
    q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.addBindValue(memberUuid);
    q.addBindValue(userId);
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    q.exec();
}

// ── Check-in flow tests ─────────────────────────────────────────────────────────

void TstCheckInService::test_checkIn_success()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createTestMember(QStringLiteral("M001"), QStringLiteral("John"),
                                     QStringLiteral("BC001"), QStringLiteral("(555) 123-4567"));
    createTermCard(uuid, 30);
    createPunchCard(uuid, 10);

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("M001");

    auto result = svc.checkIn(id, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QVERIFY(!result.value().deductionEventId.isEmpty());
}

void TstCheckInService::test_checkIn_memberNotFound()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("NONEXISTENT");

    auto result = svc.checkIn(id, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY(result.isErr());
}

void TstCheckInService::test_checkIn_frozenBlocked()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createTestMember(QStringLiteral("M002"), QStringLiteral("Jane"),
                                     QStringLiteral("BC002"), QStringLiteral("(555) 234-5678"));
    createTermCard(uuid, 30);
    createPunchCard(uuid, 10);
    freezeMember(uuid, s_adminId);

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("M002");

    auto result = svc.checkIn(id, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::AccountFrozen);
}

void TstCheckInService::test_checkIn_termCardExpired()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createTestMember(QStringLiteral("M003"), QStringLiteral("Bob"),
                                     QStringLiteral("BC003"), QStringLiteral("(555) 345-6789"));
    // Create expired term card (ended yesterday)
    createTermCard(uuid, -1);
    createPunchCard(uuid, 10);

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("M003");

    auto result = svc.checkIn(id, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::TermCardExpired);
}

void TstCheckInService::test_checkIn_termCardMissing()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createTestMember(QStringLiteral("M004"), QStringLiteral("Alice"),
                                     QStringLiteral("BC004"), QStringLiteral("(555) 456-7890"));
    // No term card
    createPunchCard(uuid, 10);

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("M004");

    auto result = svc.checkIn(id, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::TermCardMissing);
    QVERIFY(result.errorMessage().contains(QStringLiteral("No term card"), Qt::CaseInsensitive));
}

void TstCheckInService::test_checkIn_punchCardExhausted()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createTestMember(QStringLiteral("M005"), QStringLiteral("Charlie"),
                                     QStringLiteral("BC005"), QStringLiteral("(555) 567-8901"));
    createTermCard(uuid, 30);
    createPunchCard(uuid, 0); // Exhausted

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("M005");

    auto result = svc.checkIn(id, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::PunchCardExhausted);
}

void TstCheckInService::test_checkIn_duplicateBlocked()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createTestMember(QStringLiteral("M006"), QStringLiteral("Dave"),
                                     QStringLiteral("BC006"), QStringLiteral("(555) 678-9012"));
    createTermCard(uuid, 30);
    createPunchCard(uuid, 10);

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("M006");

    // First check-in succeeds
    auto first = svc.checkIn(id, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY2(first.isOk(), first.isErr() ? qPrintable(first.errorMessage()) : "");
    QVERIFY(!first.value().deductionEventId.isEmpty());

    // Immediate second attempt on same session should be blocked
    auto second = svc.checkIn(id, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY(second.isErr());
    QCOMPARE(second.errorCode(), ErrorCode::DuplicateCheckIn);
}

void TstCheckInService::test_checkIn_duplicateAfterWindow()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createTestMember(QStringLiteral("M007"), QStringLiteral("Eve"),
                                     QStringLiteral("BC007"), QStringLiteral("(555) 789-0123"));
    createTermCard(uuid, 30);
    createPunchCard(uuid, 10);

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("M007");

    // First check-in
    auto first = svc.checkIn(id, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY(first.isOk());

    // Backdating the attempt to 31 seconds ago to simulate window expiry
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE checkin_attempts SET attempted_at = ? WHERE member_id = ?"));
    q.addBindValue(QDateTime::currentDateTimeUtc().addSecs(-31).toString(Qt::ISODateWithMs));
    q.addBindValue(uuid);
    q.exec();

    q.prepare(QStringLiteral("UPDATE checkin_duplicate_guards SET last_success_at = ? WHERE member_id = ? AND session_id = ?"));
    q.addBindValue(QDateTime::currentDateTimeUtc().addSecs(-31).toString(Qt::ISODateWithMs));
    q.addBindValue(uuid);
    q.addBindValue(QStringLiteral("SESSION-01"));
    q.exec();

    // Now should succeed (outside duplicate window)
    auto second = svc.checkIn(id, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY2(second.isOk(), second.isErr() ? qPrintable(second.errorMessage()) : "");
    QVERIFY(!second.value().deductionEventId.isEmpty());
}

void TstCheckInService::test_checkIn_atomicDeduction()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createTestMember(QStringLiteral("M008"), QStringLiteral("Frank"),
                                     QStringLiteral("BC008"), QStringLiteral("(555) 890-1234"));
    createTermCard(uuid, 30);
    createPunchCard(uuid, 5);

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("M008");

    auto result = svc.checkIn(id, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY(result.isOk());
    QVERIFY(!result.value().deductionEventId.isEmpty());

    // Verify balance was deducted
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT current_balance FROM punch_cards WHERE member_id = ?"));
    q.addBindValue(uuid);
    q.exec();
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 4);
}

void TstCheckInService::test_checkIn_lookupByMemberId()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createTestMember(QStringLiteral("M009"), QStringLiteral("Grace"),
                                     QStringLiteral("BC009"), QStringLiteral("(555) 901-2345"));
    createTermCard(uuid, 30);
    createPunchCard(uuid, 10);

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("M009");

    auto result = svc.checkIn(id, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QVERIFY(!result.value().deductionEventId.isEmpty());
}

void TstCheckInService::test_checkIn_lookupByMobile()
{
    AesGcmCipher cipher(s_masterKey);
    MemberRepository memberRepo(m_db);
    memberRepo.setDecryptor([&cipher](const QString& field, const QString& value) {
        QByteArray aad;
        if (field == QStringLiteral("mobile")) {
            aad = QByteArrayLiteral("member.mobile");
        } else if (field == QStringLiteral("barcode")) {
            aad = QByteArrayLiteral("member.barcode");
        } else {
            aad = field.toUtf8();
        }
        auto dec = cipher.decrypt(QByteArray::fromBase64(value.toLatin1()), aad);
        return dec.isOk() ? dec.value() : QString();
    });
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createTestMember(QStringLiteral("M010"), QStringLiteral("Hank"),
                                     QStringLiteral("BC010"), QStringLiteral("(555) 012-3456"));
    createTermCard(uuid, 30);
    createPunchCard(uuid, 10);

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::Mobile;
    id.value = QStringLiteral("5550123456");

    auto result = svc.checkIn(id, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QVERIFY(!result.value().deductionEventId.isEmpty());
}

// ── Mobile normalization tests ──────────────────────────────────────────────────

void TstCheckInService::test_normalizeMobile_valid()
{
    QCOMPARE(CheckInService::normalizeMobile(QStringLiteral("5551234567")),
             QStringLiteral("(555) 123-4567"));
}

void TstCheckInService::test_normalizeMobile_withDashes()
{
    QCOMPARE(CheckInService::normalizeMobile(QStringLiteral("555-123-4567")),
             QStringLiteral("(555) 123-4567"));
}

void TstCheckInService::test_normalizeMobile_tooShort()
{
    QVERIFY(CheckInService::normalizeMobile(QStringLiteral("12345")).isEmpty());
}

void TstCheckInService::test_normalizeMobile_tooLong()
{
    QVERIFY(CheckInService::normalizeMobile(QStringLiteral("12345678901")).isEmpty());
}

// ── Correction tests ────────────────────────────────────────────────────────────

void TstCheckInService::test_requestCorrection_success()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    // Create and check in a member to get a deduction event
    QString uuid = createTestMember(QStringLiteral("M011"), QStringLiteral("Irene"),
                                     QStringLiteral("BC011"), QStringLiteral("(555) 111-2222"));
    createTermCard(uuid, 30);
    createPunchCard(uuid, 10);

    MemberIdentifier id;
    id.type = MemberIdentifier::Type::MemberId;
    id.value = QStringLiteral("M011");

    auto checkInResult = svc.checkIn(id, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY(checkInResult.isOk());

    // Get the deduction event ID
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT id FROM deduction_events WHERE member_id = '") + uuid + QStringLiteral("'"));
    QVERIFY(q.next());
    QString deductionId = q.value(0).toString();

    auto corrResult = svc.requestCorrection(deductionId, QStringLiteral("Wrong deduction"), s_operatorId);
    QVERIFY2(corrResult.isOk(), corrResult.isErr() ? qPrintable(corrResult.errorMessage()) : "");
    QCOMPARE(corrResult.value().status, CorrectionStatus::Pending);
}

void TstCheckInService::test_requestCorrection_alreadyReversed()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createTestMember(QStringLiteral("M012"), QStringLiteral("Jack"),
                                     QStringLiteral("BC012"), QStringLiteral("(555) 222-3333"));
    createTermCard(uuid, 30);
    createPunchCard(uuid, 10);

    MemberIdentifier mid;
    mid.type = MemberIdentifier::Type::MemberId;
    mid.value = QStringLiteral("M012");

    auto seededCheckIn = svc.checkIn(mid, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY2(seededCheckIn.isOk(), seededCheckIn.isErr() ? qPrintable(seededCheckIn.errorMessage()) : "");

    // Get deduction ID and manually mark as reversed
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT id FROM deduction_events WHERE member_id = '") + uuid + QStringLiteral("'"));
    QVERIFY(q.next());
    QString deductionId = q.value(0).toString();

    q.prepare(QStringLiteral("UPDATE deduction_events SET reversed_by_correction_id = 'some-id' WHERE id = ?"));
    q.addBindValue(deductionId);
    q.exec();

    auto result = svc.requestCorrection(deductionId, QStringLiteral("Error"), s_operatorId);
    QVERIFY(result.isErr());
}

void TstCheckInService::test_rejectCorrection_success()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createTestMember(QStringLiteral("M013"), QStringLiteral("Kate"),
                                     QStringLiteral("BC013"), QStringLiteral("(555) 333-4444"));
    createTermCard(uuid, 30);
    createPunchCard(uuid, 10);

    MemberIdentifier mid;
    mid.type = MemberIdentifier::Type::MemberId;
    mid.value = QStringLiteral("M013");

    auto seededCheckIn = svc.checkIn(mid, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY2(seededCheckIn.isOk(), seededCheckIn.isErr() ? qPrintable(seededCheckIn.errorMessage()) : "");

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT id FROM deduction_events WHERE member_id = '") + uuid + QStringLiteral("'"));
    QVERIFY(q.next());
    QString deductionId = q.value(0).toString();

    auto corrResult = svc.requestCorrection(deductionId, QStringLiteral("Mistake"), s_operatorId);
    QVERIFY(corrResult.isOk());

    const QString sessionToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString stepUpId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QString expiresAt = QDateTime::currentDateTimeUtc()
                                  .addSecs(Validation::StepUpWindowSeconds)
                                  .toString(Qt::ISODateWithMs);

    q.prepare(QStringLiteral("INSERT INTO user_sessions (token, user_id, created_at, last_active_at, active)"
                              " VALUES (?, ?, ?, ?, 1)"));
    q.addBindValue(sessionToken);
    q.addBindValue(s_adminId);
    q.addBindValue(now);
    q.addBindValue(now);
    q.exec();

    q.prepare(QStringLiteral("INSERT INTO step_up_windows (id, user_id, session_token, granted_at, expires_at, consumed)"
                              " VALUES (?, ?, ?, ?, ?, 0)"));
    q.addBindValue(stepUpId);
    q.addBindValue(s_adminId);
    q.addBindValue(sessionToken);
    q.addBindValue(now);
    q.addBindValue(expiresAt);
    q.exec();

    auto rejectResult = svc.rejectCorrection(corrResult.value().id,
                                             QStringLiteral("Denied"),
                                             s_adminId,
                                             stepUpId);
    QVERIFY2(rejectResult.isOk(), rejectResult.isErr() ? qPrintable(rejectResult.errorMessage()) : "");
}

void TstCheckInService::test_applyCorrection_restoresBalance()
{
    MemberRepository memberRepo(m_db);
    CheckInRepository checkInRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    CheckInHarness harness(m_db, memberRepo, checkInRepo, auditRepo, cipher);
    CheckInService& svc = harness.service;

    QString uuid = createTestMember(QStringLiteral("M014"), QStringLiteral("Leo"),
                                     QStringLiteral("BC014"), QStringLiteral("(555) 444-5555"));
    createTermCard(uuid, 30);
    createPunchCard(uuid, 5);

    MemberIdentifier mid;
    mid.type = MemberIdentifier::Type::MemberId;
    mid.value = QStringLiteral("M014");

    auto seededCheckIn = svc.checkIn(mid, QStringLiteral("SESSION-01"), s_operatorId);
    QVERIFY2(seededCheckIn.isOk(), seededCheckIn.isErr() ? qPrintable(seededCheckIn.errorMessage()) : "");

    // Verify balance is 4
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT current_balance FROM punch_cards WHERE member_id = ?"));
    q.addBindValue(uuid);
    q.exec();
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 4);

    // Get deduction ID
    q.exec(QStringLiteral("SELECT id FROM deduction_events WHERE member_id = '") + uuid + QStringLiteral("'"));
    QVERIFY(q.next());
    QString deductionId = q.value(0).toString();

    // Request correction
    auto corrResult = svc.requestCorrection(deductionId, QStringLiteral("Wrong"), s_operatorId);
    QVERIFY(corrResult.isOk());

    // Create step-up window for approval
    const QString sessionToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString stepUpId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QString expiresAt = QDateTime::currentDateTimeUtc()
                                  .addSecs(Validation::StepUpWindowSeconds)
                                  .toString(Qt::ISODateWithMs);
    q.prepare(QStringLiteral("INSERT INTO user_sessions (token, user_id, created_at, last_active_at, active)"
                              " VALUES (?, ?, ?, ?, 1)"));
    q.addBindValue(sessionToken);
    q.addBindValue(s_adminId);
    q.addBindValue(now);
    q.addBindValue(now);
    q.exec();

    q.prepare(QStringLiteral("INSERT INTO step_up_windows (id, user_id, session_token, granted_at, expires_at, consumed)"
                              " VALUES (?, ?, ?, ?, ?, 0)"));
    q.addBindValue(stepUpId);
    q.addBindValue(s_adminId);
    q.addBindValue(sessionToken);
    q.addBindValue(now);
    q.addBindValue(expiresAt);
    q.exec();

    // Approve correction
    auto approveResult = svc.approveCorrection(corrResult.value().id, QStringLiteral("Approved"),
                                                 s_adminId, stepUpId);
    QVERIFY2(approveResult.isOk(), approveResult.isErr() ? qPrintable(approveResult.errorMessage()) : "");

    // Apply correction
    const QString applySessionToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString applyStepUpId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    q.prepare(QStringLiteral("INSERT INTO user_sessions (token, user_id, created_at, last_active_at, active)"
                              " VALUES (?, ?, ?, ?, 1)"));
    q.addBindValue(applySessionToken);
    q.addBindValue(s_adminId);
    q.addBindValue(now);
    q.addBindValue(now);
    q.exec();

    q.prepare(QStringLiteral("INSERT INTO step_up_windows (id, user_id, session_token, granted_at, expires_at, consumed)"
                              " VALUES (?, ?, ?, ?, ?, 0)"));
    q.addBindValue(applyStepUpId);
    q.addBindValue(s_adminId);
    q.addBindValue(applySessionToken);
    q.addBindValue(now);
    q.addBindValue(expiresAt);
    q.exec();

    auto applyResult = svc.applyCorrection(corrResult.value().id, s_adminId, applyStepUpId);
    QVERIFY2(applyResult.isOk(), applyResult.isErr() ? qPrintable(applyResult.errorMessage()) : "");

    // Verify balance restored to 5
    q.prepare(QStringLiteral("SELECT current_balance FROM punch_cards WHERE member_id = ?"));
    q.addBindValue(uuid);
    q.exec();
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 5);
}

QTEST_MAIN(TstCheckInService)
#include "tst_checkin_service.moc"
