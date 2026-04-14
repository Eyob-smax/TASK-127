// SyncService.cpp - ProctorOps

#include "SyncService.h"

#include "AuthService.h"
#include "crypto/Ed25519Signer.h"
#include "utils/Logger.h"
#include "models/CommonTypes.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace {

QString sha256OfFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    QCryptographicHash hasher(QCryptographicHash::Sha256);
    hasher.addData(&f);
    return QString::fromLatin1(hasher.result().toHex());
}

QString sha256OfBytes(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString normalizedPath(QString path)
{
    path = QDir::cleanPath(path);
    return QDir::fromNativeSeparators(path);
}

Result<QList<QJsonObject>> readJsonLinesFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return Result<QList<QJsonObject>>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("Cannot open entity file: %1").arg(filePath));
    }

    QList<QJsonObject> records;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty())
            continue;

        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) {
            return Result<QList<QJsonObject>>::err(
                ErrorCode::PackageCorrupt,
                QStringLiteral("Entity file contains invalid JSON: %1").arg(filePath));
        }

        records.append(doc.object());
    }

    return Result<QList<QJsonObject>>::ok(std::move(records));
}

}

SyncService::SyncService(ISyncRepository& syncRepo,
                         ICheckInRepository& checkInRepo,
                         IAuditRepository& auditRepo,
                         AuthService& authService,
                         AuditService& auditService,
                         PackageVerifier& verifier)
    : m_syncRepo(syncRepo)
    , m_checkInRepo(checkInRepo)
    , m_auditRepo(auditRepo)
    , m_authService(authService)
    , m_auditService(auditService)
    , m_verifier(verifier)
{
}

