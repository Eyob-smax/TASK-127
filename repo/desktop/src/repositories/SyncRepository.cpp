// SyncRepository.cpp — ProctorOps
// Concrete SQLite implementation for sync packages, conflicts, and trusted keys.

#include "SyncRepository.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>

SyncRepository::SyncRepository(QSqlDatabase& db)
    : m_db(db)
{
}

// ── Status/type string conversions ─────────────────────────────────────────

QString SyncRepository::packageStatusToString(SyncPackageStatus s)
{
    switch (s) {
    case SyncPackageStatus::Pending:  return QStringLiteral("Pending");
    case SyncPackageStatus::Verified: return QStringLiteral("Verified");
    case SyncPackageStatus::Applied:  return QStringLiteral("Applied");
    case SyncPackageStatus::Partial:  return QStringLiteral("Partial");
    case SyncPackageStatus::Rejected: return QStringLiteral("Rejected");
    }
    return QStringLiteral("Pending");
}

SyncPackageStatus SyncRepository::packageStatusFromString(const QString& s)
{
    if (s == QStringLiteral("Verified")) return SyncPackageStatus::Verified;
    if (s == QStringLiteral("Applied"))  return SyncPackageStatus::Applied;
    if (s == QStringLiteral("Partial"))  return SyncPackageStatus::Partial;
    if (s == QStringLiteral("Rejected")) return SyncPackageStatus::Rejected;
    return SyncPackageStatus::Pending;
}

QString SyncRepository::conflictTypeToString(ConflictType t)
{
    switch (t) {
    case ConflictType::DoubleDeduction:       return QStringLiteral("DoubleDeduction");
    case ConflictType::MutableRecordConflict: return QStringLiteral("MutableRecordConflict");
    case ConflictType::DeleteConflict:        return QStringLiteral("DeleteConflict");
    }
    return QStringLiteral("MutableRecordConflict");
}

ConflictType SyncRepository::conflictTypeFromString(const QString& s)
{
    if (s == QStringLiteral("DoubleDeduction")) return ConflictType::DoubleDeduction;
    if (s == QStringLiteral("DeleteConflict"))  return ConflictType::DeleteConflict;
    return ConflictType::MutableRecordConflict;
}

QString SyncRepository::conflictStatusToString(ConflictStatus s)
{
    switch (s) {
    case ConflictStatus::Pending:                return QStringLiteral("Pending");
    case ConflictStatus::ResolvedAcceptLocal:    return QStringLiteral("ResolvedAcceptLocal");
    case ConflictStatus::ResolvedAcceptIncoming: return QStringLiteral("ResolvedAcceptIncoming");
    case ConflictStatus::ResolvedManualMerge:    return QStringLiteral("ResolvedManualMerge");
    case ConflictStatus::Skipped:                return QStringLiteral("Skipped");
    }
    return QStringLiteral("Pending");
}

ConflictStatus SyncRepository::conflictStatusFromString(const QString& s)
{
    if (s == QStringLiteral("ResolvedAcceptLocal"))    return ConflictStatus::ResolvedAcceptLocal;
    if (s == QStringLiteral("ResolvedAcceptIncoming")) return ConflictStatus::ResolvedAcceptIncoming;
    if (s == QStringLiteral("ResolvedManualMerge"))    return ConflictStatus::ResolvedManualMerge;
    if (s == QStringLiteral("Skipped"))                return ConflictStatus::Skipped;
    return ConflictStatus::Pending;
}

// ── Sync packages ──────────────────────────────────────────────────────────

