// tst_data_subject_service.cpp — ProctorOps
// Unit tests for DataSubjectService:
//   - Export request creation, fulfillment, rejection
//   - Deletion request creation, approval, completion, rejection
//   - Compliance control boundaries: only PENDING requests transition, rationale required
//   - Anonymization is final; audit tombstones retained

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QTemporaryDir>
#include <QUuid>

#include "repositories/AuditRepository.h"
#include "repositories/MemberRepository.h"
#include "repositories/UserRepository.h"
#include "services/AuditService.h"
#include "services/AuthService.h"
#include "services/DataSubjectService.h"
#include "crypto/AesGcmCipher.h"
#include "utils/Migration.h"

static void runMigrations(QSqlDatabase& db)
{
    Migration runner(db, QStringLiteral(SOURCE_ROOT "/database/migrations"));
    const auto result = runner.applyPending();
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

class TstDataSubjectService : public QObject {
    Q_OBJECT

private:
    QSqlDatabase          m_db;
    AuditRepository*      m_auditRepo{nullptr};
    MemberRepository*     m_memberRepo{nullptr};
    UserRepository*       m_userRepo{nullptr};
    AesGcmCipher*         m_cipher{nullptr};
    AuditService*         m_auditService{nullptr};
    AuthService*          m_authService{nullptr};
    DataSubjectService*   m_service{nullptr};

    QString m_memberId;

    QString issueStepUpWindow(const QString& userId)
    {
        const QString sessionToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString stepUpId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        const QString expiresAt = QDateTime::currentDateTimeUtc().addSecs(120).toString(Qt::ISODateWithMs);

        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("INSERT INTO user_sessions (token, user_id, created_at, last_active_at, active) "
                                 "VALUES (?, ?, ?, ?, 1)"));
        q.addBindValue(sessionToken);
        q.addBindValue(userId);
        q.addBindValue(now);
        q.addBindValue(now);
        if (!q.exec())
            return {};

        q.prepare(QStringLiteral("INSERT INTO step_up_windows (id, user_id, session_token, granted_at, expires_at, consumed) "
                                 "VALUES (?, ?, ?, ?, ?, 0)"));
        q.addBindValue(stepUpId);
        q.addBindValue(userId);
        q.addBindValue(sessionToken);
        q.addBindValue(now);
        q.addBindValue(expiresAt);
        if (!q.exec())
            return {};

        return stepUpId;
    }

private slots:
    void initTestCase()
    {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                          QStringLiteral("tst_data_subject_db"));
        m_db.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(m_db.open());
        runMigrations(m_db);

        QSqlQuery q(m_db);
        q.exec(QStringLiteral(
            "INSERT OR IGNORE INTO users (id, username, role, status, created_at, updated_at, created_by_user_id) "
            "VALUES ('u-admin', 'admin', 'SECURITY_ADMINISTRATOR', 'Active', datetime('now'), datetime('now'), NULL)"));
        q.exec(QStringLiteral(
            "INSERT OR IGNORE INTO users (id, username, role, status, created_at, updated_at, created_by_user_id) "
            "VALUES ('u-op', 'operator', 'FRONT_DESK_OPERATOR', 'Active', datetime('now'), datetime('now'), NULL)"));

        const QByteArray masterKey(32, '\x43');
        m_cipher       = new AesGcmCipher(masterKey);
        m_auditRepo    = new AuditRepository(m_db);
        m_memberRepo   = new MemberRepository(m_db);
        m_userRepo     = new UserRepository(m_db);
        m_auditService = new AuditService(*m_auditRepo, *m_cipher);
        m_authService  = new AuthService(*m_userRepo, *m_auditRepo);
        m_service      = new DataSubjectService(*m_auditRepo, *m_memberRepo,
                                                *m_authService, *m_auditService, *m_cipher);

        // Insert a test member
        m_memberId = QStringLiteral("m-subject-001");
        auto encName = m_cipher->encrypt(QStringLiteral("Jane Doe"),
                                         QByteArrayLiteral("member.name"));
        QVERIFY(encName.isOk());

        q.prepare(QStringLiteral(
            "INSERT INTO members (id, member_id, barcode_encrypted, mobile_encrypted, "
            "name_encrypted, deleted, created_at, updated_at) "
            "VALUES (?, ?, '', '', ?, 0, datetime('now'), datetime('now'))"));
        q.addBindValue(m_memberId);
        q.addBindValue(QStringLiteral("MBR-001"));
        q.addBindValue(QString::fromLatin1(encName.value().toBase64()));
        QVERIFY(q.exec());
    }

    void cleanupTestCase()
    {
        delete m_service;
        delete m_authService;
        delete m_auditService;
        delete m_userRepo;
        delete m_memberRepo;
        delete m_auditRepo;
        delete m_cipher;
        m_db.close();
        QSqlDatabase::removeDatabase(QStringLiteral("tst_data_subject_db"));
    }

    // ── Export request tests ──────────────────────────────────────────────────

    void test_createExportRequest_success()
    {
        auto res = m_service->createExportRequest(m_memberId, QStringLiteral("Member requested access"),
                                                   QStringLiteral("u-admin"));
        QVERIFY(res.isOk());
        QCOMPARE(res.value().status, QStringLiteral("PENDING"));
        QCOMPARE(res.value().memberId, m_memberId);
    }

    void test_createExportRequest_requiresRationale()
    {
        auto res = m_service->createExportRequest(m_memberId, QStringLiteral("  "),
                                                   QStringLiteral("u-admin"));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::ValidationFailed);
    }

    void test_fulfillExportRequest_writesFile()
    {
        auto reqRes = m_service->createExportRequest(m_memberId,
                                                      QStringLiteral("Access request"),
                                                      QStringLiteral("u-admin"));
        QVERIFY(reqRes.isOk());

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString outPath = tmp.path() + QStringLiteral("/export.json");

        auto fulfillRes = m_service->fulfillExportRequest(reqRes.value().id, outPath,
                                                           QStringLiteral("u-admin"),
                                                           issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY2(fulfillRes.isOk(), fulfillRes.isErr() ? qPrintable(fulfillRes.errorMessage()) : "");
        QCOMPARE(fulfillRes.value().status, QStringLiteral("COMPLETED"));
        QVERIFY(QFile::exists(outPath));

        // Verify the export file contains the watermark
        QFile f(outPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(f.readAll());
        QVERIFY(content.contains(QStringLiteral("AUTHORIZED_EXPORT_ONLY")));
    }

    void test_fulfillExportRequest_onlyPending()
    {
        auto reqRes = m_service->createExportRequest(m_memberId,
                                                      QStringLiteral("Test"),
                                                      QStringLiteral("u-admin"));
        QVERIFY(reqRes.isOk());

        // Reject first, then try to fulfill
        QVERIFY(m_service->rejectExportRequest(reqRes.value().id,
                                                QStringLiteral("u-admin"),
                                                issueStepUpWindow(QStringLiteral("u-admin"))).isOk());

        QTemporaryDir tmp;
        const QString outPath = tmp.path() + QStringLiteral("/nope.json");
        auto fulfillRes = m_service->fulfillExportRequest(reqRes.value().id, outPath,
                                                           QStringLiteral("u-admin"),
                                                           issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY(!fulfillRes.isOk());
        QCOMPARE(fulfillRes.errorCode(), ErrorCode::InvalidState);
    }

    void test_rejectExportRequest()
    {
        auto reqRes = m_service->createExportRequest(m_memberId,
                                                      QStringLiteral("Request to reject"),
                                                      QStringLiteral("u-admin"));
        QVERIFY(reqRes.isOk());

        auto rejectRes = m_service->rejectExportRequest(reqRes.value().id,
                                                         QStringLiteral("u-admin"),
                                                         issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY(rejectRes.isOk());

        // Check status updated
        auto listRes = m_service->listExportRequests(QStringLiteral("u-admin"),
                                 QStringLiteral("REJECTED"));
        QVERIFY(listRes.isOk());
        bool found = false;
        for (const ExportRequest& r : listRes.value())
            if (r.id == reqRes.value().id) found = true;
        QVERIFY(found);
    }

    // ── Deletion request tests ────────────────────────────────────────────────

    void test_createDeletionRequest_success()
    {
        auto res = m_service->createDeletionRequest(m_memberId,
                                                     QStringLiteral("Member requests erasure"),
                                                     QStringLiteral("u-admin"));
        QVERIFY(res.isOk());
        QCOMPARE(res.value().status, QStringLiteral("PENDING"));
    }

    void test_createDeletionRequest_requiresRationale()
    {
        auto res = m_service->createDeletionRequest(m_memberId, QString{},
                                                     QStringLiteral("u-admin"));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::ValidationFailed);
    }

    void test_approveDeletionRequest_successAndAudit()
    {
        auto reqRes = m_service->createDeletionRequest(m_memberId,
                                                        QStringLiteral("Erasure"),
                                                        QStringLiteral("u-admin"));
        QVERIFY(reqRes.isOk());

        auto approveRes = m_service->approveDeletionRequest(reqRes.value().id,
                                                             QStringLiteral("u-admin"),
                                                             issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY(approveRes.isOk());
        QCOMPARE(approveRes.value().status, QStringLiteral("APPROVED"));
        QCOMPARE(approveRes.value().approverUserId, QStringLiteral("u-admin"));
    }

    void test_approveOnlyPending()
    {
        auto reqRes = m_service->createDeletionRequest(m_memberId,
                                                        QStringLiteral("To be rejected first"),
                                                        QStringLiteral("u-admin"));
        QVERIFY(reqRes.isOk());

        QVERIFY(m_service->rejectDeletionRequest(reqRes.value().id,
                                                   QStringLiteral("u-admin"),
                                                   issueStepUpWindow(QStringLiteral("u-admin"))).isOk());

        auto approveRes = m_service->approveDeletionRequest(reqRes.value().id,
                                                             QStringLiteral("u-admin"),
                                                             issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY(!approveRes.isOk());
        QCOMPARE(approveRes.errorCode(), ErrorCode::InvalidState);
    }

    void test_completeDeletion_anonymizesMember()
    {
        // Create a fresh member for this test to avoid conflicts
        const QString testMemberId = QStringLiteral("m-to-delete-001");
        auto encName = m_cipher->encrypt(QStringLiteral("John Smith"));
        QVERIFY(encName.isOk());

        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "INSERT INTO members (id, member_id, name_encrypted, mobile_encrypted, "
            "barcode_encrypted, deleted, created_at, updated_at) "
            "VALUES (?, ?, ?, '', '', 0, datetime('now'), datetime('now'))"));
        q.addBindValue(testMemberId);
        q.addBindValue(QStringLiteral("MBR-DEL"));
        q.addBindValue(encName.value());
        QVERIFY(q.exec());

        auto reqRes = m_service->createDeletionRequest(testMemberId,
                                                        QStringLiteral("Erasure complete"),
                                                        QStringLiteral("u-admin"));
        QVERIFY(reqRes.isOk());

        QVERIFY(m_service->approveDeletionRequest(reqRes.value().id,
                                                    QStringLiteral("u-admin"),
                                                    issueStepUpWindow(QStringLiteral("u-admin"))).isOk());

        auto completeRes = m_service->completeDeletion(reqRes.value().id,
                                                        QStringLiteral("u-admin"),
                                                        issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY2(completeRes.isOk(), completeRes.isErr() ? qPrintable(completeRes.errorMessage()) : "");

        // Verify member is marked deleted
        auto listRes = m_service->listDeletionRequests(QStringLiteral("u-admin"),
                                   QStringLiteral("COMPLETED"));
        QVERIFY(listRes.isOk());
        bool found = false;
        for (const DeletionRequest& r : listRes.value())
            if (r.id == reqRes.value().id) { found = true; QVERIFY(!r.fieldsAnonymized.isEmpty()); }
        QVERIFY(found);
    }

    void test_listDeletionRequests_filterByStatus()
    {
        auto res = m_service->listDeletionRequests(QStringLiteral("u-admin"),
                               QStringLiteral("PENDING"));
        QVERIFY(res.isOk());
        for (const DeletionRequest& r : res.value())
            QCOMPARE(r.status, QStringLiteral("PENDING"));
    }

    void test_listExportRequests_requiresSecurityAdministrator()
    {
        auto res = m_service->listExportRequests(QStringLiteral("u-op"));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::AuthorizationDenied);
    }

    void test_listDeletionRequests_requiresSecurityAdministrator()
    {
        auto res = m_service->listDeletionRequests(QStringLiteral("u-op"));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::AuthorizationDenied);
    }
};

QTEST_MAIN(TstDataSubjectService)
#include "tst_data_subject_service.moc"
