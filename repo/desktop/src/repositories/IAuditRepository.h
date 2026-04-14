#pragma once
// IAuditRepository.h — ProctorOps
// Pure interface for the tamper-evident audit chain.
// Entries are append-only. No update or delete methods are provided.

#include "models/Audit.h"
#include "utils/Result.h"
#include <QString>
#include <QList>
#include <QDateTime>

class IAuditRepository {
public:
    virtual ~IAuditRepository() = default;

    // ── Append-only writes ─────────────────────────────────────────────────
    // Inserts a new entry and atomically updates the audit_chain_head record.
    // Must be called within an exclusive transaction to preserve chain ordering.
    virtual Result<AuditEntry>          insertEntry(const AuditEntry& entry)                  = 0;

    // ── Chain head ─────────────────────────────────────────────────────────
    // Returns the hash of the most recently written entry (for new entry's previousEntryHash).
    // Returns an empty string if the chain is empty (genesis entry).
    virtual Result<QString>             getChainHeadHash()                                     = 0;

    // ── Queries (read-only) ────────────────────────────────────────────────
    virtual Result<AuditEntry>          findEntryById(const QString& entryId)                  = 0;
    virtual Result<QList<AuditEntry>>   queryEntries(const AuditFilter& filter)               = 0;

    // ── Retention ──────────────────────────────────────────────────────────
    // Returns count of entries older than threshold (for retention reporting).
    virtual Result<qint64>              countEntriesOlderThan(const QDateTime& threshold)      = 0;
    // Purge is only allowed with security-admin step-up; records this in the audit log.
    // Default retention is Validation::AuditRetentionYears years; not reducible below 1 year.
    virtual Result<void>                purgeEntriesOlderThan(const QDateTime& threshold)      = 0;

    // ── Export and deletion requests ───────────────────────────────────────
    virtual Result<ExportRequest>          insertExportRequest(const ExportRequest&)             = 0;
    virtual Result<ExportRequest>          getExportRequest(const QString& id)                   = 0;
    virtual Result<QList<ExportRequest>>   listExportRequests(const QString& statusFilter = {})  = 0;
    virtual Result<void>                   updateExportRequest(const ExportRequest&)             = 0;

    virtual Result<DeletionRequest>        insertDeletionRequest(const DeletionRequest&)         = 0;
    virtual Result<DeletionRequest>        getDeletionRequest(const QString& id)                 = 0;
    virtual Result<QList<DeletionRequest>> listDeletionRequests(const QString& statusFilter = {}) = 0;
    virtual Result<void>                   updateDeletionRequest(const DeletionRequest&)         = 0;
};