Result<SyncPackage> SyncRepository::insertPackage(const SyncPackage& pkg)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO sync_packages "
        "(id, source_desk_id, signer_key_id, exported_at, since_watermark, "
        " status, package_file_path, imported_at, imported_by_user_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(pkg.id);
    q.addBindValue(pkg.sourceDeskId);
    q.addBindValue(pkg.signerKeyId);
    q.addBindValue(pkg.exportedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(pkg.sinceWatermark.toString(Qt::ISODateWithMs));
    q.addBindValue(packageStatusToString(pkg.status));
    q.addBindValue(pkg.packageFilePath);
    q.addBindValue(pkg.importedAt.isNull() ? QVariant() : pkg.importedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(pkg.importedByUserId.isEmpty() ? QVariant() : pkg.importedByUserId);

    if (!q.exec())
        return Result<SyncPackage>::err(ErrorCode::DbError, q.lastError().text());

    return Result<SyncPackage>::ok(pkg);
}

Result<SyncPackage> SyncRepository::getPackage(const QString& id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, source_desk_id, signer_key_id, exported_at, since_watermark, "
        "       status, package_file_path, imported_at, imported_by_user_id "
        "FROM sync_packages WHERE id = ?"));
    q.addBindValue(id);

    if (!q.exec())
        return Result<SyncPackage>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<SyncPackage>::err(ErrorCode::NotFound);

    SyncPackage pkg;
    pkg.id               = q.value(0).toString();
    pkg.sourceDeskId     = q.value(1).toString();
    pkg.signerKeyId      = q.value(2).toString();
    pkg.exportedAt       = QDateTime::fromString(q.value(3).toString(), Qt::ISODateWithMs);
    pkg.sinceWatermark   = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
    pkg.status           = packageStatusFromString(q.value(5).toString());
    pkg.packageFilePath  = q.value(6).toString();
    pkg.importedAt       = q.value(7).isNull() ? QDateTime()
                             : QDateTime::fromString(q.value(7).toString(), Qt::ISODateWithMs);
    pkg.importedByUserId = q.value(8).toString();

    return Result<SyncPackage>::ok(std::move(pkg));
}

Result<QList<SyncPackage>> SyncRepository::listPackages()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT id, source_desk_id, signer_key_id, exported_at, since_watermark, "
            "       status, package_file_path, imported_at, imported_by_user_id "
            "FROM sync_packages "
            "ORDER BY exported_at DESC, imported_at DESC")))
        return Result<QList<SyncPackage>>::err(ErrorCode::DbError, q.lastError().text());

    QList<SyncPackage> packages;
    while (q.next()) {
        SyncPackage pkg;
        pkg.id               = q.value(0).toString();
        pkg.sourceDeskId     = q.value(1).toString();
        pkg.signerKeyId      = q.value(2).toString();
        pkg.exportedAt       = QDateTime::fromString(q.value(3).toString(), Qt::ISODateWithMs);
        pkg.sinceWatermark   = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
        pkg.status           = packageStatusFromString(q.value(5).toString());
        pkg.packageFilePath  = q.value(6).toString();
        pkg.importedAt       = q.value(7).isNull() ? QDateTime()
                                 : QDateTime::fromString(q.value(7).toString(), Qt::ISODateWithMs);
        pkg.importedByUserId = q.value(8).toString();
        packages.append(std::move(pkg));
    }

    return Result<QList<SyncPackage>>::ok(std::move(packages));
}

Result<std::optional<SyncPackage>> SyncRepository::latestExportForDesk(const QString& deskId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, source_desk_id, signer_key_id, exported_at, since_watermark, "
        "       status, package_file_path, imported_at, imported_by_user_id "
        "FROM sync_packages WHERE source_desk_id = ? "
        "ORDER BY exported_at DESC LIMIT 1"));
    q.addBindValue(deskId);

    if (!q.exec())
        return Result<std::optional<SyncPackage>>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<std::optional<SyncPackage>>::ok(std::nullopt);

    SyncPackage pkg;
    pkg.id               = q.value(0).toString();
    pkg.sourceDeskId     = q.value(1).toString();
    pkg.signerKeyId      = q.value(2).toString();
    pkg.exportedAt       = QDateTime::fromString(q.value(3).toString(), Qt::ISODateWithMs);
    pkg.sinceWatermark   = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
    pkg.status           = packageStatusFromString(q.value(5).toString());
    pkg.packageFilePath  = q.value(6).toString();
    pkg.importedAt       = q.value(7).isNull() ? QDateTime()
                             : QDateTime::fromString(q.value(7).toString(), Qt::ISODateWithMs);
    pkg.importedByUserId = q.value(8).toString();

    return Result<std::optional<SyncPackage>>::ok(pkg);
}

