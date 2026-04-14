// UpdateRepository.cpp — ProctorOps

#include "UpdateRepository.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDateTime>

// ── Enum helpers ───────────────────────────────────────────────────────────────

QString UpdateRepository::packageStatusToString(UpdatePackageStatus s)
{
    switch (s) {
        case UpdatePackageStatus::Staged:     return QStringLiteral("Staged");
        case UpdatePackageStatus::Applied:    return QStringLiteral("Applied");
        case UpdatePackageStatus::RolledBack: return QStringLiteral("RolledBack");
        case UpdatePackageStatus::Rejected:   return QStringLiteral("Rejected");
        case UpdatePackageStatus::Cancelled:  return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Staged");
}

UpdatePackageStatus UpdateRepository::packageStatusFromString(const QString& s)
{
    if (s == QLatin1String("Applied"))    return UpdatePackageStatus::Applied;
    if (s == QLatin1String("RolledBack")) return UpdatePackageStatus::RolledBack;
    if (s == QLatin1String("Rejected"))   return UpdatePackageStatus::Rejected;
    if (s == QLatin1String("Cancelled"))  return UpdatePackageStatus::Cancelled;
    return UpdatePackageStatus::Staged;
}

// ── Row mappers ────────────────────────────────────────────────────────────────

UpdatePackage UpdateRepository::packageFromQuery(QSqlQuery& q)
{
    UpdatePackage p;
    p.id                 = q.value(0).toString();
    p.version            = q.value(1).toString();
    p.targetPlatform     = q.value(2).toString();
    p.description        = q.value(3).toString();
    p.signerKeyId        = q.value(4).toString();
    p.signatureValid     = q.value(5).toBool();
    p.stagedPath         = q.value(6).toString();
    p.status             = packageStatusFromString(q.value(7).toString());
    p.importedAt         = QDateTime::fromString(q.value(8).toString(), Qt::ISODateWithMs);
    p.importedByUserId   = q.value(9).toString();
    return p;
}

UpdateComponent UpdateRepository::componentFromQuery(QSqlQuery& q)
{
    UpdateComponent c;
    c.packageId           = q.value(0).toString();
    c.name                = q.value(1).toString();
    c.version             = q.value(2).toString();
    c.sha256Hex           = q.value(3).toString();
    c.componentFilePath   = q.value(4).toString();
    return c;
}

InstallHistoryEntry UpdateRepository::historyFromQuery(QSqlQuery& q)
{
    InstallHistoryEntry h;
    h.id                    = q.value(0).toString();
    h.packageId             = q.value(1).toString();
    h.fromVersion           = q.value(2).toString();
    h.toVersion             = q.value(3).toString();
    h.appliedAt             = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
    h.appliedByUserId       = q.value(5).toString();
    h.snapshotPayloadJson   = q.value(6).toString();
    return h;
}

RollbackRecord UpdateRepository::rollbackFromQuery(QSqlQuery& q)
{
    RollbackRecord r;
    r.id                   = q.value(0).toString();
    r.installHistoryId     = q.value(1).toString();
    r.fromVersion          = q.value(2).toString();
    r.toVersion            = q.value(3).toString();
    r.rationale            = q.value(4).toString();
    r.rolledBackAt         = QDateTime::fromString(q.value(5).toString(), Qt::ISODateWithMs);
    r.rolledBackByUserId   = q.value(6).toString();
    return r;
}

// ── Constructor ───────────────────────────────────────────────────────────────

UpdateRepository::UpdateRepository(QSqlDatabase& db) : m_db(db) {}

// ── Update packages ────────────────────────────────────────────────────────────

Result<UpdatePackage> UpdateRepository::insertPackage(const UpdatePackage& pkg)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO update_packages "
        "(id, version, target_platform, description, signer_key_id, "
        " signature_valid, staged_path, status, imported_at, imported_by_user_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(pkg.id);
    q.addBindValue(pkg.version);
    q.addBindValue(pkg.targetPlatform);
    q.addBindValue(pkg.description);
    q.addBindValue(pkg.signerKeyId);
    q.addBindValue(pkg.signatureValid ? 1 : 0);
    q.addBindValue(pkg.stagedPath.isEmpty() ? QVariant() : pkg.stagedPath);
    q.addBindValue(packageStatusToString(pkg.status));
    q.addBindValue(pkg.importedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(pkg.importedByUserId);

    if (!q.exec())
        return Result<UpdatePackage>::err(ErrorCode::DbError, q.lastError().text());

    return Result<UpdatePackage>::ok(pkg);
}

Result<UpdatePackage> UpdateRepository::getPackage(const QString& id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, version, target_platform, description, signer_key_id, "
        "       signature_valid, staged_path, status, imported_at, imported_by_user_id "
        "FROM update_packages WHERE id = ?"));
    q.addBindValue(id);

    if (!q.exec())
        return Result<UpdatePackage>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<UpdatePackage>::err(ErrorCode::NotFound);

    return Result<UpdatePackage>::ok(packageFromQuery(q));
}

Result<QList<UpdatePackage>> UpdateRepository::listPackages()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT id, version, target_platform, description, signer_key_id, "
            "       signature_valid, staged_path, status, imported_at, imported_by_user_id "
            "FROM update_packages ORDER BY imported_at DESC")))
        return Result<QList<UpdatePackage>>::err(ErrorCode::DbError, q.lastError().text());

    QList<UpdatePackage> result;
    while (q.next())
        result.append(packageFromQuery(q));
    return Result<QList<UpdatePackage>>::ok(std::move(result));
}

