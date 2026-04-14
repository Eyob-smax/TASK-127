// tst_operator_workflows.cpp — ProctorOps
// Integration-style tests for primary operator workflows.
// Exercises service-to-repository interactions across the full check-in and
// question governance flows with a real in-memory SQLite database.
// No mocks substitute for real service or repository implementations.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDate>
#include <QDateTime>
#include <QUuid>
#include <QCryptographicHash>

#include "services/CheckInService.h"
#include "services/QuestionService.h"
#include "services/AuditService.h"
#include "services/AuthService.h"
#include "repositories/MemberRepository.h"
#include "repositories/CheckInRepository.h"
#include "repositories/QuestionRepository.h"
#include "repositories/KnowledgePointRepository.h"
#include "repositories/AuditRepository.h"
#include "repositories/UserRepository.h"
#include "crypto/AesGcmCipher.h"
#include "utils/Validation.h"
#include "models/Member.h"
#include "models/CheckIn.h"
#include "models/Question.h"
#include "models/CommonTypes.h"

class TstOperatorWorkflows : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    // ── Check-in workflows ──────────────────────────────────────────────────
    void test_checkin_successReducesBalance();
    void test_checkin_duplicateBlockedWithin30s();
    void test_checkin_frozenMemberBlocked();
    void test_checkin_expiredTermCardBlocked();
    void test_checkin_exhaustedPunchCardBlocked();

    // ── Correction workflow ─────────────────────────────────────────────────
    void test_correction_requestAndApply_restoresBalance();
    void test_correction_doubleReversal_rejected();

    // ── Question governance workflows ────────────────────────────────────────
    void test_question_createAndQuery();
    void test_question_softDeleteExcludedFromQuery();
    void test_question_kpMappingAndSubtreeFilter();
    void test_question_tagFilter();
    void test_question_externalIdDuplicateRejected();

private:
    void applySchema();
    QString createMember(const QString& memberId);
    void createTermCard(const QString& memberId, int daysOffset = 30);
    void createPunchCard(const QString& memberId, int balance = 5);
    QString createQuestion(const QString& body, int difficulty = 3);

    QSqlDatabase        m_db;
    QByteArray          m_masterKey;

    // Repos (raw pointers; owned by unique_ptrs below)
    MemberRepository*        m_memberRepo{nullptr};
    CheckInRepository*       m_checkInRepo{nullptr};
    QuestionRepository*      m_questionRepo{nullptr};
    KnowledgePointRepository* m_kpRepo{nullptr};
    AuditRepository*         m_auditRepo{nullptr};
    UserRepository*          m_userRepo{nullptr};

    // Services
    AesGcmCipher*    m_cipher{nullptr};
    AuditService*    m_auditService{nullptr};
    AuthService*     m_authService{nullptr};
    CheckInService*  m_checkInService{nullptr};
    QuestionService* m_questionService{nullptr};

    // Owned storage
    std::unique_ptr<AesGcmCipher>             m_cipherOwned;
    std::unique_ptr<MemberRepository>         m_memberRepoOwned;
    std::unique_ptr<CheckInRepository>        m_checkInRepoOwned;
    std::unique_ptr<QuestionRepository>       m_questionRepoOwned;
    std::unique_ptr<KnowledgePointRepository> m_kpRepoOwned;
    std::unique_ptr<AuditRepository>          m_auditRepoOwned;
    std::unique_ptr<UserRepository>           m_userRepoOwned;
    std::unique_ptr<AuditService>             m_auditServiceOwned;
    std::unique_ptr<AuthService>              m_authServiceOwned;
    std::unique_ptr<CheckInService>           m_checkInServiceOwned;
    std::unique_ptr<QuestionService>          m_questionServiceOwned;
};

