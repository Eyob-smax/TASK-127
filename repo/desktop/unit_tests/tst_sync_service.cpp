// tst_sync_service.cpp — ProctorOps
// Unit tests for SyncService: package export metadata, signing key import/revocation,
// conflict detection, and package status state machine.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDir>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include "repositories/SyncRepository.h"
#include "repositories/CheckInRepository.h"
#include "repositories/AuditRepository.h"
#include "repositories/UserRepository.h"
#include "services/AuditService.h"
#include "services/AuthService.h"
#include "services/PackageVerifier.h"
#include "services/SyncService.h"
#include "crypto/AesGcmCipher.h"
#include "crypto/Ed25519Signer.h"
#include "crypto/Ed25519Verifier.h"
#include "utils/Migration.h"

static void runMigrationsForSync(QSqlDatabase& db)
{
    const QString basePath = QStringLiteral(SOURCE_ROOT "/database/migrations");
    Migration runner(db, basePath);
    const auto result = runner.applyPending();
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

class TstSyncService : public QObject {
    Q_OBJECT

private:
    QSqlDatabase         m_db;
    SyncRepository*      m_syncRepo{nullptr};
    CheckInRepository*   m_checkInRepo{nullptr};
    AuditRepository*     m_auditRepo{nullptr};
    UserRepository*      m_userRepo{nullptr};
    AesGcmCipher*        m_cipher{nullptr};
    AuditService*        m_auditService{nullptr};
    AuthService*         m_authService{nullptr};
    PackageVerifier*     m_verifier{nullptr};
    SyncService*         m_syncService{nullptr};

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
                                          QStringLiteral("tst_sync_service_db"));
        m_db.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(m_db.open());
        runMigrationsForSync(m_db);

        // Need a bootstrap user for audit records
        QSqlQuery q(m_db);
        q.exec(QStringLiteral(
            "INSERT OR IGNORE INTO users (id, username, role, status, created_at, updated_at, created_by_user_id) "
            "VALUES ('u-sys', 'system', 'SECURITY_ADMINISTRATOR', 'Active', datetime('now'), datetime('now'), NULL)"));
        q.exec(QStringLiteral(
            "INSERT OR IGNORE INTO users (id, username, role, status, created_at, updated_at, created_by_user_id) "
            "VALUES ('u-op', 'operator', 'FRONT_DESK_OPERATOR', 'Active', datetime('now'), datetime('now'), NULL)"));

        const QByteArray masterKey(32, '\x42');
        m_cipher       = new AesGcmCipher(masterKey);
        m_syncRepo     = new SyncRepository(m_db);
        m_checkInRepo  = new CheckInRepository(m_db);
        m_auditRepo    = new AuditRepository(m_db);
        m_userRepo     = new UserRepository(m_db);
        m_auditService = new AuditService(*m_auditRepo, *m_cipher);
        m_authService  = new AuthService(*m_userRepo, *m_auditRepo);
        m_verifier     = new PackageVerifier(*m_syncRepo);
        m_syncService  = new SyncService(*m_syncRepo, *m_checkInRepo, *m_auditRepo,
                                          *m_authService, *m_auditService, *m_verifier);
    }

    void cleanupTestCase()
    {
        delete m_syncService;
        delete m_verifier;
        delete m_authService;
        delete m_auditService;
        delete m_userRepo;
        delete m_auditRepo;
        delete m_checkInRepo;
        delete m_syncRepo;
        delete m_cipher;
        m_db.close();
        QSqlDatabase::removeDatabase(QStringLiteral("tst_sync_service_db"));
    }

    // ── Signing key management tests ──────────────────────────────────────────

    void test_importSigningKey_valid()
    {
        // Generate a test key pair
        auto keyRes = Ed25519Signer::generateKeyPair();
        QVERIFY(keyRes.isOk());
        const QString pubKeyHex = QString::fromLatin1(keyRes.value().second.toHex());

        auto res = m_syncService->importSigningKey(
            QStringLiteral("Test Key"), pubKeyHex, QDateTime(),
            QStringLiteral("u-sys"), issueStepUpWindow(QStringLiteral("u-sys")));

        QVERIFY(res.isOk());
        QCOMPARE(res.value().label, QStringLiteral("Test Key"));
        QVERIFY(!res.value().fingerprint.isEmpty());
        QVERIFY(!res.value().revoked);
    }

    void test_importSigningKey_duplicate_fingerprint()
    {
        // Two imports of the same public key should fail (UNIQUE constraint on fingerprint)
        auto keyRes = Ed25519Signer::generateKeyPair();
        QVERIFY(keyRes.isOk());
        const QString pubKeyHex = QString::fromLatin1(keyRes.value().second.toHex());

        auto res1 = m_syncService->importSigningKey(
            QStringLiteral("Key A"), pubKeyHex, QDateTime(),
            QStringLiteral("u-sys"), issueStepUpWindow(QStringLiteral("u-sys")));
        QVERIFY(res1.isOk());

        auto res2 = m_syncService->importSigningKey(
            QStringLiteral("Key B"), pubKeyHex, QDateTime(),
            QStringLiteral("u-sys"), issueStepUpWindow(QStringLiteral("u-sys")));
        // Should fail due to UNIQUE fingerprint constraint
        QVERIFY(!res2.isOk());
    }

    void test_revokeSigningKey()
    {
        auto keyRes = Ed25519Signer::generateKeyPair();
        QVERIFY(keyRes.isOk());
        const QString pubKeyHex = QString::fromLatin1(keyRes.value().second.toHex());

        auto impRes = m_syncService->importSigningKey(
            QStringLiteral("Key to Revoke"), pubKeyHex, QDateTime(),
            QStringLiteral("u-sys"), issueStepUpWindow(QStringLiteral("u-sys")));
        QVERIFY(impRes.isOk());

        const QString keyId = impRes.value().id;
        auto revRes = m_syncService->revokeSigningKey(keyId,
                                                       QStringLiteral("u-sys"),
                                                       issueStepUpWindow(QStringLiteral("u-sys")));
        QVERIFY(revRes.isOk());

        // Verify key is revoked
        auto listRes = m_syncService->listSigningKeys(QStringLiteral("u-sys"));
        QVERIFY(listRes.isOk());
        bool found = false;
        for (const TrustedSigningKey& k : listRes.value()) {
            if (k.id == keyId) { found = true; QVERIFY(k.revoked); }
        }
        QVERIFY(found);
    }

    void test_listSigningKeys_returns_all()
    {
        auto res = m_syncService->listSigningKeys(QStringLiteral("u-sys"));
        QVERIFY(res.isOk());
        // Should have at least the keys created in prior tests
        QVERIFY(res.value().size() >= 1);
    }

    void test_listSigningKeys_requiresSecurityAdministrator()
    {
        auto res = m_syncService->listSigningKeys(QStringLiteral("u-op"));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::AuthorizationDenied);
    }

    // ── Package import with invalid signature ─────────────────────────────────

    void test_importPackage_missingManifest()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        // No manifest.json in the directory
        auto res = m_syncService->importPackage(tmp.path(), QStringLiteral("u-sys"));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::PackageCorrupt);
    }

    void test_importPackage_invalidJson()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QFile f(tmp.path() + QStringLiteral("/manifest.json"));
        f.open(QIODevice::WriteOnly);
        f.write("not json at all");
        f.close();

        auto res = m_syncService->importPackage(tmp.path(), QStringLiteral("u-sys"));
        QVERIFY(!res.isOk());
    }

    void test_importPackage_signatureInvalid()
    {
        // Package with wrong signature should be rejected
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Generate and register a signing key
        auto keyRes = Ed25519Signer::generateKeyPair();
        QVERIFY(keyRes.isOk());
        const QString pubKeyHex = QString::fromLatin1(keyRes.value().second.toHex());
        auto impRes = m_syncService->importSigningKey(
            QStringLiteral("Valid Key"), pubKeyHex, QDateTime(),
            QStringLiteral("u-sys"), issueStepUpWindow(QStringLiteral("u-sys")));
        QVERIFY(impRes.isOk());

        // Write manifest with garbage signature
        QJsonObject manifest;
        manifest[QStringLiteral("package_id")]      = QStringLiteral("pkg-bad");
        manifest[QStringLiteral("source_desk_id")]  = QStringLiteral("desk-1");
        manifest[QStringLiteral("signer_key_id")]   = impRes.value().id;
        manifest[QStringLiteral("exported_at")]     = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        manifest[QStringLiteral("since_watermark")] = QDateTime::fromMSecsSinceEpoch(0, Qt::UTC).toString(Qt::ISODateWithMs);
        manifest[QStringLiteral("entities")]        = QJsonObject{};
        manifest[QStringLiteral("signature")]       = QStringLiteral("cafebabe00000000");

        QFile mf(tmp.path() + QStringLiteral("/manifest.json"));
        mf.open(QIODevice::WriteOnly);
        mf.write(QJsonDocument(manifest).toJson());
        mf.close();

        auto res = m_syncService->importPackage(tmp.path(), QStringLiteral("u-sys"));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::SignatureInvalid);
    }

    // ── Conflict query ────────────────────────────────────────────────────────

    void test_listPendingConflicts_emptyForNewPackage()
    {
        // Insert a package directly and query for its conflicts
        SyncPackage pkg;
        pkg.id = QStringLiteral("pkg-no-conflicts");
        pkg.sourceDeskId = QStringLiteral("desk-A");
        pkg.signerKeyId  = QStringLiteral("k-test");
        pkg.exportedAt   = QDateTime::currentDateTimeUtc();
        pkg.sinceWatermark = QDateTime::fromMSecsSinceEpoch(0, Qt::UTC);
        pkg.status = SyncPackageStatus::Pending;
        pkg.packageFilePath = QStringLiteral("/tmp/pkg");
        m_syncRepo->insertPackage(pkg);

        auto res = m_syncService->listPendingConflicts(pkg.id, QStringLiteral("u-sys"));
        QVERIFY(res.isOk());
        QVERIFY(res.value().isEmpty());
    }
};

QTEST_MAIN(TstSyncService)
#include "tst_sync_service.moc"
