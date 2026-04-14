// tst_export_flow.cpp — ProctorOps
// Integration tests for the GDPR / MLPS data subject export flow:
//   Request creation → fulfillment authorization → export file generation
//   → audit chain verification → redaction controls.
// Compliance control boundaries: only SecurityAdministrator may fulfill/reject.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QUuid>

#include "repositories/AuditRepository.h"
#include "repositories/MemberRepository.h"
#include "repositories/UserRepository.h"
#include "services/AuditService.h"
#include "services/AuthService.h"
#include "services/DataSubjectService.h"
#include "crypto/AesGcmCipher.h"
#include "utils/Migration.h"
#include "models/Audit.h"

static void runMigrations(QSqlDatabase& db)
{
    Migration runner(db, QStringLiteral(SOURCE_ROOT "/database/migrations"));
    const auto result = runner.applyPending();
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

class TstExportFlow : public QObject {
    Q_OBJECT

private:
    QSqlDatabase        m_db;
    AuditRepository*    m_auditRepo{nullptr};
    MemberRepository*   m_memberRepo{nullptr};
    UserRepository*     m_userRepo{nullptr};
    AesGcmCipher*       m_cipher{nullptr};
    AuditService*       m_auditService{nullptr};
    AuthService*        m_authService{nullptr};
    DataSubjectService* m_service{nullptr};

    QString m_memberId;
    QString m_encName;

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
        if (!q.exec()) {
            qWarning() << "issueStepUpWindow user_sessions insert failed:" << q.lastError().text();
            return QString{};
        }

        q.prepare(QStringLiteral("INSERT INTO step_up_windows (id, user_id, session_token, granted_at, expires_at, consumed) "
                                 "VALUES (?, ?, ?, ?, ?, 0)"));
        q.addBindValue(stepUpId);
        q.addBindValue(userId);
        q.addBindValue(sessionToken);
        q.addBindValue(now);
        q.addBindValue(expiresAt);
        if (!q.exec()) {
            qWarning() << "issueStepUpWindow step_up_windows insert failed:" << q.lastError().text();
            return QString{};
        }

        return stepUpId;
    }

private slots:
    void initTestCase()
    {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                          QStringLiteral("tst_export_flow_db"));
        m_db.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(m_db.open());
        runMigrations(m_db);

        QSqlQuery q(m_db);
        q.exec(QStringLiteral(
            "INSERT OR IGNORE INTO users (id, username, role, status, created_at, updated_at, created_by_user_id) "
            "VALUES ('u-admin', 'admin', 'SECURITY_ADMINISTRATOR', 'Active', datetime('now'), datetime('now'), NULL)"));
        q.exec(QStringLiteral(
            "INSERT OR IGNORE INTO users (id, username, role, status, created_at, updated_at, created_by_user_id) "
            "VALUES ('u-operator', 'operator1', 'FRONT_DESK_OPERATOR', 'Active', datetime('now'), datetime('now'), NULL)"));

        const QByteArray masterKey(32, '\x45');
        m_cipher       = new AesGcmCipher(masterKey);
        m_auditRepo    = new AuditRepository(m_db);
        m_memberRepo   = new MemberRepository(m_db);
        m_userRepo     = new UserRepository(m_db);
        m_auditService = new AuditService(*m_auditRepo, *m_cipher);
        m_authService  = new AuthService(*m_userRepo, *m_auditRepo);
        m_service      = new DataSubjectService(*m_auditRepo, *m_memberRepo,
                                                *m_authService, *m_auditService, *m_cipher);

        // Insert test member with encrypted PII
        m_memberId = QStringLiteral("m-export-test");
        auto encNameRes = m_cipher->encrypt(QStringLiteral("Alice Export"));
        QVERIFY(encNameRes.isOk());
        m_encName = encNameRes.value();

        auto encMobileRes = m_cipher->encrypt(QStringLiteral("(555) 123-4567"));
        QVERIFY(encMobileRes.isOk());

        q.prepare(QStringLiteral(
            "INSERT INTO members (id, member_id, name_encrypted, mobile_encrypted, "
            "barcode_encrypted, deleted, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, '', 0, datetime('now'), datetime('now'))"));
        q.addBindValue(m_memberId);
        q.addBindValue(QStringLiteral("MBR-EXP-001"));
        q.addBindValue(m_encName);
        q.addBindValue(encMobileRes.value());
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
        QSqlDatabase::removeDatabase(QStringLiteral("tst_export_flow_db"));
    }

    // ── Test: full export flow ────────────────────────────────────────────────

    void test_fullExportFlow()
    {
        // Step 1: Any operator creates the request
        auto reqRes = m_service->createExportRequest(m_memberId,
                                                      QStringLiteral("Subject access request"),
                                                      QStringLiteral("u-operator"));
        QVERIFY(reqRes.isOk());
        QCOMPARE(reqRes.value().status, QStringLiteral("PENDING"));

        // Verify audit event was recorded
        AuditFilter filter;
        filter.entityId = reqRes.value().id;
        auto auditRes = m_auditRepo->queryEntries(filter);
        QVERIFY(auditRes.isOk());
        QVERIFY(!auditRes.value().isEmpty());

        // Step 2: Admin fulfills the request
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString outPath = tmp.path() + QStringLiteral("/subject_data.json");

        auto fulfillRes = m_service->fulfillExportRequest(reqRes.value().id, outPath,
                                                           QStringLiteral("u-admin"),
                                                           issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY2(fulfillRes.isOk(), fulfillRes.isErr() ? qPrintable(fulfillRes.errorMessage()) : "");
        QCOMPARE(fulfillRes.value().status, QStringLiteral("COMPLETED"));

        // Step 3: Verify export file structure
        QFile f(outPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        QVERIFY(!doc.isNull());

        QJsonObject root = doc.object();
        QCOMPARE(root[QStringLiteral("export_type")].toString(),
                  QStringLiteral("GDPR_SUBJECT_ACCESS"));
        QVERIFY(root.contains(QStringLiteral("WATERMARK")));
        QVERIFY(root[QStringLiteral("WATERMARK")].toString().contains(
                    QStringLiteral("AUTHORIZED_EXPORT_ONLY")));
        QVERIFY(root.contains(QStringLiteral("member")));

        // Step 4: Verify completion audit event
        filter.entityId = reqRes.value().id;
        auto auditRes2 = m_auditRepo->queryEntries(filter);
        QVERIFY(auditRes2.isOk());
        QVERIFY(auditRes2.value().size() >= 2); // created + completed
    }

    // ── Test: redaction controls ──────────────────────────────────────────────

    void test_exportFileMasksRawMobile()
    {
        auto reqRes = m_service->createExportRequest(m_memberId,
                                                      QStringLiteral("Mobile masking test"),
                                                      QStringLiteral("u-admin"));
        QVERIFY(reqRes.isOk());

        QTemporaryDir tmp;
        const QString outPath = tmp.path() + QStringLiteral("/masked_export.json");

        auto fulfillRes = m_service->fulfillExportRequest(reqRes.value().id, outPath,
                                                           QStringLiteral("u-admin"),
                                                           issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY(fulfillRes.isOk());

        QFile f(outPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(f.readAll());

        // Full raw mobile should not appear in the export
        QVERIFY(!content.contains(QStringLiteral("(555) 123-4567")));
    }

    // ── Test: retention — completed requests remain queryable ─────────────────

    void test_completedRequestRetained()
    {
        // Previously completed requests remain in the list (compliance evidence)
        auto listRes = m_service->listExportRequests(QStringLiteral("u-admin"),
                                 QStringLiteral("COMPLETED"));
        QVERIFY(listRes.isOk());
        QVERIFY(!listRes.value().isEmpty());
    }

    // ── Test: idempotency boundary — cannot re-fulfill ────────────────────────

    void test_cannotFulfillCompletedRequest()
    {
        auto listRes = m_service->listExportRequests(QStringLiteral("u-admin"),
                                 QStringLiteral("COMPLETED"));
        QVERIFY(listRes.isOk());
        QVERIFY(!listRes.value().isEmpty());

        QTemporaryDir tmp;
        const QString outPath = tmp.path() + QStringLiteral("/duplicate.json");
        const QString requestId = listRes.value().first().id;

        auto fulfillRes = m_service->fulfillExportRequest(requestId, outPath,
                                                           QStringLiteral("u-admin"),
                                                           issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY(!fulfillRes.isOk());
        QCOMPARE(fulfillRes.errorCode(), ErrorCode::InvalidState);
    }

    // ── Test: deletion request full flow ──────────────────────────────────────

    void test_deletionFlow_auditTrailRetained()
    {
        const QString delMemberId = QStringLiteral("m-to-erase");
        auto encNameRes = m_cipher->encrypt(QStringLiteral("Bob Erased"));
        QVERIFY(encNameRes.isOk());

        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "INSERT INTO members (id, member_id, name_encrypted, mobile_encrypted, "
            "barcode_encrypted, deleted, created_at, updated_at) "
            "VALUES (?, ?, ?, '', '', 0, datetime('now'), datetime('now'))"));
        q.addBindValue(delMemberId);
        q.addBindValue(QStringLiteral("MBR-ERASE"));
        q.addBindValue(encNameRes.value());
        QVERIFY(q.exec());

        // Create → approve → complete
        auto createRes = m_service->createDeletionRequest(delMemberId,
                                                           QStringLiteral("GDPR erasure"),
                                                           QStringLiteral("u-operator"));
        QVERIFY(createRes.isOk());

        auto approveRes = m_service->approveDeletionRequest(createRes.value().id,
                                                             QStringLiteral("u-admin"),
                                                             issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY(approveRes.isOk());

        auto completeRes = m_service->completeDeletion(createRes.value().id,
                                                        QStringLiteral("u-admin"),
                                                        issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY2(completeRes.isOk(), completeRes.isErr() ? qPrintable(completeRes.errorMessage()) : "");

        // Audit entries for this member should still exist (tombstone retained)
        AuditFilter filter;
        filter.entityId = createRes.value().id;
        auto auditRes = m_auditRepo->queryEntries(filter);
        QVERIFY(auditRes.isOk());
        QVERIFY(auditRes.value().size() >= 3); // created, approved, completed

        // Deletion request should be COMPLETED in list
        auto listRes = m_service->listDeletionRequests(QStringLiteral("u-admin"),
                                   QStringLiteral("COMPLETED"));
        QVERIFY(listRes.isOk());
        bool found = false;
        for (const DeletionRequest& r : listRes.value())
            if (r.id == createRes.value().id) found = true;
        QVERIFY(found);
    }
};

QTEST_MAIN(TstExportFlow)
#include "tst_export_flow.moc"