Result<SyncPackage> SyncService::exportPackage(const QString& destinationDir,
                                               const QString& deskId,
                                               const QString& signerKeyId,
                                               const QByteArray& signerPrivKeyDer,
                                               const QString& actorUserId,
                                               const QString& stepUpWindowId)
{
    auto authResult = m_authService.authorizePrivilegedAction(
        actorUserId,
        Role::SecurityAdministrator,
        stepUpWindowId);
    if (authResult.isErr())
        return Result<SyncPackage>::err(authResult.errorCode(), authResult.errorMessage());

    QDateTime sinceWatermark = QDateTime::fromMSecsSinceEpoch(0, Qt::UTC);
    auto latestExportResult = m_syncRepo.latestExportForDesk(deskId);
    if (!latestExportResult.isOk())
        return Result<SyncPackage>::err(latestExportResult.errorCode(), latestExportResult.errorMessage());
    if (latestExportResult.value().has_value() && latestExportResult.value()->exportedAt.isValid())
        sinceWatermark = latestExportResult.value()->exportedAt;

    const QDateTime exportedAt = QDateTime::currentDateTimeUtc();
    const QString packageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString packageDir = destinationDir + QStringLiteral("/") + packageId;
    if (!QDir().mkpath(packageDir)) {
        return Result<SyncPackage>::err(
            ErrorCode::IoError,
            QStringLiteral("Cannot create package directory: %1").arg(packageDir));
    }

    auto deductionsResult = writeDeductionsDelta(packageDir, sinceWatermark);
    if (!deductionsResult.isOk())
        return Result<SyncPackage>::err(deductionsResult.errorCode(), deductionsResult.errorMessage());

    auto correctionsResult = writeCorrectionsDelta(packageDir, sinceWatermark);
    if (!correctionsResult.isOk())
        return Result<SyncPackage>::err(correctionsResult.errorCode(), correctionsResult.errorMessage());

    QJsonObject entities;
    entities[QStringLiteral("deductions")] = QJsonObject{
        {QStringLiteral("file"), QStringLiteral("deductions.jsonl")},
        {QStringLiteral("sha256"), deductionsResult.value().sha256Hex},
        {QStringLiteral("record_count"), deductionsResult.value().recordCount}
    };
    entities[QStringLiteral("corrections")] = QJsonObject{
        {QStringLiteral("file"), QStringLiteral("corrections.jsonl")},
        {QStringLiteral("sha256"), correctionsResult.value().sha256Hex},
        {QStringLiteral("record_count"), correctionsResult.value().recordCount}
    };

    QJsonObject manifestBody;
    manifestBody[QStringLiteral("package_id")] = packageId;
    manifestBody[QStringLiteral("source_desk_id")] = deskId;
    manifestBody[QStringLiteral("signer_key_id")] = signerKeyId;
    manifestBody[QStringLiteral("exported_at")] = exportedAt.toString(Qt::ISODateWithMs);
    manifestBody[QStringLiteral("since_watermark")] = sinceWatermark.toString(Qt::ISODateWithMs);
    manifestBody[QStringLiteral("entities")] = entities;

    const QByteArray manifestBodyBytes =
        QJsonDocument(manifestBody).toJson(QJsonDocument::Compact);

    auto signResult = Ed25519Signer::sign(manifestBodyBytes, signerPrivKeyDer);
    if (!signResult.isOk())
        return Result<SyncPackage>::err(signResult.errorCode(), signResult.errorMessage());

    QJsonObject manifest = manifestBody;
    manifest[QStringLiteral("signature")] = QString::fromLatin1(signResult.value().toHex());

    QFile manifestFile(packageDir + QStringLiteral("/manifest.json"));
    if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return Result<SyncPackage>::err(
            ErrorCode::IoError,
            QStringLiteral("Cannot write manifest.json"));
    }
    manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    manifestFile.close();

    SyncPackage pkg;
    pkg.id = packageId;
    pkg.sourceDeskId = deskId;
    pkg.signerKeyId = signerKeyId;
    pkg.exportedAt = exportedAt;
    pkg.sinceWatermark = sinceWatermark;
    pkg.status = SyncPackageStatus::Pending;
    pkg.packageFilePath = packageDir;

    auto insertResult = m_syncRepo.insertPackage(pkg);
    if (!insertResult.isOk())
        return Result<SyncPackage>::err(insertResult.errorCode(), insertResult.errorMessage());

    for (auto it = entities.constBegin(); it != entities.constEnd(); ++it) {
        const QJsonObject entityObject = it.value().toObject();

        SyncPackageEntity entity;
        entity.packageId = packageId;
        entity.entityType = it.key();
        entity.filePath = entityObject[QStringLiteral("file")].toString();
        entity.sha256Hex = entityObject[QStringLiteral("sha256")].toString();
        entity.recordCount = entityObject[QStringLiteral("record_count")].toInt();
        entity.verified = false;
        entity.applied = false;

        auto entityInsertResult = m_syncRepo.insertPackageEntity(entity);
        if (!entityInsertResult.isOk())
            return Result<SyncPackage>::err(entityInsertResult.errorCode(), entityInsertResult.errorMessage());
    }

    m_auditService.recordEvent(actorUserId,
                               AuditEventType::SyncExport,
                               QStringLiteral("SyncPackage"), packageId,
                               {}, {
                                   {QStringLiteral("dest"), destinationDir},
                                   {QStringLiteral("desk"), deskId},
                                   {QStringLiteral("since_watermark"), sinceWatermark.toString(Qt::ISODateWithMs)}
                               });

    Logger::instance().info(QStringLiteral("SyncService"),
                            QStringLiteral("Sync package exported"),
                            {
                                {QStringLiteral("package_id"), packageId},
                                {QStringLiteral("dest"), destinationDir},
                                {QStringLiteral("deduction_count"), deductionsResult.value().recordCount},
                                {QStringLiteral("correction_count"), correctionsResult.value().recordCount}
                            });

    return insertResult;
}