void TstOperatorWorkflows::initTestCase()
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("tst_operator_workflows"));
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(m_db.open());
    applySchema();

    m_masterKey = QByteArray(32, '\xAB');
    m_cipherOwned         = std::make_unique<AesGcmCipher>(m_masterKey);
    m_memberRepoOwned     = std::make_unique<MemberRepository>(m_db);
    m_checkInRepoOwned    = std::make_unique<CheckInRepository>(m_db);
    m_questionRepoOwned   = std::make_unique<QuestionRepository>(m_db);
    m_kpRepoOwned         = std::make_unique<KnowledgePointRepository>(m_db);
    m_auditRepoOwned      = std::make_unique<AuditRepository>(m_db);
    m_userRepoOwned       = std::make_unique<UserRepository>(m_db);
    m_auditServiceOwned   = std::make_unique<AuditService>(*m_auditRepoOwned, *m_cipherOwned);
    m_authServiceOwned    = std::make_unique<AuthService>(*m_userRepoOwned, *m_auditRepoOwned);
    m_checkInServiceOwned = std::make_unique<CheckInService>(
        *m_memberRepoOwned, *m_checkInRepoOwned,
        *m_authServiceOwned, *m_auditServiceOwned, *m_cipherOwned, m_db);
    m_questionServiceOwned = std::make_unique<QuestionService>(
        *m_questionRepoOwned, *m_kpRepoOwned, *m_auditServiceOwned, *m_authServiceOwned);

    m_cipher          = m_cipherOwned.get();
    m_memberRepo      = m_memberRepoOwned.get();
    m_checkInRepo     = m_checkInRepoOwned.get();
    m_questionRepo    = m_questionRepoOwned.get();
    m_kpRepo          = m_kpRepoOwned.get();
    m_auditRepo       = m_auditRepoOwned.get();
    m_userRepo        = m_userRepoOwned.get();
    m_auditService    = m_auditServiceOwned.get();
    m_authService     = m_authServiceOwned.get();
    m_checkInService  = m_checkInServiceOwned.get();
    m_questionService = m_questionServiceOwned.get();
}

void TstOperatorWorkflows::cleanupTestCase()
{
    m_db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("tst_operator_workflows"));
}

void TstOperatorWorkflows::init()
{
    // Clear data between tests
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("DELETE FROM checkin_attempts"));
    q.exec(QStringLiteral("DELETE FROM deduction_events"));
    q.exec(QStringLiteral("DELETE FROM correction_requests"));
    q.exec(QStringLiteral("DELETE FROM correction_approvals"));
    q.exec(QStringLiteral("DELETE FROM step_up_windows"));
    q.exec(QStringLiteral("DELETE FROM user_sessions"));
    q.exec(QStringLiteral("DELETE FROM captcha_states"));
    q.exec(QStringLiteral("DELETE FROM lockout_records"));
    q.exec(QStringLiteral("DELETE FROM credentials"));
    q.exec(QStringLiteral("DELETE FROM members"));
    q.exec(QStringLiteral("DELETE FROM term_cards"));
    q.exec(QStringLiteral("DELETE FROM punch_cards"));
    q.exec(QStringLiteral("DELETE FROM member_freeze_records"));
    q.exec(QStringLiteral("DELETE FROM questions"));
    q.exec(QStringLiteral("DELETE FROM question_kp_mappings"));
    q.exec(QStringLiteral("DELETE FROM question_tag_mappings"));
    q.exec(QStringLiteral("DELETE FROM knowledge_points"));
    q.exec(QStringLiteral("DELETE FROM tags"));
    q.exec(QStringLiteral("DELETE FROM users"));

    // Seed actors used by workflow tests.
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    q.prepare(QStringLiteral(
        "INSERT INTO users (id, username, role, status, created_at, updated_at, created_by_user_id) "
        "VALUES (?, ?, ?, 'Active', ?, ?, NULL)"));
    q.addBindValue(QStringLiteral("operator-1"));
    q.addBindValue(QStringLiteral("operator"));
    q.addBindValue(QStringLiteral("FRONT_DESK_OPERATOR"));
    q.addBindValue(now);
    q.addBindValue(now);
    QVERIFY(q.exec());

    q.prepare(QStringLiteral(
        "INSERT INTO users (id, username, role, status, created_at, updated_at, created_by_user_id) "
        "VALUES (?, ?, ?, 'Active', ?, ?, NULL)"));
    q.addBindValue(QStringLiteral("admin-1"));
    q.addBindValue(QStringLiteral("admin"));
    q.addBindValue(QStringLiteral("SECURITY_ADMINISTRATOR"));
    q.addBindValue(now);
    q.addBindValue(now);
    QVERIFY(q.exec());

    q.prepare(QStringLiteral(
        "INSERT INTO users (id, username, role, status, created_at, updated_at, created_by_user_id) "
        "VALUES (?, ?, ?, 'Active', ?, ?, NULL)"));
    q.addBindValue(QStringLiteral("test-actor"));
    q.addBindValue(QStringLiteral("content-manager"));
    q.addBindValue(QStringLiteral("CONTENT_MANAGER"));
    q.addBindValue(now);
    q.addBindValue(now);
    QVERIFY(q.exec());
}

