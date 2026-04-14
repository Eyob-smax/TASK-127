// tst_update_service.cpp — ProctorOps
// Unit tests for UpdateService: package staging, manifest validation,
// install history recordkeeping, rollback authorization, and status transitions.
//
// Override: delivery does not require a signed .msi artifact.
// Update domain logic is fully tested here. See docs/design.md §2.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>

#include "repositories/SyncRepository.h"
#include "repositories/UpdateRepository.h"
#include "repositories/AuditRepository.h"
#include "repositories/UserRepository.h"
#include "services/AuditService.h"
#include "services/AuthService.h"
#include "services/PackageVerifier.h"
#include "services/UpdateService.h"
#include "crypto/AesGcmCipher.h"
#include "crypto/Ed25519Signer.h"
#include "utils/Migration.h"

static void runMigrations(QSqlDatabase& db)
{
    Migration runner(db, QStringLiteral(SOURCE_ROOT "/database/migrations"));
    const auto result = runner.applyPending();
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

// Helper: compute SHA-256 hex of bytes
static QString sha256Hex(const QByteArray& data)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

class TstUpdateService : public QObject {
    Q_OBJECT

private:
    QSqlDatabase      m_db;
    SyncRepository*   m_syncRepo{nullptr};
    UpdateRepository* m_updateRepo{nullptr};
    AuditRepository*  m_auditRepo{nullptr};
    UserRepository*   m_userRepo{nullptr};
    AesGcmCipher*     m_cipher{nullptr};
    AuditService*     m_auditService{nullptr};
    AuthService*      m_authService{nullptr};
    PackageVerifier*  m_verifier{nullptr};
    UpdateService*    m_updateService{nullptr};

    // Test signing key pair
    QByteArray m_privKeyDer;
    QByteArray m_pubKeyDer;
    QString    m_signerKeyId;

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
                                          QStringLiteral("tst_update_service_db"));
        m_db.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(m_db.open());
        runMigrations(m_db);

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
        m_updateRepo   = new UpdateRepository(m_db);
        m_auditRepo    = new AuditRepository(m_db);
        m_userRepo     = new UserRepository(m_db);
        m_auditService = new AuditService(*m_auditRepo, *m_cipher);
        m_authService  = new AuthService(*m_userRepo, *m_auditRepo);
        m_verifier     = new PackageVerifier(*m_syncRepo);
        m_updateService = new UpdateService(*m_updateRepo, *m_syncRepo,
                                             *m_authService, *m_verifier, *m_auditService);

        // Generate and register a signing key for test packages
        auto keyRes = Ed25519Signer::generateKeyPair();
        QVERIFY(keyRes.isOk());
        m_privKeyDer = keyRes.value().first;
        m_pubKeyDer  = keyRes.value().second;

        TrustedSigningKey key;
        key.id               = QStringLiteral("k-test-update");
        key.label            = QStringLiteral("Test Update Key");
        key.publicKeyDerHex  = QString::fromLatin1(m_pubKeyDer.toHex());
        key.fingerprint      = sha256Hex(m_pubKeyDer);
        key.importedAt       = QDateTime::currentDateTimeUtc();
        key.importedByUserId = QStringLiteral("u-sys");
        key.revoked          = false;
        m_syncRepo->insertSigningKey(key);
        m_signerKeyId = key.id;
    }

    void cleanupTestCase()
    {
        delete m_updateService;
        delete m_verifier;
        delete m_authService;
        delete m_auditService;
        delete m_userRepo;
        delete m_auditRepo;
        delete m_updateRepo;
        delete m_syncRepo;
        delete m_cipher;
        m_db.close();
        QSqlDatabase::removeDatabase(QStringLiteral("tst_update_service_db"));
    }

    // Helper: write a valid signed package directory
    QString writeValidPackage(const QString& baseDir, const QString& version)
    {
        const QString pkgDir = baseDir + QStringLiteral("/pkg-") + version;
        QDir().mkpath(pkgDir);

        // Write a dummy component file
        const QByteArray componentContent = QStringLiteral("dummy exe content").toUtf8();
        const QString componentPath = pkgDir + QStringLiteral("/proctorops.exe");
        QFile cf(componentPath);
        cf.open(QIODevice::WriteOnly);
        cf.write(componentContent);
        cf.close();

        // Build manifest body (without signature)
        QJsonObject manifestBody;
        manifestBody[QStringLiteral("package_id")]      = QStringLiteral("pkg-") + version;
        manifestBody[QStringLiteral("version")]         = version;
        manifestBody[QStringLiteral("target_platform")] = QStringLiteral("windows-x86_64");
        manifestBody[QStringLiteral("description")]     = QStringLiteral("Test update");
        manifestBody[QStringLiteral("signer_key_id")]   = m_signerKeyId;

        QJsonArray components;
        QJsonObject comp;
        comp[QStringLiteral("name")]    = QStringLiteral("proctorops.exe");
        comp[QStringLiteral("version")] = version;
        comp[QStringLiteral("sha256")]  = sha256Hex(componentContent);
        comp[QStringLiteral("file")]    = QStringLiteral("proctorops.exe");
        components.append(comp);
        manifestBody[QStringLiteral("components")] = components;

        const QByteArray manifestBodyBytes =
            QJsonDocument(manifestBody).toJson(QJsonDocument::Compact);

        // Sign manifest body
        auto signRes = Ed25519Signer::sign(manifestBodyBytes, m_privKeyDer);
        if (!signRes.isOk()) return {};

        QJsonObject manifest = manifestBody;
        manifest[QStringLiteral("signature")] = QString::fromLatin1(signRes.value().toHex());

        QFile mf(pkgDir + QStringLiteral("/update-manifest.json"));
        mf.open(QIODevice::WriteOnly);
        mf.write(QJsonDocument(manifest).toJson());
        mf.close();

        return pkgDir;
    }

    // ── Import tests ──────────────────────────────────────────────────────────

    void test_importPackage_validSignedPackage()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = writeValidPackage(tmp.path(), QStringLiteral("1.0.0"));
        QVERIFY(!pkgDir.isEmpty());

        auto res = m_updateService->importPackage(pkgDir, QStringLiteral("u-sys"));
        QVERIFY2(res.isOk(), res.isErr() ? qPrintable(res.errorMessage()) : "");
        QCOMPARE(res.value().version, QStringLiteral("1.0.0"));
        QVERIFY(res.value().signatureValid);
        QCOMPARE(res.value().status, UpdatePackageStatus::Staged);
    }

    void test_importPackage_missingManifest()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        auto res = m_updateService->importPackage(tmp.path(), QStringLiteral("u-sys"));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::PackageCorrupt);
    }

    void test_importPackage_invalidSignature()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = tmp.path() + QStringLiteral("/bad-sig");
        QDir().mkpath(pkgDir);

        QJsonObject manifest;
        manifest[QStringLiteral("package_id")]      = QStringLiteral("bad-sig-pkg");
        manifest[QStringLiteral("version")]         = QStringLiteral("2.0.0");
        manifest[QStringLiteral("target_platform")] = QStringLiteral("windows-x86_64");
        manifest[QStringLiteral("description")]     = QStringLiteral("Bad sig test");
        manifest[QStringLiteral("signer_key_id")]   = m_signerKeyId;
        manifest[QStringLiteral("components")]      = QJsonArray{};
        manifest[QStringLiteral("signature")]       = QStringLiteral("deadbeef00000000");

        QFile mf(pkgDir + QStringLiteral("/update-manifest.json"));
        mf.open(QIODevice::WriteOnly);
        mf.write(QJsonDocument(manifest).toJson());
        mf.close();

        auto res = m_updateService->importPackage(pkgDir, QStringLiteral("u-sys"));
        // Rejected due to invalid signature
        QVERIFY(!res.isOk());
    }

    void test_importPackage_digestMismatch()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = tmp.path() + QStringLiteral("/bad-digest");
        QDir().mkpath(pkgDir);

        // Write a component with wrong content
        QFile cf(pkgDir + QStringLiteral("/proctorops.exe"));
        cf.open(QIODevice::WriteOnly);
        cf.write("corrupted content");
        cf.close();

        // Build manifest claiming a different digest
        QJsonObject manifestBody;
        manifestBody[QStringLiteral("package_id")]      = QStringLiteral("bad-digest-pkg");
        manifestBody[QStringLiteral("version")]         = QStringLiteral("3.0.0");
        manifestBody[QStringLiteral("target_platform")] = QStringLiteral("windows-x86_64");
        manifestBody[QStringLiteral("description")]     = QStringLiteral("Bad digest test");
        manifestBody[QStringLiteral("signer_key_id")]   = m_signerKeyId;

        QJsonArray comps;
        QJsonObject comp;
        comp[QStringLiteral("name")]    = QStringLiteral("proctorops.exe");
        comp[QStringLiteral("version")] = QStringLiteral("3.0.0");
        comp[QStringLiteral("sha256")]  = QStringLiteral("aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899");
        comp[QStringLiteral("file")]    = QStringLiteral("proctorops.exe");
        comps.append(comp);
        manifestBody[QStringLiteral("components")] = comps;

        const QByteArray manifestBodyBytes = QJsonDocument(manifestBody).toJson(QJsonDocument::Compact);
        auto signRes = Ed25519Signer::sign(manifestBodyBytes, m_privKeyDer);
        QVERIFY(signRes.isOk());

        QJsonObject manifest = manifestBody;
        manifest[QStringLiteral("signature")] = QString::fromLatin1(signRes.value().toHex());

        QFile mf(pkgDir + QStringLiteral("/update-manifest.json"));
        mf.open(QIODevice::WriteOnly);
        mf.write(QJsonDocument(manifest).toJson());
        mf.close();

        auto res = m_updateService->importPackage(pkgDir, QStringLiteral("u-sys"));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::PackageCorrupt);
    }

    // ── Apply and history tests ───────────────────────────────────────────────

    void test_applyPackage_recordsInstallHistory()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = writeValidPackage(tmp.path(), QStringLiteral("1.1.0"));
        QVERIFY(!pkgDir.isEmpty());

        auto impRes = m_updateService->importPackage(pkgDir, QStringLiteral("u-sys"));
        QVERIFY(impRes.isOk());

        const QString packageId = impRes.value().id;
        auto appRes = m_updateService->applyPackage(packageId, QStringLiteral("1.0.0"),
                                                     QStringLiteral("u-sys"),
                                                     issueStepUpWindow(QStringLiteral("u-sys")));
        QVERIFY2(appRes.isOk(), appRes.isErr() ? qPrintable(appRes.errorMessage()) : "");

        auto histRes = m_updateService->listInstallHistory(QStringLiteral("u-sys"));
        QVERIFY(histRes.isOk());
        bool found = false;
        for (const InstallHistoryEntry& h : histRes.value()) {
            if (h.packageId == packageId) {
                found = true;
                QCOMPARE(h.fromVersion, QStringLiteral("1.0.0"));
                QCOMPARE(h.toVersion, QStringLiteral("1.1.0"));
            }
        }
        QVERIFY(found);
    }

    void test_applyPackage_deploysArtifacts()
    {
        const QString runtimeRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + QStringLiteral("/update_runtime");
        QDir runtimeDir(runtimeRoot);
        if (runtimeDir.exists())
            runtimeDir.removeRecursively();

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = writeValidPackage(tmp.path(), QStringLiteral("1.1.5"));
        QVERIFY(!pkgDir.isEmpty());

        auto impRes = m_updateService->importPackage(pkgDir, QStringLiteral("u-sys"));
        QVERIFY(impRes.isOk());

        auto appRes = m_updateService->applyPackage(impRes.value().id,
                                                    QStringLiteral("1.1.4"),
                                                    QStringLiteral("u-sys"),
                                                    issueStepUpWindow(QStringLiteral("u-sys")));
        QVERIFY2(appRes.isOk(), appRes.isErr() ? qPrintable(appRes.errorMessage()) : "");

        const QString liveFilePath = runtimeRoot + QStringLiteral("/live/proctorops.exe");
        QVERIFY(QFileInfo::exists(liveFilePath));

        QFile liveFile(liveFilePath);
        QVERIFY(liveFile.open(QIODevice::ReadOnly));
        QCOMPARE(liveFile.readAll(), QByteArrayLiteral("dummy exe content"));
    }

    void test_applyPackage_onlyFromStaged()
    {
        // Cannot apply a Cancelled package
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = writeValidPackage(tmp.path(), QStringLiteral("1.2.0"));
        QVERIFY(!pkgDir.isEmpty());

        auto impRes = m_updateService->importPackage(pkgDir, QStringLiteral("u-sys"));
        QVERIFY(impRes.isOk());

        const QString packageId = impRes.value().id;
        QVERIFY(m_updateService->cancelPackage(packageId, QStringLiteral("u-sys")).isOk());

        auto appRes = m_updateService->applyPackage(packageId, QStringLiteral("1.0.0"),
                                                     QStringLiteral("u-sys"),
                                                     issueStepUpWindow(QStringLiteral("u-sys")));
        QVERIFY(!appRes.isOk());
        QCOMPARE(appRes.errorCode(), ErrorCode::InvalidState);
    }

    // ── Rollback tests ────────────────────────────────────────────────────────

    void test_rollback_requiresRationale()
    {
        auto res = m_updateService->rollback(QStringLiteral("h-nonexistent"),
                                              QString{}, // empty rationale
                                              QStringLiteral("u-sys"),
                                              issueStepUpWindow(QStringLiteral("u-sys")));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::ValidationFailed);
    }

    void test_rollback_recordsRollbackRecord()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = writeValidPackage(tmp.path(), QStringLiteral("2.0.0"));
        QVERIFY(!pkgDir.isEmpty());

        auto impRes = m_updateService->importPackage(pkgDir, QStringLiteral("u-sys"));
        QVERIFY(impRes.isOk());

        const QString packageId = impRes.value().id;
        QVERIFY(m_updateService->applyPackage(packageId, QStringLiteral("1.0.0"),
                                               QStringLiteral("u-sys"),
                                               issueStepUpWindow(QStringLiteral("u-sys"))).isOk());

        auto histRes = m_updateService->listInstallHistory(QStringLiteral("u-sys"));
        QVERIFY(histRes.isOk());
        QVERIFY(!histRes.value().isEmpty());
        const QString histId = histRes.value().first().id;

        auto rollRes = m_updateService->rollback(histId, QStringLiteral("Stability issues"),
                                                  QStringLiteral("u-sys"),
                                                  issueStepUpWindow(QStringLiteral("u-sys")));
        QVERIFY2(rollRes.isOk(), rollRes.isErr() ? qPrintable(rollRes.errorMessage()) : "");
        QCOMPARE(rollRes.value().rationale, QStringLiteral("Stability issues"));
        QCOMPARE(rollRes.value().toVersion, QStringLiteral("1.0.0")); // rolled back to fromVersion

        auto listRes = m_updateService->listRollbackRecords(QStringLiteral("u-sys"));
        QVERIFY(listRes.isOk());
        QVERIFY(!listRes.value().isEmpty());
    }

    void test_rollback_restoresArtifacts()
    {
        const QString runtimeRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + QStringLiteral("/update_runtime");
        QDir runtimeDir(runtimeRoot);
        if (runtimeDir.exists())
            runtimeDir.removeRecursively();
        QVERIFY(QDir().mkpath(runtimeRoot + QStringLiteral("/live")));

        const QString liveFilePath = runtimeRoot + QStringLiteral("/live/proctorops.exe");
        {
            QFile baseline(liveFilePath);
            QVERIFY(baseline.open(QIODevice::WriteOnly | QIODevice::Truncate));
            baseline.write("baseline-version");
            baseline.close();
        }

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = writeValidPackage(tmp.path(), QStringLiteral("7.0.0"));
        QVERIFY(!pkgDir.isEmpty());

        auto impRes = m_updateService->importPackage(pkgDir, QStringLiteral("u-sys"));
        QVERIFY(impRes.isOk());

        QVERIFY(m_updateService->applyPackage(impRes.value().id,
                                              QStringLiteral("6.9.9"),
                                              QStringLiteral("u-sys"),
                                              issueStepUpWindow(QStringLiteral("u-sys"))).isOk());

        {
            QFile afterApply(liveFilePath);
            QVERIFY(afterApply.open(QIODevice::ReadOnly));
            QCOMPARE(afterApply.readAll(), QByteArrayLiteral("dummy exe content"));
        }

        auto history = m_updateService->listInstallHistory(QStringLiteral("u-sys"));
        QVERIFY(history.isOk());
        QString historyId;
        for (const InstallHistoryEntry& entry : history.value()) {
            if (entry.packageId == impRes.value().id) {
                historyId = entry.id;
                break;
            }
        }
        QVERIFY(!historyId.isEmpty());

        auto rollbackResult = m_updateService->rollback(historyId,
                                                        QStringLiteral("Regression fallback"),
                                                        QStringLiteral("u-sys"),
                                                        issueStepUpWindow(QStringLiteral("u-sys")));
        QVERIFY2(rollbackResult.isOk(), rollbackResult.isErr() ? qPrintable(rollbackResult.errorMessage()) : "");

        QFile restored(liveFilePath);
        QVERIFY(restored.open(QIODevice::ReadOnly));
        QCOMPARE(restored.readAll(), QByteArrayLiteral("baseline-version"));
    }

    void test_listPackages_returns_all()
    {
        auto res = m_updateService->listPackages(QStringLiteral("u-sys"));
        QVERIFY(res.isOk());
        QVERIFY(res.value().size() >= 1);
    }

    void test_listPackages_requiresSecurityAdministrator()
    {
        auto res = m_updateService->listPackages(QStringLiteral("u-op"));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::AuthorizationDenied);
    }

    void test_cancelPackage_requiresSecurityAdministrator()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = writeValidPackage(tmp.path(), QStringLiteral("2.5.0"));
        QVERIFY(!pkgDir.isEmpty());

        auto impRes = m_updateService->importPackage(pkgDir, QStringLiteral("u-sys"));
        QVERIFY(impRes.isOk());

        auto cancelRes = m_updateService->cancelPackage(impRes.value().id, QStringLiteral("u-op"));
        QVERIFY(!cancelRes.isOk());
        QCOMPARE(cancelRes.errorCode(), ErrorCode::AuthorizationDenied);
    }

    void test_listInstallHistory_requiresSecurityAdministrator()
    {
        auto res = m_updateService->listInstallHistory(QStringLiteral("u-op"));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::AuthorizationDenied);
    }

    void test_listRollbackRecords_requiresSecurityAdministrator()
    {
        auto res = m_updateService->listRollbackRecords(QStringLiteral("u-op"));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::AuthorizationDenied);
    }
};

#include <QCryptographicHash>
QTEST_MAIN(TstUpdateService)
#include "tst_update_service.moc"