Result<SyncPackage> SyncService::importPackage(const QString& packageDir,
                                               const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::SecurityAdministrator);
    if (authResult.isErr())
        return Result<SyncPackage>::err(authResult.errorCode(), authResult.errorMessage());

    QFile manifestFile(packageDir + QStringLiteral("/manifest.json"));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        return Result<SyncPackage>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("Cannot open manifest.json in: %1").arg(packageDir));
    }

    const QByteArray manifestBytes = manifestFile.readAll();
    const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestBytes);
    if (!manifestDoc.isObject()) {
        return Result<SyncPackage>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("manifest.json is not valid JSON"));
    }

    const QJsonObject manifest = manifestDoc.object();
    const QString packageId = manifest[QStringLiteral("package_id")].toString();
    const QString signerKeyId = manifest[QStringLiteral("signer_key_id")].toString();
    const QString signatureHex = manifest[QStringLiteral("signature")].toString();
    if (packageId.isEmpty() || signerKeyId.isEmpty() || signatureHex.isEmpty()) {
        return Result<SyncPackage>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("manifest.json missing required fields"));
    }

    QJsonObject manifestBody = manifest;
    manifestBody.remove(QStringLiteral("signature"));
    const QByteArray manifestBodyBytes =
        QJsonDocument(manifestBody).toJson(QJsonDocument::Compact);

    const QByteArray signatureBytes = QByteArray::fromHex(signatureHex.toLatin1());
    auto signatureResult = m_verifier.verifyPackageSignature(manifestBodyBytes, signatureBytes, signerKeyId);
    if (!signatureResult.isOk())
        return Result<SyncPackage>::err(signatureResult.errorCode(), signatureResult.errorMessage());
    if (!signatureResult.value()) {
        return Result<SyncPackage>::err(
            ErrorCode::SignatureInvalid,
            QStringLiteral("Sync package signature verification failed"));
    }

    const QJsonObject entities = manifest[QStringLiteral("entities")].toObject();
    QList<SyncPackageEntity> entityList;
    for (auto it = entities.constBegin(); it != entities.constEnd(); ++it) {
        const QJsonObject entityObject = it.value().toObject();
        const QString relativePath = entityObject[QStringLiteral("file")].toString();
        auto resolvedPath = resolvePackagePath(packageDir, relativePath);
        if (resolvedPath.isErr())
            return Result<SyncPackage>::err(resolvedPath.errorCode(), resolvedPath.errorMessage());

        const QString expectedDigest = entityObject[QStringLiteral("sha256")].toString();
        const QString actualDigest = sha256OfFile(resolvedPath.value());
        if (actualDigest.isEmpty()) {
            return Result<SyncPackage>::err(
                ErrorCode::PackageCorrupt,
                QStringLiteral("Entity file missing: %1").arg(relativePath));
        }
        if (actualDigest != expectedDigest) {
            return Result<SyncPackage>::err(
                ErrorCode::PackageCorrupt,
                QStringLiteral("Digest mismatch for: %1").arg(it.key()));
        }

        SyncPackageEntity entity;
        entity.packageId = packageId;
        entity.entityType = it.key();
        entity.filePath = relativePath;
        entity.sha256Hex = expectedDigest;
        entity.recordCount = entityObject[QStringLiteral("record_count")].toInt();
        entity.verified = true;
        entity.applied = false;
        entityList.append(std::move(entity));
    }

    SyncPackage pkg;
    pkg.id = packageId;
    pkg.sourceDeskId = manifest[QStringLiteral("source_desk_id")].toString();
    pkg.signerKeyId = signerKeyId;
    pkg.exportedAt = QDateTime::fromString(
        manifest[QStringLiteral("exported_at")].toString(), Qt::ISODateWithMs);
    pkg.sinceWatermark = QDateTime::fromString(
        manifest[QStringLiteral("since_watermark")].toString(), Qt::ISODateWithMs);
    pkg.status = SyncPackageStatus::Verified;
    pkg.packageFilePath = packageDir;
    pkg.importedAt = QDateTime::currentDateTimeUtc();
    pkg.importedByUserId = actorUserId;

    auto insertResult = m_syncRepo.insertPackage(pkg);
    if (!insertResult.isOk())
        return Result<SyncPackage>::err(insertResult.errorCode(), insertResult.errorMessage());

    for (const SyncPackageEntity& entity : entityList) {
        auto entityInsertResult = m_syncRepo.insertPackageEntity(entity);
        if (!entityInsertResult.isOk())
            return Result<SyncPackage>::err(entityInsertResult.errorCode(), entityInsertResult.errorMessage());
    }

    int conflictCount = 0;
    for (const SyncPackageEntity& entity : entityList) {
        auto resolvedPath = resolvePackagePath(packageDir, entity.filePath);
        if (resolvedPath.isErr())
            return Result<SyncPackage>::err(resolvedPath.errorCode(), resolvedPath.errorMessage());

        auto recordsResult = readJsonLinesFile(resolvedPath.value());
        if (recordsResult.isErr())
            return Result<SyncPackage>::err(recordsResult.errorCode(), recordsResult.errorMessage());

        bool entityApplied = true;

        if (entity.entityType == QLatin1String("deductions")) {
            auto conflictResult = detectAndRecordConflicts(packageId,
                                                           QStringLiteral("DeductionEvent"),
                                                           recordsResult.value());
            if (conflictResult.isErr())
                return Result<SyncPackage>::err(conflictResult.errorCode(), conflictResult.errorMessage());

            conflictCount += conflictResult.value().conflictCount;
            if (conflictResult.value().conflictCount > 0)
                entityApplied = false;

            for (const QJsonObject& record : recordsResult.value()) {
                const QString recordId = record[QStringLiteral("id")].toString();
                if (conflictResult.value().conflictedEntityIds.contains(recordId))
                    continue;

                auto applyResult = m_checkInRepo.applyIncomingDeduction(record, actorUserId);
                if (applyResult.isErr()) {
                    ++conflictCount;
                    entityApplied = false;

                    ConflictRecord conflict;
                    conflict.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    conflict.packageId = packageId;
                    conflict.type = ConflictType::MutableRecordConflict;
                    conflict.entityType = QStringLiteral("DeductionEvent");
                    conflict.entityId = recordId.isEmpty() ? conflict.id : recordId;
                    conflict.description = QStringLiteral("Incoming deduction apply failed: %1")
                                               .arg(applyResult.errorMessage());
                    conflict.status = ConflictStatus::Pending;
                    conflict.incomingPayloadJson = QString::fromUtf8(
                        QJsonDocument(record).toJson(QJsonDocument::Compact));
                    conflict.localPayloadJson = QStringLiteral("{}");
                    conflict.detectedAt = QDateTime::currentDateTimeUtc();

                    auto insertResult = m_syncRepo.insertConflict(conflict);
                    if (!insertResult.isOk()) {
                        return Result<SyncPackage>::err(insertResult.errorCode(),
                                                        insertResult.errorMessage());
                    }
                }
            }
        } else if (entity.entityType == QLatin1String("corrections")) {
            for (const QJsonObject& record : recordsResult.value()) {
                auto applyResult = m_checkInRepo.applyIncomingCorrection(record, actorUserId);
                if (applyResult.isErr()) {
                    ++conflictCount;
                    entityApplied = false;

                    ConflictRecord conflict;
                    conflict.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    conflict.packageId = packageId;
                    conflict.type = ConflictType::MutableRecordConflict;
                    conflict.entityType = QStringLiteral("CorrectionRequest");
                    conflict.entityId = record[QStringLiteral("id")].toString();
                    if (conflict.entityId.isEmpty())
                        conflict.entityId = conflict.id;
                    conflict.description = QStringLiteral("Incoming correction apply failed: %1")
                                               .arg(applyResult.errorMessage());
                    conflict.status = ConflictStatus::Pending;
                    conflict.incomingPayloadJson = QString::fromUtf8(
                        QJsonDocument(record).toJson(QJsonDocument::Compact));
                    conflict.localPayloadJson = QStringLiteral("{}");
                    conflict.detectedAt = QDateTime::currentDateTimeUtc();

                    auto insertResult = m_syncRepo.insertConflict(conflict);
                    if (!insertResult.isOk()) {
                        return Result<SyncPackage>::err(insertResult.errorCode(),
                                                        insertResult.errorMessage());
                    }
                }
            }
        } else {
            // Unknown entity type remains un-applied for explicit operator review.
            entityApplied = false;
        }

        if (entityApplied) {
            auto markApplied = m_syncRepo.markEntityApplied(packageId, entity.entityType);
            if (!markApplied.isOk())
                return Result<SyncPackage>::err(markApplied.errorCode(), markApplied.errorMessage());
        }
    }

    const SyncPackageStatus finalStatus =
        conflictCount > 0 ? SyncPackageStatus::Partial : SyncPackageStatus::Applied;
    auto statusResult = m_syncRepo.updatePackageStatus(packageId, finalStatus);
    if (!statusResult.isOk())
        return Result<SyncPackage>::err(statusResult.errorCode(), statusResult.errorMessage());
    pkg.status = finalStatus;

    m_auditService.recordEvent(actorUserId,
                               AuditEventType::SyncImport,
                               QStringLiteral("SyncPackage"), packageId,
                               {}, {
                                   {QStringLiteral("source_desk"), pkg.sourceDeskId},
                                   {QStringLiteral("conflict_count"), conflictCount}
                               });

    Logger::instance().info(QStringLiteral("SyncService"),
                            QStringLiteral("Sync package imported"),
                            {
                                {QStringLiteral("package_id"), packageId},
                                {QStringLiteral("status"),
                                 finalStatus == SyncPackageStatus::Partial
                                     ? QStringLiteral("Partial")
                                     : QStringLiteral("Applied")},
                                {QStringLiteral("conflict_count"), conflictCount}
                            });

    return Result<SyncPackage>::ok(pkg);
}