void TstOperatorWorkflows::applySchema()
{
    QSqlQuery q(m_db);
    // Identity
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS users ("
        "id TEXT PRIMARY KEY, username TEXT NOT NULL UNIQUE, role TEXT NOT NULL, "
        "status TEXT NOT NULL DEFAULT 'Active', created_at TEXT NOT NULL, "
        "updated_at TEXT NOT NULL, created_by_user_id TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS credentials ("
        "user_id TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE, "
        "algorithm TEXT NOT NULL DEFAULT 'argon2id', time_cost INTEGER NOT NULL, "
        "memory_cost INTEGER NOT NULL, parallelism INTEGER NOT NULL, "
        "tag_length INTEGER NOT NULL, salt_hex TEXT NOT NULL, hash_hex TEXT NOT NULL, "
        "updated_at TEXT NOT NULL)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS user_sessions ("
        "token TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE, "
        "created_at TEXT NOT NULL, last_active_at TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS lockout_records ("
        "username TEXT PRIMARY KEY, failed_attempts INTEGER NOT NULL DEFAULT 0, "
        "first_fail_at TEXT, locked_at TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS captcha_states ("
        "username TEXT PRIMARY KEY, challenge_id TEXT NOT NULL, answer_hash_hex TEXT NOT NULL, "
        "issued_at TEXT NOT NULL, expires_at TEXT NOT NULL, solve_attempts INTEGER NOT NULL DEFAULT 0, "
        "solved INTEGER NOT NULL DEFAULT 0)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS step_up_windows ("
        "id TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE, "
        "session_token TEXT NOT NULL REFERENCES user_sessions(token) ON DELETE CASCADE, "
        "granted_at TEXT NOT NULL, expires_at TEXT NOT NULL, consumed INTEGER NOT NULL DEFAULT 0)"));

    // Members
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS members ("
        "id TEXT PRIMARY KEY, member_id TEXT UNIQUE, member_id_hash TEXT, barcode_encrypted TEXT, "
        "mobile_encrypted TEXT, name_encrypted TEXT, deleted INTEGER DEFAULT 0, "
        "created_at TEXT, updated_at TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS term_cards ("
        "id TEXT PRIMARY KEY, member_id TEXT, term_start TEXT, term_end TEXT, "
        "active INTEGER DEFAULT 1, created_at TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS punch_cards ("
        "id TEXT PRIMARY KEY, member_id TEXT, product_code TEXT, "
        "initial_balance INTEGER, current_balance INTEGER, "
        "created_at TEXT, updated_at TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS member_freeze_records ("
        "id TEXT PRIMARY KEY, member_id TEXT, reason TEXT, "
        "frozen_by_user_id TEXT, frozen_at TEXT, "
        "thawed_by_user_id TEXT, thawed_at TEXT)"));
    // Check-in
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS checkin_attempts ("
        "id TEXT PRIMARY KEY, member_id TEXT, session_id TEXT, "
        "operator_user_id TEXT, status TEXT, attempted_at TEXT, "
        "deduction_event_id TEXT, failure_reason TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS deduction_events ("
        "id TEXT PRIMARY KEY, member_id TEXT, punch_card_id TEXT, "
        "checkin_attempt_id TEXT, sessions_deducted INTEGER, "
        "balance_before INTEGER, balance_after INTEGER, deducted_at TEXT, "
        "reversed_by_correction_id TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS correction_requests ("
        "id TEXT PRIMARY KEY, deduction_event_id TEXT, "
        "requested_by_user_id TEXT, rationale TEXT, status TEXT, created_at TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS correction_approvals ("
        "correction_request_id TEXT PRIMARY KEY, approved_by_user_id TEXT, "
        "step_up_window_id TEXT, rationale TEXT, approved_at TEXT, "
        "before_payload_json TEXT, after_payload_json TEXT)"));
    // Questions
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS questions ("
        "id TEXT PRIMARY KEY, body_text TEXT, answer_options_json TEXT, "
        "correct_answer_index INTEGER, difficulty INTEGER, discrimination REAL, "
        "status TEXT DEFAULT 'Draft', external_id TEXT, "
        "created_at TEXT, updated_at TEXT, "
        "created_by_user_id TEXT, updated_by_user_id TEXT, "
        "CONSTRAINT uq_questions_external_id UNIQUE (external_id))"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS knowledge_points ("
        "id TEXT PRIMARY KEY, name TEXT, parent_id TEXT, position INTEGER, "
        "path TEXT, created_at TEXT, deleted INTEGER DEFAULT 0)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS question_kp_mappings ("
        "question_id TEXT, knowledge_point_id TEXT, mapped_at TEXT, "
        "mapped_by_user_id TEXT, PRIMARY KEY (question_id, knowledge_point_id))"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS tags ("
        "id TEXT PRIMARY KEY, name TEXT UNIQUE, created_at TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS question_tag_mappings ("
        "question_id TEXT, tag_id TEXT, applied_at TEXT, applied_by_user_id TEXT, "
        "PRIMARY KEY (question_id, tag_id))"));
    // Audit
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS audit_entries ("
        "id TEXT PRIMARY KEY, timestamp TEXT, actor_user_id TEXT, "
        "event_type TEXT, entity_type TEXT, entity_id TEXT, "
        "before_payload_json TEXT, after_payload_json TEXT, "
        "previous_entry_hash TEXT, entry_hash TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS audit_chain_head ("
        "id INTEGER PRIMARY KEY CHECK (id = 1), "
        "last_entry_id TEXT, last_entry_hash TEXT NOT NULL DEFAULT '')"));
    q.exec(QStringLiteral(
        "INSERT OR IGNORE INTO audit_chain_head (id, last_entry_id, last_entry_hash) "
        "VALUES (1, NULL, '')"));
}