Result<void> SyncRepository::updatePackageStatus(const QString& id, SyncPackageStatus status)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE sync_packages SET status = ? WHERE id = ?"));
    q.addBindValue(packageStatusToString(status));
    q.addBindValue(id);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

// ── Package entities ───────────────────────────────────────────────────────

Result<void> SyncRepository::insertPackageEntity(const SyncPackageEntity& entity)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO sync_package_entities "
        "(package_id, entity_type, file_path, sha256_hex, record_count, verified, applied, applied_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(entity.packageId);
    q.addBindValue(entity.entityType);
    q.addBindValue(entity.filePath);
    q.addBindValue(entity.sha256Hex);
    q.addBindValue(entity.recordCount);
    q.addBindValue(entity.verified ? 1 : 0);
    q.addBindValue(entity.applied ? 1 : 0);
    q.addBindValue(entity.appliedAt.isNull() ? QVariant() : entity.appliedAt.toString(Qt::ISODateWithMs));

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<QList<SyncPackageEntity>> SyncRepository::getPackageEntities(const QString& packageId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT package_id, entity_type, file_path, sha256_hex, record_count, verified, applied, applied_at "
        "FROM sync_package_entities WHERE package_id = ?"));
    q.addBindValue(packageId);

    if (!q.exec())
        return Result<QList<SyncPackageEntity>>::err(ErrorCode::DbError, q.lastError().text());

    QList<SyncPackageEntity> entities;
    while (q.next()) {
        SyncPackageEntity e;
        e.packageId   = q.value(0).toString();
        e.entityType  = q.value(1).toString();
        e.filePath    = q.value(2).toString();
        e.sha256Hex   = q.value(3).toString();
        e.recordCount = q.value(4).toInt();
        e.verified    = q.value(5).toInt() != 0;
        e.applied     = q.value(6).toInt() != 0;
        e.appliedAt   = q.value(7).isNull() ? QDateTime()
                          : QDateTime::fromString(q.value(7).toString(), Qt::ISODateWithMs);
        entities.append(std::move(e));
    }
    return Result<QList<SyncPackageEntity>>::ok(std::move(entities));
}

Result<void> SyncRepository::markEntityVerified(const QString& packageId,
                                                  const QString& entityType)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE sync_package_entities SET verified = 1 "
        "WHERE package_id = ? AND entity_type = ?"));
    q.addBindValue(packageId);
    q.addBindValue(entityType);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<void> SyncRepository::markEntityApplied(const QString& packageId,
                                                const QString& entityType)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE sync_package_entities SET applied = 1, applied_at = ? "
        "WHERE package_id = ? AND entity_type = ?"));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    q.addBindValue(packageId);
    q.addBindValue(entityType);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

// ── Conflict records ───────────────────────────────────────────────────────

