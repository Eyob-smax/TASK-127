#pragma once
// Audit.h — ProctorOps
// Domain models for the tamper-evident audit chain, export requests, and deletion requests.
// Audit entries are immutable once written. The hash chain prevents undetected modification.

#include "CommonTypes.h"
#include <QString>
#include <QDateTime>
#include <QStringList>
#include <optional>

// ── AuditEntry ────────────────────────────────────────────────────────────────
// Immutable. Written by AuditService::record(). Never updated or deleted.
// PII fields within before/after payloads are AES-256-GCM encrypted before storage.
struct AuditEntry {
    QString        id;                 // UUID
    QDateTime      timestamp;          // UTC; set by AuditService at write time
    QString        actorUserId;        // the authenticated user triggering the event
    AuditEventType eventType;
    QString        entityType;         // e.g. "Question", "Member", "CheckIn"
    QString        entityId;           // the affected entity's UUID
    QString        beforePayloadJson;  // field-level diff before; PII fields encrypted
    QString        afterPayloadJson;   // field-level diff after; PII fields encrypted
    QString        previousEntryHash;  // SHA-256 hex of previous entry's canonical form
    QString        entryHash;          // SHA-256 hex of this entry's canonical form
    // Invariant: entryHash == SHA256(canonical(this entry including previousEntryHash))
    // Invariant: previousEntryHash matches the entryHash of the immediately prior entry
    // Invariant: once written, no field may be modified (enforce at repository level)
};

// ── AuditFilter ───────────────────────────────────────────────────────────────
struct AuditFilter {
    QString                      actorUserId;
    std::optional<AuditEventType> eventType;
    QString                      entityType;
    QString                      entityId;
    std::optional<QDateTime>     fromTimestamp;
    std::optional<QDateTime>     toTimestamp;
    int                          limit  = 100;
    int                          offset = 0;
};

// ── ChainVerifyReport ─────────────────────────────────────────────────────────
struct ChainVerifyReport {
    qint64   entriesVerified;
    QString  firstEntryId;
    QString  lastEntryId;
    bool     integrityOk;
    QString  firstBrokenEntryId; // empty if integrityOk
};

// ── ExportRequest ─────────────────────────────────────────────────────────────
// GDPR / MLPS data subject access request.
struct ExportRequest {
    QString   id;
    QString   memberId;
    QString   requesterUserId;
    QString   status;            // "PENDING", "COMPLETED", "REJECTED"
    QString   rationale;
    QDateTime createdAt;
    QDateTime fulfilledAt;       // null until completed
    QString   outputFilePath;    // null until completed
};

// ── DeletionRequest ──────────────────────────────────────────────────────────
// GDPR / MLPS erasure request.
// On completion, PII fields are anonymized; audit tombstones are retained.
struct DeletionRequest {
    QString      id;
    QString      memberId;
    QString      requesterUserId;
    QString      approverUserId;   // empty until approved
    QString      status;           // "PENDING", "APPROVED", "COMPLETED", "REJECTED"
    QString      rationale;
    QDateTime    createdAt;
    QDateTime    approvedAt;       // null until approved
    QDateTime    completedAt;      // null until completed
    QStringList  fieldsAnonymized; // populated on completion
};