Result<void> SyncService::resolveConflict(const QString& conflictId,
                                          ConflictStatus resolution,
                                          const QString& actorUserId,
                                          const QString& stepUpWindowId)
{
    auto authResult = m_authService.authorizePrivilegedAction(
        actorUserId,
        Role::SecurityAdministrator,
        stepUpWindowId);
    if (authResult.isErr())
        return authResult;

    auto conflictResult = m_syncRepo.getConflict(conflictId);
    if (!conflictResult.isOk())
        return Result<void>::err(conflictResult.errorCode(), conflictResult.errorMessage());

    QString resolutionActionType;
    QString resolutionActionId;

    const ConflictRecord& conflict = conflictResult.value();
    if (conflict.type == ConflictType::DoubleDeduction
        && (resolution == ConflictStatus::ResolvedAcceptIncoming
            || resolution == ConflictStatus::ResolvedManualMerge)) {
        const QJsonDocument localDoc = QJsonDocument::fromJson(conflict.localPayloadJson.toUtf8());
        const QJsonDocument incomingDoc = QJsonDocument::fromJson(conflict.incomingPayloadJson.toUtf8());
        if (!localDoc.isObject() || !incomingDoc.isObject()) {
            return Result<void>::err(
                ErrorCode::ValidationFailed,
                QStringLiteral("Conflict payload cannot be parsed for compensating correction"));
        }

        const QString localDeductionId = localDoc.object()[QStringLiteral("id")].toString();
        if (localDeductionId.isEmpty()) {
            return Result<void>::err(
                ErrorCode::ValidationFailed,
                QStringLiteral("Conflict local payload is missing deduction id"));
        }

        auto correctionResult = m_checkInRepo.createCompensatingCorrection(
            localDeductionId,
            actorUserId,
            QStringLiteral("Sync conflict resolution accepted incoming deduction"));
        if (correctionResult.isErr())
            return Result<void>::err(correctionResult.errorCode(), correctionResult.errorMessage());

        auto applyIncomingResult = m_checkInRepo.applyIncomingDeduction(incomingDoc.object(), actorUserId);
        if (applyIncomingResult.isErr()) {
            return Result<void>::err(applyIncomingResult.errorCode(),
                                     applyIncomingResult.errorMessage());
        }

        resolutionActionType = applyIncomingResult.value()
            ? QStringLiteral("CorrectionAndIncomingApplied")
            : QStringLiteral("CorrectionApplied");
        resolutionActionId = correctionResult.value();

        const QString incomingDeductionId = incomingDoc.object()[QStringLiteral("id")].toString();
        if (!incomingDeductionId.isEmpty())
            resolutionActionId += QStringLiteral("|") + incomingDeductionId;
    } else if (resolution == ConflictStatus::ResolvedAcceptLocal || resolution == ConflictStatus::Skipped) {
        resolutionActionType = QStringLiteral("LocalKept");
    } else {
        resolutionActionType = QStringLiteral("StatusOnly");
    }

    auto result = m_syncRepo.resolveConflict(conflictId,
                                             resolution,
                                             actorUserId,
                                             resolutionActionType,
                                             resolutionActionId);
    if (!result.isOk())
        return result;

    m_auditService.recordEvent(actorUserId,
                               AuditEventType::SyncConflictResolved,
                               QStringLiteral("ConflictRecord"), conflictId,
                               {}, {
                                   {QStringLiteral("resolution"), QString::number(static_cast<int>(resolution))}
                               });

    return Result<void>::ok();
}