Result<ConflictRecord> SyncRepository::insertConflict(const ConflictRecord& c)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO conflict_records "
        "(id, package_id, type, entity_type, entity_id, description, status, "
        " incoming_payload_json, local_payload_json, detected_at, "
        " resolved_by_user_id, resolved_at, resolution_action_type, resolution_action_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(c.id);
    q.addBindValue(c.packageId);
    q.addBindValue(conflictTypeToString(c.type));
    q.addBindValue(c.entityType);
    q.addBindValue(c.entityId);
    q.addBindValue(c.description);
    q.addBindValue(conflictStatusToString(c.status));
    q.addBindValue(c.incomingPayloadJson);
    q.addBindValue(c.localPayloadJson);
    q.addBindValue(c.detectedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(c.resolvedByUserId.isEmpty() ? QVariant() : c.resolvedByUserId);
    q.addBindValue(c.resolvedAt.isNull() ? QVariant() : c.resolvedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(c.resolutionActionType.isEmpty() ? QVariant() : c.resolutionActionType);
    q.addBindValue(c.resolutionActionId.isEmpty() ? QVariant() : c.resolutionActionId);

    if (!q.exec())
        return Result<ConflictRecord>::err(ErrorCode::DbError, q.lastError().text());

    return Result<ConflictRecord>::ok(c);
}

Result<ConflictRecord> SyncRepository::getConflict(const QString& conflictId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, package_id, type, entity_type, entity_id, description, status, "
        "       incoming_payload_json, local_payload_json, detected_at, "
        "       resolved_by_user_id, resolved_at, resolution_action_type, resolution_action_id "
        "FROM conflict_records WHERE id = ?"));
    q.addBindValue(conflictId);

    if (!q.exec())
        return Result<ConflictRecord>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<ConflictRecord>::err(ErrorCode::NotFound);

    ConflictRecord c;
    c.id                  = q.value(0).toString();
    c.packageId           = q.value(1).toString();
    c.type                = conflictTypeFromString(q.value(2).toString());
    c.entityType          = q.value(3).toString();
    c.entityId            = q.value(4).toString();
    c.description         = q.value(5).toString();
    c.status              = conflictStatusFromString(q.value(6).toString());
    c.incomingPayloadJson = q.value(7).toString();
    c.localPayloadJson    = q.value(8).toString();
    c.detectedAt          = QDateTime::fromString(q.value(9).toString(), Qt::ISODateWithMs);
    c.resolvedByUserId    = q.value(10).toString();
    c.resolvedAt          = q.value(11).isNull() ? QDateTime()
                              : QDateTime::fromString(q.value(11).toString(), Qt::ISODateWithMs);
    c.resolutionActionType = q.value(12).toString();
    c.resolutionActionId   = q.value(13).toString();

    return Result<ConflictRecord>::ok(std::move(c));
}

Result<QList<ConflictRecord>> SyncRepository::listPendingConflicts(const QString& packageId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, package_id, type, entity_type, entity_id, description, status, "
        "       incoming_payload_json, local_payload_json, detected_at, "
        "       resolved_by_user_id, resolved_at, resolution_action_type, resolution_action_id "
        "FROM conflict_records WHERE package_id = ? AND status = 'Pending' "
        "ORDER BY detected_at"));
    q.addBindValue(packageId);

    if (!q.exec())
        return Result<QList<ConflictRecord>>::err(ErrorCode::DbError, q.lastError().text());

    QList<ConflictRecord> conflicts;
    while (q.next()) {
        ConflictRecord c;
        c.id                  = q.value(0).toString();
        c.packageId           = q.value(1).toString();
        c.type                = conflictTypeFromString(q.value(2).toString());
        c.entityType          = q.value(3).toString();
        c.entityId            = q.value(4).toString();
        c.description         = q.value(5).toString();
        c.status              = conflictStatusFromString(q.value(6).toString());
        c.incomingPayloadJson = q.value(7).toString();
        c.localPayloadJson    = q.value(8).toString();
        c.detectedAt          = QDateTime::fromString(q.value(9).toString(), Qt::ISODateWithMs);
        c.resolvedByUserId    = q.value(10).toString();
        c.resolvedAt          = q.value(11).isNull() ? QDateTime()
                                  : QDateTime::fromString(q.value(11).toString(), Qt::ISODateWithMs);
        c.resolutionActionType = q.value(12).toString();
        c.resolutionActionId   = q.value(13).toString();
        conflicts.append(std::move(c));
    }
    return Result<QList<ConflictRecord>>::ok(std::move(conflicts));
}