QString TstOperatorWorkflows::createMember(const QString& memberId)
{
    auto enc = m_cipher->encrypt(QStringLiteral("Test Member"), QByteArray("member.name"));
    auto encBarcode = m_cipher->encrypt(QStringLiteral("BAR") + memberId, QByteArray("member.barcode"));
    auto encMobile  = m_cipher->encrypt(QStringLiteral("(555) 000-0001"), QByteArray("member.mobile"));

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO members (id, member_id, member_id_hash, barcode_encrypted, mobile_encrypted, "
        "name_encrypted, deleted, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, 0, ?, ?)"));
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    q.addBindValue(id);
    q.addBindValue(memberId);
    const QString memberIdHash = QString::fromLatin1(
        QCryptographicHash::hash(memberId.trimmed().toUtf8(), QCryptographicHash::Sha256).toHex());
    q.addBindValue(memberIdHash);
    q.addBindValue(encBarcode.isOk() ? encBarcode.value().toBase64() : QStringLiteral("INVALID"));
    q.addBindValue(encMobile.isOk() ? encMobile.value().toBase64() : QStringLiteral("INVALID"));
    q.addBindValue(enc.isOk() ? enc.value().toBase64() : QStringLiteral("INVALID"));
    q.addBindValue(now);
    q.addBindValue(now);
    const bool inserted = q.exec();
    if (!inserted) {
        qWarning() << "createMember insert failed:" << q.lastError().text();
        return QString{};
    }
    return id;
}

