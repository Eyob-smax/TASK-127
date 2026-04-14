// tst_update_flow.cpp — ProctorOps
// Integration tests for the offline update/rollback flow:
//   Package import → signature verification → staging → apply → install history
//   → rollback → rollback record retention.
//
// Override note: delivery does not require a signed .msi artifact.
// The full domain model, signature verification, staging, install history,
// and rollback logic are all in scope and tested here.

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
#include <QDir>
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
#include "models/Update.h"
#include "utils/Migration.h"

static void runMigrations(QSqlDatabase& db)
{
    Migration runner(db, QStringLiteral(SOURCE_ROOT "/database/migrations"));
    const auto result = runner.applyPending();
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

static QString sha256Hex(const QByteArray& data)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

class TstUpdateFlow : public QObject {
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

    QByteArray m_privKey;
    QByteArray m_pubKey;
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
                                          QStringLiteral("tst_update_flow_db"));
        m_db.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(m_db.open());
        runMigrations(m_db);

        QSqlQuery q(m_db);
        q.exec(QStringLiteral(
            "INSERT OR IGNORE INTO users (id, username, role, status, created_at, updated_at, created_by_user_id) "
            "VALUES ('u-admin', 'admin', 'SECURITY_ADMINISTRATOR', 'Active', datetime('now'), datetime('now'), NULL)"));

        const QByteArray masterKey(32, '\x66');
        m_cipher        = new AesGcmCipher(masterKey);
        m_syncRepo      = new SyncRepository(m_db);
        m_updateRepo    = new UpdateRepository(m_db);
        m_auditRepo     = new AuditRepository(m_db);
        m_userRepo      = new UserRepository(m_db);
        m_auditService  = new AuditService(*m_auditRepo, *m_cipher);
        m_authService   = new AuthService(*m_userRepo, *m_auditRepo);
        m_verifier      = new PackageVerifier(*m_syncRepo);
        m_updateService = new UpdateService(*m_updateRepo, *m_syncRepo,
                                             *m_authService, *m_verifier, *m_auditService);

        // Generate a signing key pair and register it in the trust store
        auto keyRes = Ed25519Signer::generateKeyPair();
        QVERIFY(keyRes.isOk());
        m_privKey = keyRes.value().first;
        m_pubKey  = keyRes.value().second;

        TrustedSigningKey key;
        key.id               = QStringLiteral("k-update-flow");
        key.label            = QStringLiteral("Update Flow Test Key");
        key.publicKeyDerHex  = QString::fromLatin1(m_pubKey.toHex());
        key.fingerprint      = sha256Hex(m_pubKey);
        key.importedAt       = QDateTime::currentDateTimeUtc();
        key.importedByUserId = QStringLiteral("u-admin");
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
        QSqlDatabase::removeDatabase(QStringLiteral("tst_update_flow_db"));
    }

    // Helper: write a validly-signed .proctorpkg directory
    QString writeValidPackage(const QString& baseDir, const QString& version)
    {
        const QString pkgDir = baseDir + QStringLiteral("/pkg-") + version;
        QDir().mkpath(pkgDir);

        const QByteArray exeContent =
            QStringLiteral("ProctorOps binary content v%1").arg(version).toUtf8();
        QFile cf(pkgDir + QStringLiteral("/proctorops.exe"));
        if (!cf.open(QIODevice::WriteOnly)) {
            qWarning() << "writeValidPackage open component failed:" << cf.errorString();
            return QString{};
        }
        cf.write(exeContent);
        cf.close();

        QJsonObject manifestBody;
        manifestBody[QStringLiteral("package_id")]      = QStringLiteral("pkg-") + version;
        manifestBody[QStringLiteral("version")]         = version;
        manifestBody[QStringLiteral("target_platform")] = QStringLiteral("windows-x86_64");
        manifestBody[QStringLiteral("description")]     =
            QStringLiteral("Integration test update v%1").arg(version);
        manifestBody[QStringLiteral("signer_key_id")]   = m_signerKeyId;

        QJsonArray components;
        QJsonObject comp;
        comp[QStringLiteral("name")]    = QStringLiteral("proctorops.exe");
        comp[QStringLiteral("version")] = version;
        comp[QStringLiteral("sha256")]  = sha256Hex(exeContent);
        comp[QStringLiteral("file")]    = QStringLiteral("proctorops.exe");
        components.append(comp);
        manifestBody[QStringLiteral("components")] = components;

        const QByteArray bodyBytes =
            QJsonDocument(manifestBody).toJson(QJsonDocument::Compact);
        auto signRes = Ed25519Signer::sign(bodyBytes, m_privKey);
        if (!signRes.isOk()) return {};

        QJsonObject manifest = manifestBody;
        manifest[QStringLiteral("signature")] = QString::fromLatin1(signRes.value().toHex());

        QFile mf(pkgDir + QStringLiteral("/update-manifest.json"));
        if (!mf.open(QIODevice::WriteOnly)) {
            qWarning() << "writeValidPackage open manifest failed:" << mf.errorString();
            return QString{};
        }
        mf.write(QJsonDocument(manifest).toJson());
        mf.close();

        return pkgDir;
    }

    // ── Test: full update flow ────────────────────────────────────────────────

    void test_fullUpdateFlow()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Step 1: Import and stage
        const QString pkgDir = writeValidPackage(tmp.path(), QStringLiteral("2.1.0"));
        QVERIFY(!pkgDir.isEmpty());

        auto importRes = m_updateService->importPackage(pkgDir, QStringLiteral("u-admin"));
        QVERIFY2(importRes.isOk(), importRes.isErr() ? qPrintable(importRes.errorMessage()) : "");
        QCOMPARE(importRes.value().status, UpdatePackageStatus::Staged);
        QVERIFY(importRes.value().signatureValid);
        QCOMPARE(importRes.value().version, QStringLiteral("2.1.0"));

        const QString packageId = importRes.value().id;

        // Step 2: Apply
        auto applyRes = m_updateService->applyPackage(packageId,
                                                       QStringLiteral("2.0.0"),
                                                       QStringLiteral("u-admin"),
                                                       issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY2(applyRes.isOk(), applyRes.isErr() ? qPrintable(applyRes.errorMessage()) : "");

        // Step 3: Verify install history entry
        auto histRes = m_updateService->listInstallHistory(QStringLiteral("u-admin"));
        QVERIFY(histRes.isOk());
        bool found = false;
        for (const InstallHistoryEntry& h : histRes.value()) {
            if (h.packageId == packageId) {
                found = true;
                QCOMPARE(h.fromVersion, QStringLiteral("2.0.0"));
                QCOMPARE(h.toVersion,   QStringLiteral("2.1.0"));
                QCOMPARE(h.appliedByUserId, QStringLiteral("u-admin"));
            }
        }
        QVERIFY(found);
    }

    // ── Test: rollback flow ───────────────────────────────────────────────────

    void test_rollback_restoresPriorVersion()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Import and apply a package to create a history entry
        const QString pkgDir = writeValidPackage(tmp.path(), QStringLiteral("3.0.0"));
        QVERIFY(!pkgDir.isEmpty());

        auto importRes = m_updateService->importPackage(pkgDir, QStringLiteral("u-admin"));
        QVERIFY(importRes.isOk());

        QVERIFY(m_updateService->applyPackage(importRes.value().id,
                                               QStringLiteral("2.1.0"),
                                               QStringLiteral("u-admin"),
                                               issueStepUpWindow(QStringLiteral("u-admin"))).isOk());

        // Find the history entry
        auto histRes = m_updateService->listInstallHistory(QStringLiteral("u-admin"));
        QVERIFY(histRes.isOk());
        QVERIFY(!histRes.value().isEmpty());

        // Find the entry for this specific package
        QString histId;
        for (const InstallHistoryEntry& h : histRes.value()) {
            if (h.packageId == importRes.value().id) { histId = h.id; break; }
        }
        QVERIFY(!histId.isEmpty());

        // Rollback
        auto rollRes = m_updateService->rollback(histId,
                                                  QStringLiteral("Regression in 3.0.0 reports"),
                                                  QStringLiteral("u-admin"),
                                                  issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY2(rollRes.isOk(), rollRes.isErr() ? qPrintable(rollRes.errorMessage()) : "");
        QCOMPARE(rollRes.value().toVersion,   QStringLiteral("2.1.0")); // rolled back to from
        QCOMPARE(rollRes.value().rationale,   QStringLiteral("Regression in 3.0.0 reports"));
        QCOMPARE(rollRes.value().rolledBackByUserId, QStringLiteral("u-admin"));

        // Rollback record retained
        auto listRes = m_updateService->listRollbackRecords(QStringLiteral("u-admin"));
        QVERIFY(listRes.isOk());
        QVERIFY(!listRes.value().isEmpty());
    }

    // ── Test: rollback requires rationale ────────────────────────────────────

    void test_rollback_emptyRationale_rejected()
    {
        auto res = m_updateService->rollback(QStringLiteral("nonexistent-hist"),
                                              QString{},
                                              QStringLiteral("u-admin"),
                                              issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::ValidationFailed);
    }

    // ── Test: invalid signature is rejected ───────────────────────────────────

    void test_importPackage_invalidSignature_rejected()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = tmp.path() + QStringLiteral("/bad-sig");
        QDir().mkpath(pkgDir);

        QJsonObject manifest;
        manifest[QStringLiteral("package_id")]      = QStringLiteral("bad-sig-update");
        manifest[QStringLiteral("version")]         = QStringLiteral("9.9.9");
        manifest[QStringLiteral("target_platform")] = QStringLiteral("windows-x86_64");
        manifest[QStringLiteral("description")]     = QStringLiteral("Bad signature test");
        manifest[QStringLiteral("signer_key_id")]   = m_signerKeyId;
        manifest[QStringLiteral("components")]      = QJsonArray{};
        manifest[QStringLiteral("signature")]       = QStringLiteral("cafebabe00000000");

        QFile mf(pkgDir + QStringLiteral("/update-manifest.json"));
        mf.open(QIODevice::WriteOnly);
        mf.write(QJsonDocument(manifest).toJson());
        mf.close();

        auto res = m_updateService->importPackage(pkgDir, QStringLiteral("u-admin"));
        QVERIFY(!res.isOk());
    }

    // ── Test: cancelled package cannot be applied ─────────────────────────────

    void test_cancelledPackage_cannotBeApplied()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = writeValidPackage(tmp.path(), QStringLiteral("4.0.0"));
        QVERIFY(!pkgDir.isEmpty());

        auto importRes = m_updateService->importPackage(pkgDir, QStringLiteral("u-admin"));
        QVERIFY(importRes.isOk());

        const QString packageId = importRes.value().id;
        QVERIFY(m_updateService->cancelPackage(packageId, QStringLiteral("u-admin")).isOk());

        auto applyRes = m_updateService->applyPackage(packageId, QStringLiteral("3.0.0"),
                                                       QStringLiteral("u-admin"),
                                                       issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY(!applyRes.isOk());
        QCOMPARE(applyRes.errorCode(), ErrorCode::InvalidState);
    }

    // ── Test: digest mismatch is rejected ─────────────────────────────────────

    void test_importPackage_digestMismatch_rejected()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = tmp.path() + QStringLiteral("/bad-digest");
        QDir().mkpath(pkgDir);

        // Write file with different content than manifest claims
        QFile cf(pkgDir + QStringLiteral("/proctorops.exe"));
        cf.open(QIODevice::WriteOnly);
        cf.write("tampered content");
        cf.close();

        QJsonObject manifestBody;
        manifestBody[QStringLiteral("package_id")]      = QStringLiteral("bad-digest-flow");
        manifestBody[QStringLiteral("version")]         = QStringLiteral("5.0.0");
        manifestBody[QStringLiteral("target_platform")] = QStringLiteral("windows-x86_64");
        manifestBody[QStringLiteral("description")]     = QStringLiteral("Digest mismatch test");
        manifestBody[QStringLiteral("signer_key_id")]   = m_signerKeyId;

        QJsonArray comps;
        QJsonObject comp;
        comp[QStringLiteral("name")]    = QStringLiteral("proctorops.exe");
        comp[QStringLiteral("version")] = QStringLiteral("5.0.0");
        // Intentionally wrong digest
        comp[QStringLiteral("sha256")]  =
            QStringLiteral("aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899");
        comp[QStringLiteral("file")]    = QStringLiteral("proctorops.exe");
        comps.append(comp);
        manifestBody[QStringLiteral("components")] = comps;

        const QByteArray bodyBytes =
            QJsonDocument(manifestBody).toJson(QJsonDocument::Compact);
        auto signRes = Ed25519Signer::sign(bodyBytes, m_privKey);
        QVERIFY(signRes.isOk());

        QJsonObject manifest = manifestBody;
        manifest[QStringLiteral("signature")] = QString::fromLatin1(signRes.value().toHex());

        QFile mf(pkgDir + QStringLiteral("/update-manifest.json"));
        mf.open(QIODevice::WriteOnly);
        mf.write(QJsonDocument(manifest).toJson());
        mf.close();

        auto res = m_updateService->importPackage(pkgDir, QStringLiteral("u-admin"));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::PackageCorrupt);
    }

    // ── Test: package list includes all imported packages ─────────────────────

    void test_listPackages_includesAll()
    {
        auto res = m_updateService->listPackages(QStringLiteral("u-admin"));
        QVERIFY(res.isOk());
        // Should have at least the packages from previous test cases
        QVERIFY(res.value().size() >= 1);
    }
};

#include <QCryptographicHash>
QTEST_MAIN(TstUpdateFlow)
#include "tst_update_flow.moc"
