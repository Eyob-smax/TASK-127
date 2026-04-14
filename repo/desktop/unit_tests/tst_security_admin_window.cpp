// tst_security_admin_window.cpp — ProctorOps
// UI structure tests for SecurityAdminWindow:
// Verifies tab structure, button presence, and widget accessibility.
// Does not test privileged operations end-to-end (covered by integration tests).

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTabWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QApplication>

#include "windows/SecurityAdminWindow.h"
#include "AppContextTestTypes.h"
#include "repositories/AuditRepository.h"
#include "repositories/UserRepository.h"
#include "repositories/MemberRepository.h"
#include "services/AuditService.h"
#include "services/AuthService.h"
#include "crypto/AesGcmCipher.h"
#include "utils/Migration.h"

static void runMigrations(QSqlDatabase& db)
{
    Migration runner(db, QStringLiteral(SOURCE_ROOT "/database/migrations"));
    const auto result = runner.applyPending();
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

class TstSecurityAdminWindow : public QObject {
    Q_OBJECT

private:
    QSqlDatabase    m_db;
    AppContext*     m_ctx{nullptr};
    SecurityAdminWindow* m_window{nullptr};

private slots:
    void initTestCase()
    {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                          QStringLiteral("tst_sec_admin_db"));
        m_db.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(m_db.open());
        runMigrations(m_db);
        m_ctx = new AppContext();

        QSqlQuery q(m_db);
        q.exec(QStringLiteral(
            "INSERT OR IGNORE INTO users (id, username, role, status, created_at, updated_at, created_by_user_id) "
            "VALUES ('u-admin', 'admin', 'SECURITY_ADMINISTRATOR', 'Active', datetime('now'), datetime('now'), NULL)"));

        const QByteArray masterKey(32, '\x44');
        m_ctx->cipher       = std::make_unique<AesGcmCipher>(masterKey);
        m_ctx->auditRepo    = std::make_unique<AuditRepository>(m_db);
        m_ctx->userRepo     = std::make_unique<UserRepository>(m_db);
        m_ctx->memberRepo   = std::make_unique<MemberRepository>(m_db);
        m_ctx->auditService = std::make_unique<AuditService>(*m_ctx->auditRepo, *m_ctx->cipher);
        m_ctx->authService  = std::make_unique<AuthService>(*m_ctx->userRepo, *m_ctx->auditRepo);
        m_ctx->session.userId = QStringLiteral("u-admin");
        m_ctx->session.token  = QStringLiteral("tok-admin");
    }

    void init()
    {
        m_window = new SecurityAdminWindow(*m_ctx);
        m_window->setWindowFlags(Qt::Widget);
        m_window->show();
        QTest::qWait(10);
    }

    void cleanup()
    {
        delete m_window;
        m_window = nullptr;
    }

    void cleanupTestCase()
    {
        m_ctx->authService.reset();
        m_ctx->auditService.reset();
        m_ctx->memberRepo.reset();
        m_ctx->userRepo.reset();
        m_ctx->auditRepo.reset();
        m_ctx->cipher.reset();
        m_ctx = nullptr;
        m_db.close();
        QSqlDatabase::removeDatabase(QStringLiteral("tst_sec_admin_db"));
    }

    void test_windowTitle()
    {
        QCOMPARE(m_window->windowTitle(), tr("Security Administration"));
    }

    void test_windowIdConstant()
    {
        QVERIFY(qstrlen(SecurityAdminWindow::WindowId) > 0);
        QCOMPARE(QLatin1String(SecurityAdminWindow::WindowId),
                 QLatin1String("window.security_admin"));
    }

    void test_tabsPresent()
    {
        auto* tabs = m_window->findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);
        QCOMPARE(tabs->count(), 3);
        QCOMPARE(tabs->tabText(0), tr("User Roles"));
        QCOMPARE(tabs->tabText(1), tr("Account Freezes"));
        QCOMPARE(tabs->tabText(2), tr("Privileged Audit"));
    }

    void test_usersTablePresent()
    {
        const auto tables = m_window->findChildren<QTableWidget*>();
        bool usersTableFound = false;
        for (auto* t : tables) {
            if (t->columnCount() == 4) { usersTableFound = true; break; }
        }
        QVERIFY(usersTableFound);
    }

    void test_privilegedButtonsInitiallyDisabled()
    {
        // Row-targeted actions should remain disabled until a row is selected.
        const auto btns = m_window->findChildren<QPushButton*>();
        for (auto* btn : btns) {
            if (btn->text().contains(tr("Reset Password")))
                QVERIFY(!btn->isEnabled());
            if (btn->text().contains(tr("Change Role")))
                QVERIFY(!btn->isEnabled());
            if (btn->text().contains(tr("Unlock")))
                QVERIFY(!btn->isEnabled());
        }
    }

    void test_freezeInputsPresent()
    {
        const auto lineEdits = m_window->findChildren<QLineEdit*>();
        QVERIFY(lineEdits.size() >= 2); // Member ID + Reason
    }

    void test_auditTablePresent()
    {
        const auto tables = m_window->findChildren<QTableWidget*>();
        bool auditTableFound = false;
        for (auto* t : tables) {
            if (t->columnCount() == 5) { auditTableFound = true; break; }
        }
        QVERIFY(auditTableFound);
    }
};

#include <QLineEdit>
QTEST_MAIN(TstSecurityAdminWindow)
#include "tst_security_admin_window.moc"