Result<void> SyncRepository::resolveConflict(const QString& conflictId,
                                               ConflictStatus status,
                                               const QString& resolvedByUserId,
                                               const QString& resolutionActionType,
                                               const QString& resolutionActionId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE conflict_records SET status = ?, resolved_by_user_id = ?, resolved_at = ?, "
        "resolution_action_type = ?, resolution_action_id = ? "
        "WHERE id = ?"));
    q.addBindValue(conflictStatusToString(status));
    q.addBindValue(resolvedByUserId);
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    q.addBindValue(resolutionActionType.isEmpty() ? QVariant() : resolutionActionType);
    q.addBindValue(resolutionActionId.isEmpty() ? QVariant() : resolutionActionId);
    q.addBindValue(conflictId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

// ── Trusted signing keys ───────────────────────────────────────────────────

Result<TrustedSigningKey> SyncRepository::insertSigningKey(const TrustedSigningKey& key)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO trusted_signing_keys "
        "(id, label, public_key_der_hex, fingerprint, imported_at, "
        " imported_by_user_id, expires_at, revoked) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(key.id);
    q.addBindValue(key.label);
    q.addBindValue(key.publicKeyDerHex);
    q.addBindValue(key.fingerprint);
    q.addBindValue(key.importedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(key.importedByUserId);
    q.addBindValue(key.expiresAt.isNull() ? QVariant() : key.expiresAt.toString(Qt::ISODateWithMs));
    q.addBindValue(key.revoked ? 1 : 0);

    if (!q.exec())
        return Result<TrustedSigningKey>::err(ErrorCode::DbError, q.lastError().text());

    return Result<TrustedSigningKey>::ok(key);
}

Result<TrustedSigningKey> SyncRepository::findSigningKeyById(const QString& keyId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, label, public_key_der_hex, fingerprint, imported_at, "
        "       imported_by_user_id, expires_at, revoked "
        "FROM trusted_signing_keys WHERE id = ?"));
    q.addBindValue(keyId);

    if (!q.exec())
        return Result<TrustedSigningKey>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<TrustedSigningKey>::err(ErrorCode::NotFound);

    TrustedSigningKey key;
    key.id               = q.value(0).toString();
    key.label            = q.value(1).toString();
    key.publicKeyDerHex  = q.value(2).toString();
    key.fingerprint      = q.value(3).toString();
    key.importedAt       = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
    key.importedByUserId = q.value(5).toString();
    key.expiresAt        = q.value(6).isNull() ? QDateTime()
                             : QDateTime::fromString(q.value(6).toString(), Qt::ISODateWithMs);
    key.revoked          = q.value(7).toInt() != 0;

    return Result<TrustedSigningKey>::ok(std::move(key));
}

Result<QList<TrustedSigningKey>> SyncRepository::listSigningKeys()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT id, label, public_key_der_hex, fingerprint, imported_at, "
            "       imported_by_user_id, expires_at, revoked "
            "FROM trusted_signing_keys ORDER BY imported_at")))
        return Result<QList<TrustedSigningKey>>::err(ErrorCode::DbError, q.lastError().text());

    QList<TrustedSigningKey> keys;
    while (q.next()) {
        TrustedSigningKey key;
        key.id               = q.value(0).toString();
        key.label            = q.value(1).toString();
        key.publicKeyDerHex  = q.value(2).toString();
        key.fingerprint      = q.value(3).toString();
        key.importedAt       = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
        key.importedByUserId = q.value(5).toString();
        key.expiresAt        = q.value(6).isNull() ? QDateTime()
                                 : QDateTime::fromString(q.value(6).toString(), Qt::ISODateWithMs);
        key.revoked          = q.value(7).toInt() != 0;
        keys.append(std::move(key));
    }
    return Result<QList<TrustedSigningKey>>::ok(std::move(keys));
}

Result<void> SyncRepository::revokeSigningKey(const QString& keyId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE trusted_signing_keys SET revoked = 1 WHERE id = ?"));
    q.addBindValue(keyId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<void>::err(ErrorCode::NotFound);

    return Result<void>::ok();
}
