// tst_audit_viewer.cpp — ProctorOps
// Unit tests for AuditViewerWindow: construction, table population, filter panel,
// and detail pane rendering.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "windows/AuditViewerWindow.h"
#include "AppContextTestTypes.h"
#include "services/AuditService.h"
#include "repositories/AuditRepository.h"
#include "crypto/AesGcmCipher.h"
#include "models/Audit.h"
#include "models/CommonTypes.h"

#include <QTableView>
#include <QStandardItemModel>
#include <QGroupBox>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>

class TstAuditViewer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ── Construction ────────────────────────────────────────────────────────
    void test_windowCreates();
    void test_windowHasTableView();
    void test_windowHasDetailPane();
    void test_windowHasFilterButtons();

    // ── Window ID ───────────────────────────────────────────────────────────
    void test_windowIdConstant();

    // ── Filter panel ─────────────────────────────────────────────────────────
    void test_applyButtonExists();
    void test_exportButtonExists();
    void test_verifyChainButtonExists();

private:
    void applySchema();

    QSqlDatabase       m_db;
    AppContext*        m_ctx{nullptr};

    std::unique_ptr<AuditRepository>  m_auditRepo;
    std::unique_ptr<AuditService>     m_auditService;  // moved to m_ctx in initTestCase
};

void TstAuditViewer::initTestCase()
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("tst_audit_viewer"));
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(m_db.open());
    applySchema();
    m_ctx = new AppContext();

    static const QByteArray testKey(32, '\x55');
    m_ctx->cipher = std::make_unique<AesGcmCipher>(testKey);

    m_auditRepo    = std::make_unique<AuditRepository>(m_db);
    m_auditService = std::make_unique<AuditService>(*m_auditRepo, *m_ctx->cipher);

    // Insert a few test entries before transferring ownership
    m_auditService->recordEvent(QStringLiteral("user-1"), AuditEventType::Login,
                                QStringLiteral("User"), QStringLiteral("user-1"));
    m_auditService->recordEvent(QStringLiteral("operator-1"), AuditEventType::CheckInSuccess,
                                QStringLiteral("CheckIn"), QStringLiteral("attempt-1"));

    // Transfer ownership into AppContext
    m_ctx->auditService = std::move(m_auditService);
}

void TstAuditViewer::cleanupTestCase()
{
    m_ctx->auditService.reset();
    m_ctx->cipher.reset();
    m_ctx = nullptr;
    m_auditRepo.reset();
    m_db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("tst_audit_viewer"));
}

void TstAuditViewer::applySchema()
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS audit_entries ("
        "id TEXT PRIMARY KEY, timestamp TEXT, actor_user_id TEXT, "
        "event_type TEXT, entity_type TEXT, entity_id TEXT, "
        "before_payload_json TEXT, after_payload_json TEXT, "
        "previous_entry_hash TEXT, entry_hash TEXT)"));
}

// ── Tests ──────────────────────────────────────────────────────────────────────

void TstAuditViewer::test_windowCreates()
{
    AuditViewerWindow win(*m_ctx);
    QVERIFY(win.objectName() == QLatin1String(AuditViewerWindow::WindowId));
    QVERIFY(!win.windowTitle().isEmpty());
}

void TstAuditViewer::test_windowHasTableView()
{
    AuditViewerWindow win(*m_ctx);
    auto* table = win.findChild<QTableView*>();
    QVERIFY(table != nullptr);
    QVERIFY(table->model() != nullptr);
}

void TstAuditViewer::test_windowHasDetailPane()
{
    AuditViewerWindow win(*m_ctx);
    auto* detail = win.findChild<QTextEdit*>();
    QVERIFY(detail != nullptr);
    QVERIFY(detail->isReadOnly());
}

void TstAuditViewer::test_windowHasFilterButtons()
{
    AuditViewerWindow win(*m_ctx);
    const auto buttons = win.findChildren<QPushButton*>();
    QStringList titles;
    for (auto* btn : buttons) titles << btn->text();
    QVERIFY(titles.contains(QStringLiteral("Apply")));
}

void TstAuditViewer::test_windowIdConstant()
{
    QCOMPARE(QLatin1String(AuditViewerWindow::WindowId), QLatin1String("window.audit_viewer"));
}

void TstAuditViewer::test_applyButtonExists()
{
    AuditViewerWindow win(*m_ctx);
    bool found = false;
    for (auto* btn : win.findChildren<QPushButton*>()) {
        if (btn->text() == QStringLiteral("Apply")) { found = true; break; }
    }
    QVERIFY(found);
}

void TstAuditViewer::test_exportButtonExists()
{
    AuditViewerWindow win(*m_ctx);
    bool found = false;
    for (auto* btn : win.findChildren<QPushButton*>()) {
        if (btn->text().contains(QStringLiteral("Export"))) { found = true; break; }
    }
    QVERIFY(found);
}

void TstAuditViewer::test_verifyChainButtonExists()
{
    AuditViewerWindow win(*m_ctx);
    bool found = false;
    for (auto* btn : win.findChildren<QPushButton*>()) {
        if (btn->text().contains(QStringLiteral("Verify"))) { found = true; break; }
    }
    QVERIFY(found);
}

QTEST_MAIN(TstAuditViewer)
#include "tst_audit_viewer.moc"
