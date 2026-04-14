#pragma once
// IUpdateRepository.h — ProctorOps
// Pure interface for update package records, install history, and rollback records.
// Override: delivery does not require a signed .msi; all logic remains in scope.

#include "models/Update.h"
#include "utils/Result.h"
#include <QString>
#include <QList>
#include <optional>

class IUpdateRepository {
public:
    virtual ~IUpdateRepository() = default;

    // ── Update packages ────────────────────────────────────────────────────
    virtual Result<UpdatePackage>          insertPackage(const UpdatePackage& pkg)          = 0;
    virtual Result<UpdatePackage>          getPackage(const QString& id)                    = 0;
    virtual Result<QList<UpdatePackage>>   listPackages()                                   = 0;
    virtual Result<void>                   updatePackageStatus(const QString& id,
                                                                UpdatePackageStatus status) = 0;
    virtual Result<void>                   markSignatureValid(const QString& id, bool valid)= 0;

    // ── Update components ──────────────────────────────────────────────────
    virtual Result<void>                   insertComponent(const UpdateComponent& c)        = 0;
    virtual Result<QList<UpdateComponent>> getComponents(const QString& packageId)          = 0;

    // ── Install history ────────────────────────────────────────────────────
    virtual Result<InstallHistoryEntry>                 insertInstallHistory(const InstallHistoryEntry&) = 0;
    virtual Result<InstallHistoryEntry>                 getInstallHistory(const QString& id)             = 0;
    virtual Result<QList<InstallHistoryEntry>>          listInstallHistory()                             = 0;
    virtual Result<std::optional<InstallHistoryEntry>>  latestInstallHistory()                          = 0;

    // ── Rollback records ───────────────────────────────────────────────────
    virtual Result<RollbackRecord>               insertRollbackRecord(const RollbackRecord&)      = 0;
    virtual Result<QList<RollbackRecord>>         listRollbackRecords()                           = 0;
};