void TstOperatorWorkflows::createTermCard(const QString& memberId, int daysOffset)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO term_cards (id, member_id, term_start, term_end, active, created_at) "
        "VALUES (?, ?, ?, ?, 1, ?)"));
    q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.addBindValue(memberId);
    q.addBindValue(QDate::currentDate().addDays(-1).toString(Qt::ISODate));
    q.addBindValue(QDate::currentDate().addDays(daysOffset).toString(Qt::ISODate));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.exec();
}

void TstOperatorWorkflows::createPunchCard(const QString& memberId, int balance)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO punch_cards (id, member_id, product_code, initial_balance, "
        "current_balance, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)"));
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.addBindValue(memberId);
    q.addBindValue(QStringLiteral("STANDARD"));
    q.addBindValue(balance);
    q.addBindValue(balance);
    q.addBindValue(now);
    q.addBindValue(now);
    q.exec();
}

QString TstOperatorWorkflows::createQuestion(const QString& body, int difficulty)
{
    Question q;
    q.bodyText = body;
    q.answerOptions = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
    q.correctAnswerIndex = 0;
    q.difficulty = difficulty;
    q.discrimination = 0.5;
    q.status = QuestionStatus::Active;
    auto result = m_questionService->createQuestion(q, QStringLiteral("test-actor"));
    return result.isOk() ? result.value().id : QString{};
}

// ── Check-in workflow tests ──────────────────────────────────────────────────

void TstOperatorWorkflows::test_checkin_successReducesBalance()
{
    const QString internalId = createMember(QStringLiteral("M-001"));
    createTermCard(internalId);
    createPunchCard(internalId, 3);

    MemberIdentifier id{MemberIdentifier::Type::MemberId, QStringLiteral("M-001")};
    auto result = m_checkInService->checkIn(id, QStringLiteral("SESSION-1"),
                                             QStringLiteral("operator-1"));
    QVERIFY(result.isOk());
    QCOMPARE(result.value().remainingBalance, 2);
}

void TstOperatorWorkflows::test_checkin_duplicateBlockedWithin30s()
{
    const QString internalId = createMember(QStringLiteral("M-002"));
    createTermCard(internalId);
    createPunchCard(internalId, 5);

    MemberIdentifier id{MemberIdentifier::Type::MemberId, QStringLiteral("M-002")};
    auto first  = m_checkInService->checkIn(id, QStringLiteral("SESSION-2"),
                                             QStringLiteral("operator-1"));
    QVERIFY(first.isOk());

    // Immediate second attempt — should be duplicate-blocked
    auto second = m_checkInService->checkIn(id, QStringLiteral("SESSION-2"),
                                             QStringLiteral("operator-1"));
    QVERIFY(!second.isOk());
}

void TstOperatorWorkflows::test_checkin_frozenMemberBlocked()
{
    const QString internalId = createMember(QStringLiteral("M-003"));
    createTermCard(internalId);
    createPunchCard(internalId, 5);

    // Insert freeze record
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO member_freeze_records "
        "(id, member_id, reason, frozen_by_user_id, frozen_at) VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.addBindValue(internalId);
    q.addBindValue(QStringLiteral("Non-payment"));
    q.addBindValue(QStringLiteral("admin-1"));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    QVERIFY(q.exec());

    MemberIdentifier id{MemberIdentifier::Type::MemberId, QStringLiteral("M-003")};
    auto result = m_checkInService->checkIn(id, QStringLiteral("SESSION-3"),
                                             QStringLiteral("operator-1"));
    QVERIFY(!result.isOk());
}

void TstOperatorWorkflows::test_checkin_expiredTermCardBlocked()
{
    const QString internalId = createMember(QStringLiteral("M-004"));
    createTermCard(internalId, -1);  // expired yesterday
    createPunchCard(internalId, 5);

    MemberIdentifier id{MemberIdentifier::Type::MemberId, QStringLiteral("M-004")};
    auto result = m_checkInService->checkIn(id, QStringLiteral("SESSION-4"),
                                             QStringLiteral("operator-1"));
    QVERIFY(!result.isOk());
}

