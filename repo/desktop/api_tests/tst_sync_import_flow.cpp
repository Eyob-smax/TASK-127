// tst_sync_import_flow.cpp — ProctorOps
// Integration tests for the offline sync package flow:
//   Package export → Ed25519 signature verification → import → conflict detection
//   → conflict resolution → signing key revocation blocks future imports.
// Tests run against an in-memory SQLite database; no network, no LAN share.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
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
#include "models/Sync.h"
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

class TstSyncImportFlow : public QObject {
    Q_OBJECT

private:
    QSqlDatabase      m_db;
    SyncRepository*   m_syncRepo{nullptr};
    CheckInRepository* m_checkInRepo{nullptr};
    AuditRepository*  m_auditRepo{nullptr};
    UserRepository*   m_userRepo{nullptr};
    AesGcmCipher*     m_cipher{nullptr};
    AuditService*     m_auditService{nullptr};
    AuthService*      m_authService{nullptr};
    PackageVerifier*  m_verifier{nullptr};
    SyncService*      m_syncService{nullptr};

    // Signing key pair registered in trust store
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
                                          QStringLiteral("tst_sync_import_flow_db"));
        m_db.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(m_db.open());
        runMigrations(m_db);

        QSqlQuery q(m_db);
        q.exec(QStringLiteral(
            "INSERT OR IGNORE INTO users (id, username, role, status, created_at, updated_at, created_by_user_id) "
            "VALUES ('u-admin', 'admin', 'SECURITY_ADMINISTRATOR', 'Active', datetime('now'), datetime('now'), NULL)"));

        const QByteArray masterKey(32, '\x55');
        m_cipher        = new AesGcmCipher(masterKey);
        m_syncRepo      = new SyncRepository(m_db);
        m_checkInRepo   = new CheckInRepository(m_db);
        m_auditRepo     = new AuditRepository(m_db);
        m_userRepo      = new UserRepository(m_db);
        m_auditService  = new AuditService(*m_auditRepo, *m_cipher);
        m_authService   = new AuthService(*m_userRepo, *m_auditRepo);
        m_verifier      = new PackageVerifier(*m_syncRepo);
        m_syncService   = new SyncService(*m_syncRepo, *m_checkInRepo,
                                           *m_auditRepo, *m_authService, *m_auditService, *m_verifier);

        // Generate a signing key pair and register in trust store
        auto keyRes = Ed25519Signer::generateKeyPair();
        QVERIFY(keyRes.isOk());
        m_privKey = keyRes.value().first;
        m_pubKey  = keyRes.value().second;

        const QString pubKeyHex = QString::fromLatin1(m_pubKey.toHex());
        auto impRes = m_syncService->importSigningKey(
            QStringLiteral("Integration Test Key"), pubKeyHex, QDateTime(),
            QStringLiteral("u-admin"), issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY(impRes.isOk());
        m_signerKeyId = impRes.value().id;
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
        QSqlDatabase::removeDatabase(QStringLiteral("tst_sync_import_flow_db"));
    }

    // Helper: write a valid signed sync package directory
    QString writeValidSyncPackage(const QString& baseDir, const QString& deskId)
    {
        const QString pkgId  = QStringLiteral("sync-pkg-") + deskId;
        const QString pkgDir = baseDir + QStringLiteral("/") + pkgId;
        QDir().mkpath(pkgDir);

        // Write an empty deductions JSONL
        const QByteArray deductBytes;
        QFile df(pkgDir + QStringLiteral("/deductions.jsonl"));
        if (!df.open(QIODevice::WriteOnly)) {
            qWarning() << "writeValidSyncPackage open deductions file failed:" << df.errorString();
            return QString{};
        }
        df.close();

        // Build manifest body
        QJsonObject entities;
        entities[QStringLiteral("deductions")] = QJsonObject{
            { QStringLiteral("file"),         QStringLiteral("deductions.jsonl") },
            { QStringLiteral("sha256"),        sha256Hex(deductBytes) },
            { QStringLiteral("record_count"),  0 }
        };

        QJsonObject body;
        body[QStringLiteral("package_id")]      = pkgId;
        body[QStringLiteral("source_desk_id")]  = deskId;
        body[QStringLiteral("signer_key_id")]   = m_signerKeyId;
        body[QStringLiteral("exported_at")]     =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        body[QStringLiteral("since_watermark")] =
            QDateTime::fromMSecsSinceEpoch(0, Qt::UTC).toString(Qt::ISODateWithMs);
        body[QStringLiteral("entities")]        = entities;

        const QByteArray bodyBytes = QJsonDocument(body).toJson(QJsonDocument::Compact);

        auto signRes = Ed25519Signer::sign(bodyBytes, m_privKey);
        if (!signRes.isOk()) return {};

        QJsonObject manifest = body;
        manifest[QStringLiteral("signature")] = QString::fromLatin1(signRes.value().toHex());

        QFile mf(pkgDir + QStringLiteral("/manifest.json"));
        if (!mf.open(QIODevice::WriteOnly)) {
            qWarning() << "writeValidSyncPackage open manifest file failed:" << mf.errorString();
            return QString{};
        }
        mf.write(QJsonDocument(manifest).toJson());
        mf.close();

        return pkgDir;
    }

    QString writeDeductionsPackage(const QString& baseDir,
                                   const QString& packageId,
                                   const QString& deskId,
                                   const QList<QJsonObject>& deductions)
    {
        const QString pkgDir = baseDir + QStringLiteral("/") + packageId;
        QDir().mkpath(pkgDir);

        QFile df(pkgDir + QStringLiteral("/deductions.jsonl"));
        if (!df.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning() << "writeDeductionsPackage open deductions file failed:" << df.errorString();
            return QString{};
        }
        for (const QJsonObject& record : deductions) {
            df.write(QJsonDocument(record).toJson(QJsonDocument::Compact));
            df.write("\n");
        }
        df.close();

        QFile cf(pkgDir + QStringLiteral("/corrections.jsonl"));
        if (!cf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning() << "writeDeductionsPackage open corrections file failed:" << cf.errorString();
            return QString{};
        }
        cf.close();

        QFile deductionsFile(pkgDir + QStringLiteral("/deductions.jsonl"));
        if (!deductionsFile.open(QIODevice::ReadOnly)) {
            qWarning() << "writeDeductionsPackage read deductions file failed:" << deductionsFile.errorString();
            return QString{};
        }
        const QByteArray deductionsData = deductionsFile.readAll();
        deductionsFile.close();

        QFile correctionsFile(pkgDir + QStringLiteral("/corrections.jsonl"));
        if (!correctionsFile.open(QIODevice::ReadOnly)) {
            qWarning() << "writeDeductionsPackage read corrections file failed:" << correctionsFile.errorString();
            return QString{};
        }
        const QByteArray correctionsData = correctionsFile.readAll();
        correctionsFile.close();

        QJsonObject entities;
        entities[QStringLiteral("deductions")] = QJsonObject{
            { QStringLiteral("file"), QStringLiteral("deductions.jsonl") },
            { QStringLiteral("sha256"), sha256Hex(deductionsData) },
            { QStringLiteral("record_count"), deductions.size() }
        };
        entities[QStringLiteral("corrections")] = QJsonObject{
            { QStringLiteral("file"), QStringLiteral("corrections.jsonl") },
            { QStringLiteral("sha256"), sha256Hex(correctionsData) },
            { QStringLiteral("record_count"), 0 }
        };

        QJsonObject body;
        body[QStringLiteral("package_id")]      = packageId;
        body[QStringLiteral("source_desk_id")]  = deskId;
        body[QStringLiteral("signer_key_id")]   = m_signerKeyId;
        body[QStringLiteral("exported_at")]     =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        body[QStringLiteral("since_watermark")] =
            QDateTime::fromMSecsSinceEpoch(0, Qt::UTC).toString(Qt::ISODateWithMs);
        body[QStringLiteral("entities")]        = entities;

        const QByteArray bodyBytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
        auto signRes = Ed25519Signer::sign(bodyBytes, m_privKey);
        if (!signRes.isOk()) {
            qWarning() << "writeDeductionsPackage sign failed:" << signRes.errorMessage();
            return QString{};
        }

        QJsonObject manifest = body;
        manifest[QStringLiteral("signature")] = QString::fromLatin1(signRes.value().toHex());

        QFile mf(pkgDir + QStringLiteral("/manifest.json"));
        if (!mf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning() << "writeDeductionsPackage open manifest file failed:" << mf.errorString();
            return QString{};
        }
        mf.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
        mf.close();

        return pkgDir;
    }

    // ── Test: valid package import ────────────────────────────────────────────

    void test_importValidPackage_succeeds()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = writeValidSyncPackage(tmp.path(), QStringLiteral("desk-A"));
        QVERIFY(!pkgDir.isEmpty());

        auto res = m_syncService->importPackage(pkgDir, QStringLiteral("u-admin"));
        QVERIFY2(res.isOk(), res.isErr() ? qPrintable(res.errorMessage()) : "");

        const SyncPackage& pkg = res.value();
        QCOMPARE(pkg.sourceDeskId, QStringLiteral("desk-A"));
        // Status is Verified or Applied (no conflicts expected on empty delta)
        QVERIFY(pkg.status == SyncPackageStatus::Verified ||
                pkg.status == SyncPackageStatus::Applied);
    }

    void test_exportThenImport_roundTripAcrossDesks()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        auto exportRes = m_syncService->exportPackage(
            tmp.path(),
            QStringLiteral("desk-source"),
            m_signerKeyId,
            m_privKey,
            QStringLiteral("u-admin"),
            issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY2(exportRes.isOk(), exportRes.isErr() ? qPrintable(exportRes.errorMessage()) : "");
        QVERIFY(QFile::exists(exportRes.value().packageFilePath + QStringLiteral("/manifest.json")));

        const QString targetConn =
            QStringLiteral("tst_sync_target_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        {
            QSqlDatabase targetDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), targetConn);
            targetDb.setDatabaseName(QStringLiteral(":memory:"));
            QVERIFY(targetDb.open());
            runMigrations(targetDb);

            QSqlQuery q(targetDb);
            q.exec(QStringLiteral(
                "INSERT OR IGNORE INTO users (id, username, role, status, created_at, updated_at, created_by_user_id) "
                "VALUES ('u-admin', 'target-admin', 'SECURITY_ADMINISTRATOR', 'Active', datetime('now'), datetime('now'), NULL)"));

            const QByteArray targetMasterKey(32, '\x57');
            AesGcmCipher targetCipher(targetMasterKey);
            SyncRepository targetSyncRepo(targetDb);
            CheckInRepository targetCheckInRepo(targetDb);
            AuditRepository targetAuditRepo(targetDb);
            UserRepository targetUserRepo(targetDb);
            AuditService targetAuditSvc(targetAuditRepo, targetCipher);
            AuthService targetAuthSvc(targetUserRepo, targetAuditRepo);
            PackageVerifier targetVerifier(targetSyncRepo);
            SyncService targetSyncSvc(targetSyncRepo,
                                      targetCheckInRepo,
                                      targetAuditRepo,
                                      targetAuthSvc,
                                      targetAuditSvc,
                                      targetVerifier);

            TrustedSigningKey trusted;
            trusted.id = m_signerKeyId;
            trusted.label = QStringLiteral("Roundtrip Key");
            trusted.publicKeyDerHex = QString::fromLatin1(m_pubKey.toHex());
            trusted.fingerprint = sha256Hex(m_pubKey);
            trusted.importedAt = QDateTime::currentDateTimeUtc();
            trusted.importedByUserId = QStringLiteral("u-admin");
            trusted.expiresAt = QDateTime();
            trusted.revoked = false;
            QVERIFY(targetSyncRepo.insertSigningKey(trusted).isOk());

            auto importRes = targetSyncSvc.importPackage(exportRes.value().packageFilePath,
                                                         QStringLiteral("u-admin"));
            QVERIFY2(importRes.isOk(), importRes.isErr() ? qPrintable(importRes.errorMessage()) : "");
            QCOMPARE(importRes.value().id, exportRes.value().id);
            QVERIFY(importRes.value().status == SyncPackageStatus::Applied
                    || importRes.value().status == SyncPackageStatus::Partial);
        }

        QSqlDatabase::removeDatabase(targetConn);
    }

    // ── Test: invalid signature is rejected ───────────────────────────────────

    void test_importPackage_invalidSignature_rejected()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = tmp.path() + QStringLiteral("/bad-sig-pkg");
        QDir().mkpath(pkgDir);

        QJsonObject manifest;
        manifest[QStringLiteral("package_id")]      = QStringLiteral("bad-sig-pkg");
        manifest[QStringLiteral("source_desk_id")]  = QStringLiteral("desk-X");
        manifest[QStringLiteral("signer_key_id")]   = m_signerKeyId;
        manifest[QStringLiteral("exported_at")]     =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        manifest[QStringLiteral("since_watermark")] =
            QDateTime::fromMSecsSinceEpoch(0, Qt::UTC).toString(Qt::ISODateWithMs);
        manifest[QStringLiteral("entities")]        = QJsonObject{};
        // Corrupted/garbage signature
        manifest[QStringLiteral("signature")]       = QStringLiteral("deadbeef00000000");

        QFile mf(pkgDir + QStringLiteral("/manifest.json"));
        mf.open(QIODevice::WriteOnly);
        mf.write(QJsonDocument(manifest).toJson());
        mf.close();

        auto res = m_syncService->importPackage(pkgDir, QStringLiteral("u-admin"));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::SignatureInvalid);
    }

    // ── Test: missing manifest ────────────────────────────────────────────────

    void test_importPackage_missingManifest_rejected()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        // No manifest.json in the directory
        auto res = m_syncService->importPackage(tmp.path(), QStringLiteral("u-admin"));
        QVERIFY(!res.isOk());
        QCOMPARE(res.errorCode(), ErrorCode::PackageCorrupt);
    }

    // ── Test: revoked key blocks import ───────────────────────────────────────

    void test_importPackage_revokedKey_rejected()
    {
        // Generate a new key, register it, then immediately revoke it
        auto keyRes = Ed25519Signer::generateKeyPair();
        QVERIFY(keyRes.isOk());
        const QByteArray& privKey = keyRes.value().first;
        const QString pubKeyHex   = QString::fromLatin1(keyRes.value().second.toHex());

        auto impKeyRes = m_syncService->importSigningKey(
            QStringLiteral("Key to Revoke"), pubKeyHex, QDateTime(),
            QStringLiteral("u-admin"), issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY(impKeyRes.isOk());
        const QString revokedKeyId = impKeyRes.value().id;

        QVERIFY(m_syncService->revokeSigningKey(revokedKeyId,
                                                  QStringLiteral("u-admin"),
                                                  issueStepUpWindow(QStringLiteral("u-admin"))).isOk());

        // Build a validly-signed package using the now-revoked key
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = tmp.path() + QStringLiteral("/revoked-pkg");
        QDir().mkpath(pkgDir);

        QJsonObject body;
        body[QStringLiteral("package_id")]      = QStringLiteral("revoked-pkg");
        body[QStringLiteral("source_desk_id")]  = QStringLiteral("desk-revoked");
        body[QStringLiteral("signer_key_id")]   = revokedKeyId;
        body[QStringLiteral("exported_at")]     =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        body[QStringLiteral("since_watermark")] =
            QDateTime::fromMSecsSinceEpoch(0, Qt::UTC).toString(Qt::ISODateWithMs);
        body[QStringLiteral("entities")]        = QJsonObject{};

        const QByteArray bodyBytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
        auto signRes = Ed25519Signer::sign(bodyBytes, privKey);
        QVERIFY(signRes.isOk());

        QJsonObject manifest = body;
        manifest[QStringLiteral("signature")] = QString::fromLatin1(signRes.value().toHex());

        QFile mf(pkgDir + QStringLiteral("/manifest.json"));
        mf.open(QIODevice::WriteOnly);
        mf.write(QJsonDocument(manifest).toJson());
        mf.close();

        auto res = m_syncService->importPackage(pkgDir, QStringLiteral("u-admin"));
        // Revoked key must be rejected even if signature is mathematically valid
        QVERIFY(!res.isOk());
    }

    // ── Test: package list retention ──────────────────────────────────────────

    void test_listPackages_retainsImportedPackages()
    {
        // After the earlier import, listPackages must return at least one entry
        auto res = m_syncService->listPackages(QStringLiteral("u-admin"));
        QVERIFY(res.isOk());
        QVERIFY(!res.value().isEmpty());
    }

    // ── Test: no conflicts for non-overlapping package ────────────────────────

    void test_noConflictsOnCleanImport()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = writeValidSyncPackage(tmp.path(), QStringLiteral("desk-B"));
        QVERIFY(!pkgDir.isEmpty());

        auto impRes = m_syncService->importPackage(pkgDir, QStringLiteral("u-admin"));
        QVERIFY(impRes.isOk());

        auto conflictsRes = m_syncService->listPendingConflicts(impRes.value().id,
                                    QStringLiteral("u-admin"));
        QVERIFY(conflictsRes.isOk());
        // An empty delta should produce zero conflicts
        QVERIFY(conflictsRes.value().isEmpty());
    }

    // ── Test: signing key list reflects all managed keys ─────────────────────

    void test_listSigningKeys_includesAllImported()
    {
        auto res = m_syncService->listSigningKeys(QStringLiteral("u-admin"));
        QVERIFY(res.isOk());
        // At least the integration test key plus the revoked key from earlier test
        QVERIFY(res.value().size() >= 2);

        bool foundMain  = false;
        bool foundRevoked = false;
        for (const TrustedSigningKey& k : res.value()) {
            if (k.id == m_signerKeyId)    foundMain    = true;
            if (k.revoked)                foundRevoked = true;
        }
        QVERIFY(foundMain);
        QVERIFY(foundRevoked);
    }

    void test_importPackage_appliesIncomingDeduction()
    {
        QSqlQuery seed(m_db);
        QVERIFY(seed.exec(QStringLiteral(
            "INSERT INTO members (id, member_id, member_id_hash, barcode_encrypted, mobile_encrypted, "
            "name_encrypted, deleted, created_at, updated_at) "
            "VALUES ('m-sync-1', 'enc-mid', 'hash-mid', 'enc-barcode', 'enc-mobile', "
            "'enc-name', 0, datetime('now'), datetime('now'))")));
        QVERIFY(seed.exec(QStringLiteral(
            "INSERT INTO punch_cards (id, member_id, product_code, initial_balance, current_balance, created_at, updated_at) "
            "VALUES ('pc-sync-1', 'm-sync-1', 'PUNCH10', 10, 10, datetime('now'), datetime('now'))")));

        QJsonObject deduction;
        deduction[QStringLiteral("id")] = QStringLiteral("ded-sync-1");
        deduction[QStringLiteral("member_id")] = QStringLiteral("m-sync-1");
        deduction[QStringLiteral("session_id")] = QStringLiteral("session-101");
        deduction[QStringLiteral("punch_card_id")] = QStringLiteral("pc-sync-1");
        deduction[QStringLiteral("checkin_attempt_id")] = QStringLiteral("attempt-sync-1");
        deduction[QStringLiteral("sessions_deducted")] = 1;
        deduction[QStringLiteral("balance_before")] = 10;
        deduction[QStringLiteral("balance_after")] = 9;
        deduction[QStringLiteral("deducted_at")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        deduction[QStringLiteral("reversed_by_correction_id")] = QString();

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = writeDeductionsPackage(tmp.path(),
                                                      QStringLiteral("sync-apply-1"),
                                                      QStringLiteral("desk-materialize"),
                                                      { deduction });

        auto importRes = m_syncService->importPackage(pkgDir, QStringLiteral("u-admin"));
        QVERIFY2(importRes.isOk(), importRes.isErr() ? qPrintable(importRes.errorMessage()) : "");
        QVERIFY(importRes.value().status == SyncPackageStatus::Applied
                || importRes.value().status == SyncPackageStatus::Partial);

        auto appliedDeduction = m_checkInRepo->getDeduction(QStringLiteral("ded-sync-1"));
        QVERIFY2(appliedDeduction.isOk(), appliedDeduction.isErr() ? qPrintable(appliedDeduction.errorMessage()) : "");

        QSqlQuery balance(m_db);
        balance.prepare(QStringLiteral("SELECT current_balance FROM punch_cards WHERE id = ?"));
        balance.addBindValue(QStringLiteral("pc-sync-1"));
        QVERIFY(balance.exec());
        QVERIFY(balance.next());
        QCOMPARE(balance.value(0).toInt(), 9);

        auto entities = m_syncRepo->getPackageEntities(QStringLiteral("sync-apply-1"));
        QVERIFY(entities.isOk());
        bool deductionAppliedFlag = false;
        for (const SyncPackageEntity& entity : entities.value()) {
            if (entity.entityType == QLatin1String("deductions"))
                deductionAppliedFlag = entity.applied;
        }
        QVERIFY(deductionAppliedFlag);
    }

    void test_resolveConflict_linksCompensatingAction()
    {
        QSqlQuery seed(m_db);
        QVERIFY(seed.exec(QStringLiteral(
            "INSERT INTO members (id, member_id, member_id_hash, barcode_encrypted, mobile_encrypted, "
            "name_encrypted, deleted, created_at, updated_at) "
            "VALUES ('m-sync-2', 'enc-mid2', 'hash-mid2', 'enc-barcode2', 'enc-mobile2', "
            "'enc-name2', 0, datetime('now'), datetime('now'))")));
        QVERIFY(seed.exec(QStringLiteral(
            "INSERT INTO punch_cards (id, member_id, product_code, initial_balance, current_balance, created_at, updated_at) "
            "VALUES ('pc-sync-2', 'm-sync-2', 'PUNCH10', 10, 9, datetime('now'), datetime('now'))")));
        QVERIFY(seed.exec(QStringLiteral(
            "INSERT INTO checkin_attempts (id, member_id, session_id, operator_user_id, status, attempted_at, deduction_event_id, failure_reason) "
            "VALUES ('attempt-local-1', 'm-sync-2', 'session-dup', 'u-admin', 'Success', datetime('now'), 'ded-local-1', NULL)")));
        QVERIFY(seed.exec(QStringLiteral(
            "INSERT INTO deduction_events (id, member_id, punch_card_id, checkin_attempt_id, sessions_deducted, "
            "balance_before, balance_after, deducted_at, reversed_by_correction_id) "
            "VALUES ('ded-local-1', 'm-sync-2', 'pc-sync-2', 'attempt-local-1', 1, 10, 9, datetime('now'), NULL)")));

        QJsonObject incoming;
        incoming[QStringLiteral("id")] = QStringLiteral("ded-incoming-1");
        incoming[QStringLiteral("member_id")] = QStringLiteral("m-sync-2");
        incoming[QStringLiteral("session_id")] = QStringLiteral("session-dup");
        incoming[QStringLiteral("punch_card_id")] = QStringLiteral("pc-sync-2");
        incoming[QStringLiteral("checkin_attempt_id")] = QStringLiteral("attempt-incoming-1");
        incoming[QStringLiteral("sessions_deducted")] = 1;
        incoming[QStringLiteral("balance_before")] = 10;
        incoming[QStringLiteral("balance_after")] = 9;
        incoming[QStringLiteral("deducted_at")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        incoming[QStringLiteral("reversed_by_correction_id")] = QString();

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString pkgDir = writeDeductionsPackage(tmp.path(),
                                                      QStringLiteral("sync-conflict-1"),
                                                      QStringLiteral("desk-conflict"),
                                                      { incoming });

        auto importRes = m_syncService->importPackage(pkgDir, QStringLiteral("u-admin"));
        QVERIFY(importRes.isOk());
        QCOMPARE(importRes.value().status, SyncPackageStatus::Partial);

        auto pending = m_syncService->listPendingConflicts(importRes.value().id, QStringLiteral("u-admin"));
        QVERIFY(pending.isOk());
        QVERIFY(!pending.value().isEmpty());

        auto resolveRes = m_syncService->resolveConflict(pending.value().first().id,
                                                         ConflictStatus::ResolvedAcceptIncoming,
                                                         QStringLiteral("u-admin"),
                                                         issueStepUpWindow(QStringLiteral("u-admin")));
        QVERIFY2(resolveRes.isOk(), resolveRes.isErr() ? qPrintable(resolveRes.errorMessage()) : "");

        auto resolvedConflict = m_syncRepo->getConflict(pending.value().first().id);
        QVERIFY(resolvedConflict.isOk());
        QVERIFY(!resolvedConflict.value().resolutionActionType.isEmpty());
        QVERIFY(!resolvedConflict.value().resolutionActionId.isEmpty());

        auto localDeduction = m_checkInRepo->getDeduction(QStringLiteral("ded-local-1"));
        QVERIFY(localDeduction.isOk());
        QVERIFY(!localDeduction.value().reversedByCorrectionId.isEmpty());

        auto importedDeduction = m_checkInRepo->getDeduction(QStringLiteral("ded-incoming-1"));
        QVERIFY(importedDeduction.isOk());
    }
};

#include <QCryptographicHash>
QTEST_MAIN(TstSyncImportFlow)
#include "tst_sync_import_flow.moc"
