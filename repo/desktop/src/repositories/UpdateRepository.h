#pragma once
// UpdateRepository.h — ProctorOps
// Concrete SQLite implementation of IUpdateRepository.
// Manages offline update package metadata, staged components,
// install history, and rollback records.
//
// Override: delivery does not require a signed .msi artifact.
// Full update/rollback domain logic is in scope. See docs/design.md §2.

#include "IUpdateRepository.h"
#include <QSqlDatabase>

class UpdateRepository : public IUpdateRepository {
public:
    explicit UpdateRepository(QSqlDatabase& db);

    // ── Update packages ────────────────────────────────────────────────────
    Result<UpdatePackage>          insertPackage(const UpdatePackage& pkg)              override;
    Result<UpdatePackage>          getPackage(const QString& id)                        override;
    Result<QList<UpdatePackage>>   listPackages()                                       override;
    Result<void>                   updatePackageStatus(const QString& id,
                                                        UpdatePackageStatus status)     override;
    Result<void>                   markSignatureValid(const QString& id, bool valid)    override;

    // ── Package components ─────────────────────────────────────────────────
    Result<void>                   insertComponent(const UpdateComponent& c)            override;
    Result<QList<UpdateComponent>> getComponents(const QString& packageId)              override;

    // ── Install history ────────────────────────────────────────────────────
    Result<InstallHistoryEntry>                insertInstallHistory(const InstallHistoryEntry&) override;
    Result<InstallHistoryEntry>                getInstallHistory(const QString& id)             override;
    Result<QList<InstallHistoryEntry>>         listInstallHistory()                             override;
    Result<std::optional<InstallHistoryEntry>> latestInstallHistory()                          override;

    // ── Rollback records ───────────────────────────────────────────────────
    Result<RollbackRecord>        insertRollbackRecord(const RollbackRecord& rec)       override;
    Result<QList<RollbackRecord>> listRollbackRecords()                                 override;

private:
    QSqlDatabase& m_db;

    static UpdatePackage      packageFromQuery(class QSqlQuery& q);
    static UpdateComponent    componentFromQuery(class QSqlQuery& q);
    static InstallHistoryEntry historyFromQuery(class QSqlQuery& q);
    static RollbackRecord     rollbackFromQuery(class QSqlQuery& q);

    static QString packageStatusToString(UpdatePackageStatus s);
    static UpdatePackageStatus packageStatusFromString(const QString& s);
};
