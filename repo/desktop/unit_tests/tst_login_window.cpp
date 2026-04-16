// tst_login_window.cpp — ProctorOps
// UI structure and bootstrap-mode tests for LoginWindow.
// Uses real AuthService + repositories over in-memory SQLite.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

#include "windows/LoginWindow.h"
#include "repositories/UserRepository.h"
#include "repositories/AuditRepository.h"
#include "services/AuthService.h"

class TstLoginWindow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    void test_windowStructure();
    void test_captchaHiddenInitially();
    void test_bootstrapModeWhenNoAdmin();
    void test_signInModeWhenAdminExists();

private:
    void applySchema();

    QSqlDatabase m_db;
    std::unique_ptr<UserRepository> m_userRepo;
    std::unique_ptr<AuditRepository> m_auditRepo;
    std::unique_ptr<AuthService> m_authService;
};

void TstLoginWindow::initTestCase()
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("tst_login_window"));
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(m_db.open());

    applySchema();

    m_userRepo = std::make_unique<UserRepository>(m_db);
    m_auditRepo = std::make_unique<AuditRepository>(m_db);
    m_authService = std::make_unique<AuthService>(*m_userRepo, *m_auditRepo);
}

void TstLoginWindow::cleanupTestCase()
{
    m_authService.reset();
    m_auditRepo.reset();
    m_userRepo.reset();

    m_db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("tst_login_window"));
}

void TstLoginWindow::cleanup()
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("DELETE FROM users"));
    q.exec(QStringLiteral("DELETE FROM credentials"));
    q.exec(QStringLiteral("DELETE FROM user_sessions"));
    q.exec(QStringLiteral("DELETE FROM lockout_records"));
    q.exec(QStringLiteral("DELETE FROM captcha_states"));
    q.exec(QStringLiteral("DELETE FROM step_up_windows"));
    q.exec(QStringLiteral("DELETE FROM audit_entries"));
    q.exec(QStringLiteral("DELETE FROM audit_chain_head"));
    q.exec(QStringLiteral("INSERT INTO audit_chain_head (id, last_entry_id, last_entry_hash) VALUES (1, NULL, '')"));
}

void TstLoginWindow::applySchema()
{
    QSqlQuery q(m_db);

    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE users ("
        "id TEXT PRIMARY KEY, username TEXT NOT NULL UNIQUE,"
        "role TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'Active',"
        "created_at TEXT NOT NULL, updated_at TEXT NOT NULL,"
        "created_by_user_id TEXT)")));

    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE credentials ("
        "user_id TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,"
        "algorithm TEXT NOT NULL DEFAULT 'argon2id',"
        "time_cost INTEGER NOT NULL, memory_cost INTEGER NOT NULL,"
        "parallelism INTEGER NOT NULL, tag_length INTEGER NOT NULL,"
        "salt_hex TEXT NOT NULL, hash_hex TEXT NOT NULL, updated_at TEXT NOT NULL)")));

    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE user_sessions ("
        "token TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
        "created_at TEXT NOT NULL, last_active_at TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1)")));

    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE lockout_records ("
        "username TEXT PRIMARY KEY, failed_attempts INTEGER NOT NULL DEFAULT 0,"
        "first_fail_at TEXT, locked_at TEXT)")));

    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE captcha_states ("
        "username TEXT PRIMARY KEY, challenge_id TEXT NOT NULL,"
        "answer_hash_hex TEXT NOT NULL, issued_at TEXT NOT NULL,"
        "expires_at TEXT NOT NULL, solve_attempts INTEGER NOT NULL DEFAULT 0,"
        "solved INTEGER NOT NULL DEFAULT 0)")));

    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE step_up_windows ("
        "id TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
        "session_token TEXT NOT NULL REFERENCES user_sessions(token) ON DELETE CASCADE,"
        "granted_at TEXT NOT NULL, expires_at TEXT NOT NULL, consumed INTEGER NOT NULL DEFAULT 0)")));

    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE audit_entries ("
        "id TEXT PRIMARY KEY, timestamp TEXT NOT NULL, actor_user_id TEXT NOT NULL,"
        "event_type TEXT NOT NULL, entity_type TEXT NOT NULL, entity_id TEXT NOT NULL,"
        "before_payload_json TEXT NOT NULL DEFAULT '{}', after_payload_json TEXT NOT NULL DEFAULT '{}',"
        "previous_entry_hash TEXT NOT NULL, entry_hash TEXT NOT NULL)")));

    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE audit_chain_head ("
        "id INTEGER PRIMARY KEY CHECK (id = 1),"
        "last_entry_id TEXT, last_entry_hash TEXT NOT NULL DEFAULT '')")));

    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO audit_chain_head (id, last_entry_id, last_entry_hash) VALUES (1, NULL, '')")));
}

void TstLoginWindow::test_windowStructure()
{
    LoginWindow win(*m_authService);

    QCOMPARE(win.windowTitle(), QStringLiteral("ProctorOps — Sign In"));

    const auto edits = win.findChildren<QLineEdit*>();
    QVERIFY(edits.size() >= 3);

    bool foundSignInButton = false;
    for (QPushButton* btn : win.findChildren<QPushButton*>()) {
        if (btn->text() == QStringLiteral("Sign In")) {
            foundSignInButton = true;
            break;
        }
    }
    QVERIFY(foundSignInButton);
}

void TstLoginWindow::test_captchaHiddenInitially()
{
    LoginWindow win(*m_authService);
    win.show();
    QTest::qWait(5);

    bool refreshVisible = false;
    for (QPushButton* btn : win.findChildren<QPushButton*>()) {
        if (btn->text() == QStringLiteral("Refresh")) {
            refreshVisible = btn->isVisible();
            break;
        }
    }

    QVERIFY(!refreshVisible);
}

void TstLoginWindow::test_bootstrapModeWhenNoAdmin()
{
    LoginWindow win(*m_authService);
    win.checkBootstrapMode();

    bool foundBootstrapText = false;
    for (QPushButton* btn : win.findChildren<QPushButton*>()) {
        if (btn->text() == QStringLiteral("Create Administrator Account")) {
            foundBootstrapText = true;
            break;
        }
    }

    QVERIFY(foundBootstrapText);
}

void TstLoginWindow::test_signInModeWhenAdminExists()
{
    QSqlQuery q(m_db);
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO users (id, username, role, status, created_at, updated_at, created_by_user_id) "
        "VALUES ('u-admin', 'admin', 'SECURITY_ADMINISTRATOR', 'Active', datetime('now'), datetime('now'), NULL)")));

    LoginWindow win(*m_authService);
    win.checkBootstrapMode();

    bool hasSignIn = false;
    bool hasBootstrapCreate = false;
    for (QPushButton* btn : win.findChildren<QPushButton*>()) {
        if (btn->text() == QStringLiteral("Sign In")) {
            hasSignIn = true;
        }
        if (btn->text() == QStringLiteral("Create Administrator Account")) {
            hasBootstrapCreate = true;
        }
    }

    QVERIFY(hasSignIn);
    QVERIFY(!hasBootstrapCreate);
}

QTEST_MAIN(TstLoginWindow)
#include "tst_login_window.moc"