void TstOperatorWorkflows::test_checkin_exhaustedPunchCardBlocked()
{
    const QString internalId = createMember(QStringLiteral("M-005"));
    createTermCard(internalId);
    createPunchCard(internalId, 0);  // zero balance

    MemberIdentifier id{MemberIdentifier::Type::MemberId, QStringLiteral("M-005")};
    auto result = m_checkInService->checkIn(id, QStringLiteral("SESSION-5"),
                                             QStringLiteral("operator-1"));
    QVERIFY(!result.isOk());
}

// ── Correction workflow tests ────────────────────────────────────────────────

void TstOperatorWorkflows::test_correction_requestAndApply_restoresBalance()
{
    const QString internalId = createMember(QStringLiteral("M-010"));
    createTermCard(internalId);
    createPunchCard(internalId, 3);

    // Check in once — balance 3 → 2
    MemberIdentifier mid{MemberIdentifier::Type::MemberId, QStringLiteral("M-010")};
    auto checkIn = m_checkInService->checkIn(mid, QStringLiteral("SESSION-10"),
                                              QStringLiteral("operator-1"));
    QVERIFY(checkIn.isOk());
    const QString deductionId = checkIn.value().deductionEventId;
    QCOMPARE(checkIn.value().remainingBalance, 2);

    // Request correction
    auto corrReq = m_checkInService->requestCorrection(
        deductionId, QStringLiteral("Wrong member scanned"), QStringLiteral("operator-1"));
    QVERIFY(corrReq.isOk());

    const QString corrId = corrReq.value().id;
    const QString stepUpId1 = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString sessionToken1 = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now1 = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QString expires1 = QDateTime::currentDateTimeUtc()
                                 .addSecs(Validation::StepUpWindowSeconds)
                                 .toString(Qt::ISODateWithMs);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO user_sessions (token, user_id, created_at, last_active_at, active) "
        "VALUES (?, ?, ?, ?, 1)"));
    q.addBindValue(sessionToken1);
    q.addBindValue(QStringLiteral("admin-1"));
    q.addBindValue(now1);
    q.addBindValue(now1);
    QVERIFY(q.exec());

    q.prepare(QStringLiteral(
        "INSERT INTO step_up_windows (id, user_id, session_token, granted_at, expires_at, consumed) "
        "VALUES (?, ?, ?, ?, ?, 0)"));
    q.addBindValue(stepUpId1);
    q.addBindValue(QStringLiteral("admin-1"));
    q.addBindValue(sessionToken1);
    q.addBindValue(now1);
    q.addBindValue(expires1);
    QVERIFY(q.exec());

    auto approveResult = m_checkInService->approveCorrection(
        corrId,
        QStringLiteral("Approved"),
        QStringLiteral("admin-1"),
        stepUpId1);
    QVERIFY(approveResult.isOk());

    const QString stepUpId2 = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString sessionToken2 = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now2 = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QString expires2 = QDateTime::currentDateTimeUtc()
                                 .addSecs(Validation::StepUpWindowSeconds)
                                 .toString(Qt::ISODateWithMs);
    q.prepare(QStringLiteral(
        "INSERT INTO user_sessions (token, user_id, created_at, last_active_at, active) "
        "VALUES (?, ?, ?, ?, 1)"));
    q.addBindValue(sessionToken2);
    q.addBindValue(QStringLiteral("admin-1"));
    q.addBindValue(now2);
    q.addBindValue(now2);
    QVERIFY(q.exec());

    q.prepare(QStringLiteral(
        "INSERT INTO step_up_windows (id, user_id, session_token, granted_at, expires_at, consumed) "
        "VALUES (?, ?, ?, ?, ?, 0)"));
    q.addBindValue(stepUpId2);
    q.addBindValue(QStringLiteral("admin-1"));
    q.addBindValue(sessionToken2);
    q.addBindValue(now2);
    q.addBindValue(expires2);
    QVERIFY(q.exec());

    auto applyResult = m_checkInService->applyCorrection(
        corrId,
        QStringLiteral("admin-1"),
        stepUpId2);
    QVERIFY(applyResult.isOk());

    // Verify balance restored (2 → 3)
    auto punchCards = m_memberRepo->getActivePunchCards(internalId);
    QVERIFY(punchCards.isOk());
    QVERIFY(!punchCards.value().isEmpty());
    QCOMPARE(punchCards.value().first().currentBalance, 3);
}