Result<void> UpdateRepository::updatePackageStatus(const QString& id, UpdatePackageStatus status)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE update_packages SET status = ? WHERE id = ?"));
    q.addBindValue(packageStatusToString(status));
    q.addBindValue(id);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<void> UpdateRepository::markSignatureValid(const QString& id, bool valid)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE update_packages SET signature_valid = ? WHERE id = ?"));
    q.addBindValue(valid ? 1 : 0);
    q.addBindValue(id);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

// ── Package components ─────────────────────────────────────────────────────────

Result<void> UpdateRepository::insertComponent(const UpdateComponent& c)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO update_components (package_id, name, version, sha256_hex, component_file_path) "
        "VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(c.packageId);
    q.addBindValue(c.name);
    q.addBindValue(c.version);
    q.addBindValue(c.sha256Hex);
    q.addBindValue(c.componentFilePath);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<QList<UpdateComponent>> UpdateRepository::getComponents(const QString& packageId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT package_id, name, version, sha256_hex, component_file_path "
        "FROM update_components WHERE package_id = ? ORDER BY name"));
    q.addBindValue(packageId);

    if (!q.exec())
        return Result<QList<UpdateComponent>>::err(ErrorCode::DbError, q.lastError().text());

    QList<UpdateComponent> result;
    while (q.next())
        result.append(componentFromQuery(q));
    return Result<QList<UpdateComponent>>::ok(std::move(result));
}

// ── Install history ────────────────────────────────────────────────────────────

Result<InstallHistoryEntry> UpdateRepository::insertInstallHistory(const InstallHistoryEntry& h)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO install_history "
        "(id, package_id, from_version, to_version, applied_at, applied_by_user_id, snapshot_payload_json) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(h.id);
    q.addBindValue(h.packageId);
    q.addBindValue(h.fromVersion);
    q.addBindValue(h.toVersion);
    q.addBindValue(h.appliedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(h.appliedByUserId);
    q.addBindValue(h.snapshotPayloadJson);

    if (!q.exec())
        return Result<InstallHistoryEntry>::err(ErrorCode::DbError, q.lastError().text());

    return Result<InstallHistoryEntry>::ok(h);
}

Result<InstallHistoryEntry> UpdateRepository::getInstallHistory(const QString& id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, package_id, from_version, to_version, applied_at, "
        "       applied_by_user_id, snapshot_payload_json "
        "FROM install_history WHERE id = ?"));
    q.addBindValue(id);

    if (!q.exec())
        return Result<InstallHistoryEntry>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<InstallHistoryEntry>::err(ErrorCode::NotFound);

    return Result<InstallHistoryEntry>::ok(historyFromQuery(q));
}

Result<QList<InstallHistoryEntry>> UpdateRepository::listInstallHistory()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT id, package_id, from_version, to_version, applied_at, "
            "       applied_by_user_id, snapshot_payload_json "
            "FROM install_history ORDER BY applied_at DESC")))
        return Result<QList<InstallHistoryEntry>>::err(ErrorCode::DbError, q.lastError().text());

    QList<InstallHistoryEntry> result;
    while (q.next())
        result.append(historyFromQuery(q));
    return Result<QList<InstallHistoryEntry>>::ok(std::move(result));
}

Result<std::optional<InstallHistoryEntry>> UpdateRepository::latestInstallHistory()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT id, package_id, from_version, to_version, applied_at, "
            "       applied_by_user_id, snapshot_payload_json "
            "FROM install_history ORDER BY applied_at DESC LIMIT 1")))
        return Result<std::optional<InstallHistoryEntry>>::err(ErrorCode::DbError, q.lastError().text());

    if (!q.next())
        return Result<std::optional<InstallHistoryEntry>>::ok(std::nullopt);

    return Result<std::optional<InstallHistoryEntry>>::ok(historyFromQuery(q));
}

// ── Rollback records ───────────────────────────────────────────────────────────

Result<RollbackRecord> UpdateRepository::insertRollbackRecord(const RollbackRecord& rec)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO rollback_records "
        "(id, install_history_id, from_version, to_version, rationale, rolled_back_at, rolled_back_by_user_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(rec.id);
    q.addBindValue(rec.installHistoryId);
    q.addBindValue(rec.fromVersion);
    q.addBindValue(rec.toVersion);
    q.addBindValue(rec.rationale);
    q.addBindValue(rec.rolledBackAt.toString(Qt::ISODateWithMs));
    q.addBindValue(rec.rolledBackByUserId);

    if (!q.exec())
        return Result<RollbackRecord>::err(ErrorCode::DbError, q.lastError().text());

    return Result<RollbackRecord>::ok(rec);
}

Result<QList<RollbackRecord>> UpdateRepository::listRollbackRecords()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT id, install_history_id, from_version, to_version, rationale, "
            "       rolled_back_at, rolled_back_by_user_id "
            "FROM rollback_records ORDER BY rolled_back_at DESC")))
        return Result<QList<RollbackRecord>>::err(ErrorCode::DbError, q.lastError().text());

    QList<RollbackRecord> result;
    while (q.next())
        result.append(rollbackFromQuery(q));
    return Result<QList<RollbackRecord>>::ok(std::move(result));
}
