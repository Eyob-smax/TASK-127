// tst_step_up_dialog.cpp — ProctorOps
// Dedicated isolated tests for StepUpDialog behavior.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QDateTime>

#include "dialogs/StepUpDialog.h"
#include "services/AuthService.h"
#include "repositories/UserRepository.h"
#include "repositories/AuditRepository.h"
#include "crypto/Argon2idHasher.h"

class TstStepUpDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    void test_dialogStructure();
    void test_confirmEmptyPassword_showsValidationError();
    void test_confirmValidPassword_acceptsAndStoresStepUpId();

private:
    void applySchema();
    QString createActiveSession(const QString& userId, const QString& username, const QString& password);

    QSqlDatabase m_db;
    std::unique_ptr<UserRepository> m_userRepo;
    std::unique_ptr<AuditRepository> m_auditRepo;
    std::unique_ptr<AuthService> m_authService;
};

void TstStepUpDialog::initTestCase()
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("tst_step_up_dialog"));
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(m_db.open());

    applySchema();

    m_userRepo = std::make_unique<UserRepository>(m_db);
    m_auditRepo = std::make_unique<AuditRepository>(m_db);
    m_authService = std::make_unique<AuthService>(*m_userRepo, *m_auditRepo);
}

void TstStepUpDialog::cleanupTestCase()
{
    m_authService.reset();
    m_auditRepo.reset();
    m_userRepo.reset();

    m_db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("tst_step_up_dialog"));
}

void TstStepUpDialog::cleanup()
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("DELETE FROM step_up_windows"));
    q.exec(QStringLiteral("DELETE FROM user_sessions"));
    q.exec(QStringLiteral("DELETE FROM credentials"));
    q.exec(QStringLiteral("DELETE FROM users"));
    q.exec(QStringLiteral("DELETE FROM lockout_records"));
    q.exec(QStringLiteral("DELETE FROM captcha_states"));
    q.exec(QStringLiteral("DELETE FROM audit_entries"));
    q.exec(QStringLiteral("DELETE FROM audit_chain_head"));
    q.exec(QStringLiteral("INSERT INTO audit_chain_head (id, last_entry_id, last_entry_hash) VALUES (1, NULL, '')"));
}

void TstStepUpDialog::applySchema()
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

QString TstStepUpDialog::createActiveSession(const QString& userId,
                                             const QString& username,
                                             const QString& password)
{
    User user;
    user.id = userId;
    user.username = username;
    user.role = Role::SecurityAdministrator;
    user.status = UserStatus::Active;
    user.createdAt = QDateTime::currentDateTimeUtc();
    user.updatedAt = user.createdAt;

    auto insUser = m_userRepo->insertUser(user);
    if (insUser.isErr()) {
        qWarning() << "insertUser failed:" << insUser.errorMessage();
        return {};
    }

    auto hashResult = Argon2idHasher::hashPassword(password);
    if (hashResult.isErr()) {
        qWarning() << "hashPassword failed:" << hashResult.errorMessage();
        return {};
    }

    Credential cred = hashResult.value();
    cred.userId = userId;
    auto upsertCred = m_userRepo->upsertCredential(cred);
    if (upsertCred.isErr()) {
        qWarning() << "upsertCredential failed:" << upsertCred.errorMessage();
        return {};
    }

    UserSession session;
    session.token = QStringLiteral("session-stepup-token");
    session.userId = userId;
    session.createdAt = QDateTime::currentDateTimeUtc();
    session.lastActiveAt = session.createdAt;
    session.active = true;

    auto insertSession = m_userRepo->insertSession(session);
    if (insertSession.isErr()) {
        qWarning() << "insertSession failed:" << insertSession.errorMessage();
        return {};
    }

    return session.token;
}

void TstStepUpDialog::test_dialogStructure()
{
    const QString token = createActiveSession(QStringLiteral("u-admin"),
                                              QStringLiteral("admin"),
                                              QStringLiteral("AdminPass123!"));
    QVERIFY(!token.isEmpty());

    StepUpDialog dlg(*m_authService, token, QStringLiteral("Approve correction"));

    QCOMPARE(dlg.windowTitle(), QStringLiteral("Re-authentication Required"));

    QLineEdit* passwordEdit = dlg.findChild<QLineEdit*>();
    QVERIFY(passwordEdit != nullptr);
    QCOMPARE(passwordEdit->echoMode(), QLineEdit::Password);

    bool hasConfirm = false;
    bool hasCancel = false;
    const auto buttons = dlg.findChildren<QPushButton*>();
    for (QPushButton* btn : buttons) {
        if (btn->text() == QStringLiteral("Confirm")) {
            hasConfirm = true;
        }
        if (btn->text().contains(QStringLiteral("Cancel"), Qt::CaseInsensitive)) {
            hasCancel = true;
        }
    }

    QVERIFY(hasConfirm);
    QVERIFY(hasCancel);
}

void TstStepUpDialog::test_confirmEmptyPassword_showsValidationError()
{
    const QString token = createActiveSession(QStringLiteral("u-admin"),
                                              QStringLiteral("admin"),
                                              QStringLiteral("AdminPass123!"));
    QVERIFY(!token.isEmpty());

    StepUpDialog dlg(*m_authService, token, QStringLiteral("Approve correction"));
    dlg.show();
    QTest::qWait(5);

    QPushButton* confirmButton = nullptr;
    const auto buttons = dlg.findChildren<QPushButton*>();
    for (QPushButton* btn : buttons) {
        if (btn->text() == QStringLiteral("Confirm")) {
            confirmButton = btn;
            break;
        }
    }
    QVERIFY(confirmButton != nullptr);

    QTest::mouseClick(confirmButton, Qt::LeftButton);

    QLabel* errorLabel = nullptr;
    const auto labels = dlg.findChildren<QLabel*>();
    for (QLabel* label : labels) {
        if (label->text().contains(QStringLiteral("Password is required."))) {
            errorLabel = label;
            break;
        }
    }

    QVERIFY(errorLabel != nullptr);
    QVERIFY(errorLabel->isVisible());
    QVERIFY(dlg.stepUpWindowId().isEmpty());
    QVERIFY(dlg.result() != QDialog::Accepted);
}

void TstStepUpDialog::test_confirmValidPassword_acceptsAndStoresStepUpId()
{
    const QString token = createActiveSession(QStringLiteral("u-admin"),
                                              QStringLiteral("admin"),
                                              QStringLiteral("AdminPass123!"));
    QVERIFY(!token.isEmpty());

    StepUpDialog dlg(*m_authService, token, QStringLiteral("Approve correction"));
    dlg.show();
    QTest::qWait(5);

    QLineEdit* passwordEdit = dlg.findChild<QLineEdit*>();
    QVERIFY(passwordEdit != nullptr);
    passwordEdit->setText(QStringLiteral("AdminPass123!"));

    QPushButton* confirmButton = nullptr;
    const auto buttons = dlg.findChildren<QPushButton*>();
    for (QPushButton* btn : buttons) {
        if (btn->text() == QStringLiteral("Confirm")) {
            confirmButton = btn;
            break;
        }
    }
    QVERIFY(confirmButton != nullptr);

    QTest::mouseClick(confirmButton, Qt::LeftButton);

    QCOMPARE(dlg.result(), static_cast<int>(QDialog::Accepted));
    QVERIFY(!dlg.stepUpWindowId().isEmpty());
}

QTEST_MAIN(TstStepUpDialog)
#include "tst_step_up_dialog.moc"