void TstOperatorWorkflows::test_correction_doubleReversal_rejected()
{
    const QString internalId = createMember(QStringLiteral("M-011"));
    createTermCard(internalId);
    createPunchCard(internalId, 2);

    MemberIdentifier mid{MemberIdentifier::Type::MemberId, QStringLiteral("M-011")};
    auto checkIn = m_checkInService->checkIn(mid, QStringLiteral("SESSION-11"),
                                              QStringLiteral("operator-1"));
    QVERIFY(checkIn.isOk());
    const QString deductionId = checkIn.value().deductionEventId;

    // Request correction once
    auto corrReq1 = m_checkInService->requestCorrection(
        deductionId, QStringLiteral("First correction"), QStringLiteral("operator-1"));
    QVERIFY(corrReq1.isOk());

    const QString stepUpId1 = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString sessionToken1 = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now1 = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QString expires1 = QDateTime::currentDateTimeUtc()
                                 .addSecs(Validation::StepUpWindowSeconds)
                                 .toString(Qt::ISODateWithMs);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO user_sessions (token, user_id, created_at, last_active_at, active) "
        "VALUES (?, ?, ?, ?, 1)"));
    q.addBindValue(sessionToken1);
    q.addBindValue(QStringLiteral("admin-1"));
    q.addBindValue(now1);
    q.addBindValue(now1);
    QVERIFY(q.exec());
    q.prepare(QStringLiteral(
        "INSERT INTO step_up_windows (id, user_id, session_token, granted_at, expires_at, consumed) "
        "VALUES (?, ?, ?, ?, ?, 0)"));
    q.addBindValue(stepUpId1);
    q.addBindValue(QStringLiteral("admin-1"));
    q.addBindValue(sessionToken1);
    q.addBindValue(now1);
    q.addBindValue(expires1);
    QVERIFY(q.exec());

    const auto approve1 = m_checkInService->approveCorrection(
        corrReq1.value().id,
        QStringLiteral("Approved"),
        QStringLiteral("admin-1"),
        stepUpId1);
    QVERIFY(approve1.isOk());

    const QString stepUpId2 = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString sessionToken2 = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now2 = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QString expires2 = QDateTime::currentDateTimeUtc()
                                 .addSecs(Validation::StepUpWindowSeconds)
                                 .toString(Qt::ISODateWithMs);
    q.prepare(QStringLiteral(
        "INSERT INTO user_sessions (token, user_id, created_at, last_active_at, active) "
        "VALUES (?, ?, ?, ?, 1)"));
    q.addBindValue(sessionToken2);
    q.addBindValue(QStringLiteral("admin-1"));
    q.addBindValue(now2);
    q.addBindValue(now2);
    QVERIFY(q.exec());
    q.prepare(QStringLiteral(
        "INSERT INTO step_up_windows (id, user_id, session_token, granted_at, expires_at, consumed) "
        "VALUES (?, ?, ?, ?, ?, 0)"));
    q.addBindValue(stepUpId2);
    q.addBindValue(QStringLiteral("admin-1"));
    q.addBindValue(sessionToken2);
    q.addBindValue(now2);
    q.addBindValue(expires2);
    QVERIFY(q.exec());

    const auto apply1 = m_checkInService->applyCorrection(
        corrReq1.value().id,
        QStringLiteral("admin-1"),
        stepUpId2);
    QVERIFY(apply1.isOk());

    // Second request on same deduction — should be rejected
    auto corrReq2 = m_checkInService->requestCorrection(
        deductionId, QStringLiteral("Double reversal attempt"), QStringLiteral("operator-1"));
    QVERIFY(!corrReq2.isOk());
}

// ── Question governance tests ────────────────────────────────────────────────

