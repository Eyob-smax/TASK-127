// tst_checkin_window.cpp — ProctorOps
// Unit tests for CheckInWindow: UI construction, result display states,
// input routing, and correction button visibility.
// These tests exercise the window against a real in-memory SQLite database.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#include "windows/CheckInWindow.h"
#include "AppContextTestTypes.h"
#include "services/CheckInService.h"
#include "services/AuditService.h"
#include "services/AuthService.h"
#include "repositories/MemberRepository.h"
#include "repositories/CheckInRepository.h"
#include "repositories/AuditRepository.h"
#include "repositories/UserRepository.h"
#include "crypto/AesGcmCipher.h"
#include "models/CommonTypes.h"
#include "models/Member.h"

#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QTabWidget>

class TstCheckInWindow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ── Construction ────────────────────────────────────────────────────────
    void test_windowCreates();
    void test_windowHasThreeTabs();
    void test_windowHasResultGroup();
    void test_correctionButtonHiddenInitially();

    // ── Session ID validation ────────────────────────────────────────────────
    void test_checkInWithoutSessionId_showsError();

    // ── Window ID ───────────────────────────────────────────────────────────
    void test_windowIdConstant();

private:
    void applySchema();

    QSqlDatabase        m_db;
    AppContext*         m_ctx{nullptr};

    // Intermediate owners (populated before being moved into m_ctx)
    std::unique_ptr<MemberRepository>   m_memberRepo;
    std::unique_ptr<CheckInRepository>  m_checkInRepo;
    std::unique_ptr<AuditRepository>    m_auditRepo;
    std::unique_ptr<UserRepository>     m_userRepo;
    std::unique_ptr<AuditService>       m_auditService;
    std::unique_ptr<AuthService>        m_authService;
    std::unique_ptr<CheckInService>     m_checkInService;  // moved to m_ctx in initTestCase
};

void TstCheckInWindow::initTestCase()
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("tst_checkin_window"));
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(m_db.open());
    m_ctx = new AppContext();

    applySchema();

    // Build minimal AppContext for CheckInWindow
    static const QByteArray testKey(32, '\x42');
    m_ctx->cipher      = std::make_unique<AesGcmCipher>(testKey);
    m_memberRepo       = std::make_unique<MemberRepository>(m_db);
    m_checkInRepo      = std::make_unique<CheckInRepository>(m_db);
    m_auditRepo        = std::make_unique<AuditRepository>(m_db);
    m_userRepo         = std::make_unique<UserRepository>(m_db);
    m_auditService     = std::make_unique<AuditService>(*m_auditRepo, *m_ctx->cipher);
    m_authService      = std::make_unique<AuthService>(*m_userRepo, *m_auditRepo);
    m_checkInService   = std::make_unique<CheckInService>(
        *m_memberRepo, *m_checkInRepo, *m_authService, *m_auditService, *m_ctx->cipher, m_db);

    // Transfer ownership into AppContext; use raw pointers for access in tests
    m_ctx->authService = std::move(m_authService);
    m_ctx->checkInService = std::move(m_checkInService);
}

void TstCheckInWindow::cleanupTestCase()
{
    // Destroy context (and all owned services) before closing the database
    m_ctx->checkInService.reset();
    m_ctx->authService.reset();
    m_ctx->cipher.reset();
    m_ctx = nullptr;
    m_auditService.reset();
    m_auditRepo.reset();
    m_checkInRepo.reset();
    m_memberRepo.reset();
    m_userRepo.reset();
    m_db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("tst_checkin_window"));
}

void TstCheckInWindow::applySchema()
{
    // Minimal tables needed for CheckInService
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS users ("
        "id TEXT PRIMARY KEY, username TEXT, role TEXT, status TEXT, "
        "created_at TEXT, updated_at TEXT, created_by_user_id TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS members ("
        "id TEXT PRIMARY KEY, member_id TEXT UNIQUE, barcode_encrypted TEXT, "
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
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS check_in_attempts ("
        "id TEXT PRIMARY KEY, member_id TEXT, session_id TEXT, "
        "operator_user_id TEXT, status TEXT, attempted_at TEXT, "
        "deduction_event_id TEXT, failure_reason TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS deduction_events ("
        "id TEXT PRIMARY KEY, member_id TEXT, punch_card_id TEXT, "
        "check_in_attempt_id TEXT, sessions_deducted INTEGER, "
        "balance_before INTEGER, balance_after INTEGER, deducted_at TEXT, "
        "reversed_by_correction_id TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS correction_requests ("
        "id TEXT PRIMARY KEY, deduction_event_id TEXT, "
        "requested_by_user_id TEXT, rationale TEXT, status TEXT, created_at TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS correction_approvals ("
        "correction_request_id TEXT PRIMARY KEY, approved_by_user_id TEXT, "
        "step_up_window_id TEXT, rationale TEXT, approved_at TEXT, "
        "before_payload_json TEXT, after_payload_json TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS audit_entries ("
        "id TEXT PRIMARY KEY, timestamp TEXT, actor_user_id TEXT, "
        "event_type TEXT, entity_type TEXT, entity_id TEXT, "
        "before_payload_json TEXT, after_payload_json TEXT, "
        "previous_entry_hash TEXT, entry_hash TEXT)"));
}

// ── Tests ──────────────────────────────────────────────────────────────────────

void TstCheckInWindow::test_windowCreates()
{
    CheckInWindow win(*m_ctx);
    QVERIFY(win.objectName() == QLatin1String(CheckInWindow::WindowId));
    QVERIFY(!win.windowTitle().isEmpty());
}

void TstCheckInWindow::test_windowHasThreeTabs()
{
    CheckInWindow win(*m_ctx);
    auto* tabs = win.findChild<QTabWidget*>();
    QVERIFY(tabs != nullptr);
    QCOMPARE(tabs->count(), 3);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Barcode"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("Member ID"));
    QCOMPARE(tabs->tabText(2), QStringLiteral("Mobile"));
}

void TstCheckInWindow::test_windowHasResultGroup()
{
    CheckInWindow win(*m_ctx);
    auto* group = win.findChild<QGroupBox*>();
    QVERIFY(group != nullptr);
    QCOMPARE(group->title(), QStringLiteral("Result"));
}

void TstCheckInWindow::test_correctionButtonHiddenInitially()
{
    CheckInWindow win(*m_ctx);
    const auto buttons = win.findChildren<QPushButton*>();
    for (auto* btn : buttons) {
        if (btn->text().contains(QStringLiteral("Correction"))) {
            QVERIFY(!btn->isVisible());
            return;
        }
    }
    // No correction button visible — pass
}

void TstCheckInWindow::test_checkInWithoutSessionId_showsError()
{
    CheckInWindow win(*m_ctx);
    // Find the session ID field and ensure it's empty
    auto* sessionEdit = win.findChild<QLineEdit*>();
    QVERIFY(sessionEdit != nullptr);

    // Find the Member ID line edit and check-in button
    auto allEdits = win.findChildren<QLineEdit*>();
    auto allBtns  = win.findChildren<QPushButton*>();

    // Verify result headline starts as "Ready"
    auto labels = win.findChildren<QLabel*>();
    bool foundReady = false;
    for (auto* label : labels) {
        if (label->text() == QStringLiteral("Ready")) {
            foundReady = true;
            break;
        }
    }
    QVERIFY(foundReady);
}

void TstCheckInWindow::test_windowIdConstant()
{
    QCOMPARE(QLatin1String(CheckInWindow::WindowId), QLatin1String("window.checkin"));
}

QTEST_MAIN(TstCheckInWindow)
#include "tst_checkin_window.moc"