Result<QList<SyncPackage>> SyncService::listPackages(const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::SecurityAdministrator);
    if (authResult.isErr())
        return Result<QList<SyncPackage>>::err(authResult.errorCode(), authResult.errorMessage());

    return m_syncRepo.listPackages();
}

Result<QList<ConflictRecord>> SyncService::listPendingConflicts(const QString& packageId,
                                                                const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::SecurityAdministrator);
    if (authResult.isErr())
        return Result<QList<ConflictRecord>>::err(authResult.errorCode(), authResult.errorMessage());

    return m_syncRepo.listPendingConflicts(packageId);
}

Result<TrustedSigningKey> SyncService::importSigningKey(const QString& label,
                                                        const QString& publicKeyDerHex,
                                                        const QDateTime& expiresAt,
                                                        const QString& actorUserId,
                                                        const QString& stepUpWindowId)
{
    auto authResult = m_authService.authorizePrivilegedAction(
        actorUserId,
        Role::SecurityAdministrator,
        stepUpWindowId);
    if (authResult.isErr())
        return Result<TrustedSigningKey>::err(authResult.errorCode(), authResult.errorMessage());

    const QByteArray pubDer = QByteArray::fromHex(publicKeyDerHex.toLatin1());
    const QString fingerprint = sha256OfBytes(pubDer);

    TrustedSigningKey key;
    key.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    key.label = label;
    key.publicKeyDerHex = publicKeyDerHex;
    key.fingerprint = fingerprint;
    key.importedAt = QDateTime::currentDateTimeUtc();
    key.importedByUserId = actorUserId;
    key.expiresAt = expiresAt;
    key.revoked = false;

    auto result = m_syncRepo.insertSigningKey(key);
    if (!result.isOk())
        return Result<TrustedSigningKey>::err(result.errorCode(), result.errorMessage());

    m_auditService.recordEvent(actorUserId,
                               AuditEventType::KeyImported,
                               QStringLiteral("TrustedSigningKey"), key.id,
                               {}, {
                                   {QStringLiteral("label"), label},
                                   {QStringLiteral("fingerprint"), fingerprint}
                               });

    return result;
}