void TstOperatorWorkflows::test_question_createAndQuery()
{
    const QString qid = createQuestion(QStringLiteral("What is the speed of light?"), 4);
    QVERIFY(!qid.isEmpty());

    QuestionFilter filter;
    filter.statusFilter = QuestionStatus::Draft;
    auto result = m_questionService->queryQuestions(filter);
    QVERIFY(result.isOk());
    QVERIFY(!result.value().isEmpty());
    QCOMPARE(result.value().first().bodyText, QStringLiteral("What is the speed of light?"));
}

void TstOperatorWorkflows::test_question_softDeleteExcludedFromQuery()
{
    const QString qid = createQuestion(QStringLiteral("To be deleted question"), 2);
    QVERIFY(!qid.isEmpty());

    auto delResult = m_questionService->deleteQuestion(qid, QStringLiteral("test-actor"));
    QVERIFY(delResult.isOk());

    QuestionFilter filter;
    filter.statusFilter = QuestionStatus::Active;
    auto result = m_questionService->queryQuestions(filter);
    QVERIFY(result.isOk());
    for (const auto& q : result.value()) {
        QVERIFY(q.id != qid);
    }
}

void TstOperatorWorkflows::test_question_kpMappingAndSubtreeFilter()
{
    // Create a KP
    KnowledgePoint kp;
    kp.name = QStringLiteral("Physics");
    kp.position = 0;
    auto kpResult = m_questionService->createKnowledgePoint(kp, QStringLiteral("test-actor"));
    QVERIFY(kpResult.isOk());
    const QString kpId = kpResult.value().id;

    // Create a question
    const QString qid = createQuestion(QStringLiteral("Newton's first law"), 3);
    QVERIFY(!qid.isEmpty());

    // Map question to KP
    auto mapResult = m_questionService->mapQuestionToKP(qid, kpId, QStringLiteral("test-actor"));
    QVERIFY(mapResult.isOk());

    // Query by KP subtree — should find our question
    QuestionFilter filter;
    filter.knowledgePointId = kpId;
    filter.statusFilter = QuestionStatus::Draft;
    auto result = m_questionService->queryQuestions(filter);
    QVERIFY(result.isOk());
    bool found = false;
    for (const auto& q : result.value()) {
        if (q.id == qid) { found = true; break; }
    }
    QVERIFY(found);
}

void TstOperatorWorkflows::test_question_tagFilter()
{
    const QString qid = createQuestion(QStringLiteral("Tagged question"), 3);
    QVERIFY(!qid.isEmpty());

    auto tagResult = m_questionService->createTag(QStringLiteral("safety"), QStringLiteral("test-actor"));
    QVERIFY(tagResult.isOk());
    const QString tagId = tagResult.value().id;

    auto applyResult = m_questionService->applyTag(qid, tagId, QStringLiteral("test-actor"));
    QVERIFY(applyResult.isOk());

    QuestionFilter filter;
    filter.tagIds.append(tagId);
    filter.statusFilter = QuestionStatus::Draft;
    auto result = m_questionService->queryQuestions(filter);
    QVERIFY(result.isOk());
    bool found = false;
    for (const auto& q : result.value()) {
        if (q.id == qid) { found = true; break; }
    }
    QVERIFY(found);
}

void TstOperatorWorkflows::test_question_externalIdDuplicateRejected()
{
    Question q1;
    q1.bodyText = QStringLiteral("Question with external ID");
    q1.answerOptions = {QStringLiteral("A"), QStringLiteral("B")};
    q1.correctAnswerIndex = 0;
    q1.difficulty = 3;
    q1.discrimination = 0.5;
    q1.status = QuestionStatus::Active;
    q1.externalId = QStringLiteral("EXT-001");
    auto r1 = m_questionService->createQuestion(q1, QStringLiteral("test-actor"));
    QVERIFY(r1.isOk());

    Question q2 = q1;
    q2.bodyText = QStringLiteral("Different body, same external ID");
    auto r2 = m_questionService->createQuestion(q2, QStringLiteral("test-actor"));
    QVERIFY(!r2.isOk());  // duplicate externalId rejected
}

QTEST_MAIN(TstOperatorWorkflows)
#include "tst_operator_workflows.moc"
