#pragma once
// AuditRepository.h — ProctorOps
// Concrete SQLite implementation of IAuditRepository.
// Enforces append-only semantics: insertEntry is the only write operation for audit_entries.

#include "IAuditRepository.h"
#include <QSqlDatabase>

class AuditRepository : public IAuditRepository {
public:
    explicit AuditRepository(QSqlDatabase& db);

    // ── Append-only writes ─────────────────────────────────────────────────
    Result<AuditEntry>         insertEntry(const AuditEntry& entry) override;
    Result<QString>            getChainHeadHash() override;

    // ── Queries ────────────────────────────────────────────────────────────
    Result<AuditEntry>         findEntryById(const QString& entryId) override;
    Result<QList<AuditEntry>>  queryEntries(const AuditFilter& filter) override;

    // ── Retention ──────────────────────────────────────────────────────────
    Result<qint64>             countEntriesOlderThan(const QDateTime& threshold) override;
    Result<void>               purgeEntriesOlderThan(const QDateTime& threshold) override;

    // ── Export and deletion requests ───────────────────────────────────────
    Result<ExportRequest>          insertExportRequest(const ExportRequest& req) override;
    Result<ExportRequest>          getExportRequest(const QString& id) override;
    Result<QList<ExportRequest>>   listExportRequests(const QString& statusFilter = {}) override;
    Result<void>                   updateExportRequest(const ExportRequest& req) override;

    Result<DeletionRequest>        insertDeletionRequest(const DeletionRequest& req) override;
    Result<DeletionRequest>        getDeletionRequest(const QString& id) override;
    Result<QList<DeletionRequest>> listDeletionRequests(const QString& statusFilter = {}) override;
    Result<void>                   updateDeletionRequest(const DeletionRequest& req) override;

private:
    QSqlDatabase& m_db;

    static AuditEntry entryFromQuery(class QSqlQuery& q);
};