Result<void> SyncService::revokeSigningKey(const QString& keyId,
                                           const QString& actorUserId,
                                           const QString& stepUpWindowId)
{
    auto authResult = m_authService.authorizePrivilegedAction(
        actorUserId,
        Role::SecurityAdministrator,
        stepUpWindowId);
    if (authResult.isErr())
        return authResult;

    auto result = m_syncRepo.revokeSigningKey(keyId);
    if (!result.isOk())
        return result;

    m_auditService.recordEvent(actorUserId,
                               AuditEventType::KeyRevoked,
                               QStringLiteral("TrustedSigningKey"), keyId,
                               {}, {});

    return Result<void>::ok();
}

Result<QList<TrustedSigningKey>> SyncService::listSigningKeys(const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::SecurityAdministrator);
    if (authResult.isErr())
        return Result<QList<TrustedSigningKey>>::err(authResult.errorCode(), authResult.errorMessage());

    return m_syncRepo.listSigningKeys();
}

Result<SyncService::WrittenEntityFile> SyncService::writeDeductionsDelta(const QString& dir,
                                                                         const QDateTime& sinceWatermark)
{
    const QString filePath = dir + QStringLiteral("/deductions.jsonl");
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return Result<WrittenEntityFile>::err(
            ErrorCode::IoError,
            QStringLiteral("Cannot write deductions.jsonl"));
    }

    auto recordsResult = m_checkInRepo.listDeductionDelta(sinceWatermark);
    if (!recordsResult.isOk())
        return Result<WrittenEntityFile>::err(recordsResult.errorCode(), recordsResult.errorMessage());

    for (const QJsonObject& record : recordsResult.value()) {
        file.write(QJsonDocument(record).toJson(QJsonDocument::Compact));
        file.write("\n");
    }
    file.close();

    WrittenEntityFile written;
    written.sha256Hex = sha256OfFile(filePath);
    written.recordCount = recordsResult.value().size();
    return Result<WrittenEntityFile>::ok(written);
}

