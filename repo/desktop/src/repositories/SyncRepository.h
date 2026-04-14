#pragma once
// SyncRepository.h — ProctorOps
// Concrete SQLite implementation of ISyncRepository.
// Manages sync packages, entity manifests, conflict records, and trusted signing keys.

#include "ISyncRepository.h"
#include <QSqlDatabase>

class SyncRepository : public ISyncRepository {
public:
    explicit SyncRepository(QSqlDatabase& db);

    // ── Sync packages ──────────────────────────────────────────────────────
    Result<SyncPackage>          insertPackage(const SyncPackage& pkg) override;
    Result<SyncPackage>          getPackage(const QString& id) override;
    Result<QList<SyncPackage>>   listPackages() override;
    Result<std::optional<SyncPackage>> latestExportForDesk(const QString& deskId) override;
    Result<void>                 updatePackageStatus(const QString& id,
                                                      SyncPackageStatus status) override;

    // ── Package entities ───────────────────────────────────────────────────
    Result<void>                       insertPackageEntity(const SyncPackageEntity& entity) override;
    Result<QList<SyncPackageEntity>>   getPackageEntities(const QString& packageId) override;
    Result<void>                       markEntityVerified(const QString& packageId,
                                                           const QString& entityType) override;
    Result<void>                       markEntityApplied(const QString& packageId,
                                                          const QString& entityType) override;

    // ── Conflict records ───────────────────────────────────────────────────
    Result<ConflictRecord>          insertConflict(const ConflictRecord& c) override;
    Result<ConflictRecord>          getConflict(const QString& conflictId) override;
    Result<QList<ConflictRecord>>   listPendingConflicts(const QString& packageId) override;
    Result<void>                    resolveConflict(const QString& conflictId,
                                                     ConflictStatus status,
                                                     const QString& resolvedByUserId,
                                                     const QString& resolutionActionType,
                                                     const QString& resolutionActionId) override;

    // ── Trusted signing keys ───────────────────────────────────────────────
    Result<TrustedSigningKey>        insertSigningKey(const TrustedSigningKey& key) override;
    Result<TrustedSigningKey>        findSigningKeyById(const QString& keyId) override;
    Result<QList<TrustedSigningKey>> listSigningKeys() override;
    Result<void>                     revokeSigningKey(const QString& keyId) override;

private:
    QSqlDatabase& m_db;

    static QString packageStatusToString(SyncPackageStatus s);
    static SyncPackageStatus packageStatusFromString(const QString& s);
    static QString conflictTypeToString(ConflictType t);
    static ConflictType conflictTypeFromString(const QString& s);
    static QString conflictStatusToString(ConflictStatus s);
    static ConflictStatus conflictStatusFromString(const QString& s);
};
