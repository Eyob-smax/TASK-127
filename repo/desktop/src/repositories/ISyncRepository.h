#pragma once
// ISyncRepository.h — ProctorOps
// Pure interface for sync package records, entity manifests, conflict records,
// and trusted signing key management.

#include "models/Sync.h"
#include "utils/Result.h"
#include <QString>
#include <QList>
#include <optional>

class ISyncRepository {
public:
    virtual ~ISyncRepository() = default;

    // ── Sync packages ──────────────────────────────────────────────────────
    virtual Result<SyncPackage>          insertPackage(const SyncPackage& pkg)               = 0;
    virtual Result<SyncPackage>          getPackage(const QString& id)                        = 0;
    virtual Result<QList<SyncPackage>>   listPackages()                                       = 0;
    virtual Result<std::optional<SyncPackage>> latestExportForDesk(const QString& deskId)     = 0;
    virtual Result<void>                 updatePackageStatus(const QString& id,
                                                              SyncPackageStatus status)       = 0;

    // ── Package entities ───────────────────────────────────────────────────
    virtual Result<void>                 insertPackageEntity(const SyncPackageEntity& entity) = 0;
    virtual Result<QList<SyncPackageEntity>> getPackageEntities(const QString& packageId)    = 0;
    virtual Result<void>                 markEntityVerified(const QString& packageId,
                                                             const QString& entityType)       = 0;
    virtual Result<void>                 markEntityApplied(const QString& packageId,
                                                            const QString& entityType)        = 0;

    // ── Conflict records ───────────────────────────────────────────────────
    virtual Result<ConflictRecord>          insertConflict(const ConflictRecord& c)           = 0;
    virtual Result<ConflictRecord>          getConflict(const QString& conflictId)            = 0;
    virtual Result<QList<ConflictRecord>>   listPendingConflicts(const QString& packageId)    = 0;
    virtual Result<void>                    resolveConflict(const QString& conflictId,
                                                             ConflictStatus status,
                                                             const QString& resolvedByUserId,
                                                             const QString& resolutionActionType,
                                                             const QString& resolutionActionId) = 0;

    // ── Trusted signing keys ───────────────────────────────────────────────
    virtual Result<TrustedSigningKey>        insertSigningKey(const TrustedSigningKey& key)   = 0;
    virtual Result<TrustedSigningKey>        findSigningKeyById(const QString& keyId)         = 0;
    virtual Result<QList<TrustedSigningKey>> listSigningKeys()                                = 0;
    virtual Result<void>                     revokeSigningKey(const QString& keyId)            = 0;
};