Result<SyncService::WrittenEntityFile> SyncService::writeCorrectionsDelta(const QString& dir,
                                                                          const QDateTime& sinceWatermark)
{
    const QString filePath = dir + QStringLiteral("/corrections.jsonl");
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return Result<WrittenEntityFile>::err(
            ErrorCode::IoError,
            QStringLiteral("Cannot write corrections.jsonl"));
    }

    auto recordsResult = m_checkInRepo.listCorrectionDelta(sinceWatermark);
    if (!recordsResult.isOk())
        return Result<WrittenEntityFile>::err(recordsResult.errorCode(), recordsResult.errorMessage());

    for (const QJsonObject& record : recordsResult.value()) {
        file.write(QJsonDocument(record).toJson(QJsonDocument::Compact));
        file.write("\n");
    }
    file.close();

    WrittenEntityFile written;
    written.sha256Hex = sha256OfFile(filePath);
    written.recordCount = recordsResult.value().size();
    return Result<WrittenEntityFile>::ok(written);
}

Result<QString> SyncService::resolvePackagePath(const QString& packageRoot,
                                                const QString& relativePath) const
{
    const QString cleanedRelative = normalizedPath(relativePath);
    if (cleanedRelative.isEmpty()) {
        return Result<QString>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("Package entity path is empty"));
    }
    if (QFileInfo(cleanedRelative).isAbsolute()
        || cleanedRelative == QStringLiteral("..")
        || cleanedRelative.startsWith(QStringLiteral("../"))
        || cleanedRelative.contains(QStringLiteral("/../"))) {
        return Result<QString>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("Package entity path escapes package root"));
    }

    const QString rootCanonical = normalizedPath(QFileInfo(packageRoot).canonicalFilePath());
    const QString candidatePath = QDir(packageRoot).absoluteFilePath(cleanedRelative);
    const QString candidateCanonical = normalizedPath(QFileInfo(candidatePath).canonicalFilePath());
    if (rootCanonical.isEmpty() || candidateCanonical.isEmpty()) {
        return Result<QString>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("Package entity file is missing"));
    }
    if (!candidateCanonical.startsWith(rootCanonical + QStringLiteral("/"))
        && candidateCanonical != rootCanonical) {
        return Result<QString>::err(
            ErrorCode::PackageCorrupt,
            QStringLiteral("Package entity path escapes package root"));
    }

    return Result<QString>::ok(candidateCanonical);
}

Result<SyncService::ConflictDetectionOutcome> SyncService::detectAndRecordConflicts(
    const QString& packageId,
    const QString& entityType,
    const QList<QJsonObject>& incomingRecords)
{
    ConflictDetectionOutcome outcome;
    for (const QJsonObject& record : incomingRecords) {
        const QString memberId = record[QStringLiteral("member_id")].toString();
        const QString sessionId = record[QStringLiteral("session_id")].toString();
        if (memberId.isEmpty() || sessionId.isEmpty())
            continue;

        auto localResult = m_checkInRepo.findLocalDeductionConflict(memberId, sessionId);
        if (!localResult.isOk())
            return Result<ConflictDetectionOutcome>::err(localResult.errorCode(), localResult.errorMessage());
        if (!localResult.value().has_value())
            continue;

        ConflictRecord conflict;
        conflict.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        conflict.packageId = packageId;
        conflict.type = ConflictType::DoubleDeduction;
        conflict.entityType = entityType;
        conflict.entityId = record[QStringLiteral("id")].toString();
        conflict.description = QStringLiteral(
            "Incoming deduction collides with local deduction for member %1 in session %2")
                                   .arg(memberId, sessionId);
        conflict.status = ConflictStatus::Pending;
        conflict.incomingPayloadJson = QString::fromUtf8(
            QJsonDocument(record).toJson(QJsonDocument::Compact));
        conflict.localPayloadJson = QString::fromUtf8(
            QJsonDocument(localResult.value().value()).toJson(QJsonDocument::Compact));
        conflict.detectedAt = QDateTime::currentDateTimeUtc();

        auto insertResult = m_syncRepo.insertConflict(conflict);
        if (!insertResult.isOk())
            return Result<ConflictDetectionOutcome>::err(insertResult.errorCode(), insertResult.errorMessage());

        ++outcome.conflictCount;
        const QString incomingId = record[QStringLiteral("id")].toString();
        if (!incomingId.isEmpty())
            outcome.conflictedEntityIds.insert(incomingId);
    }

    return Result<ConflictDetectionOutcome>::ok(std::move(outcome));
}
