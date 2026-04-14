#pragma once
// CommonTypes.h — ProctorOps
// Shared enumerations used across multiple domain model headers.
// Include this header instead of redefining enums locally.

#include <QObject>
#include <QString>

// ── Role (RBAC) ───────────────────────────────────────────────────────────────
// Ordered from least to most privileged. Services compare role >= required.
enum class Role : int {
    FrontDeskOperator    = 0,
    Proctor              = 1,
    ContentManager       = 2,
    SecurityAdministrator = 3,
};

/// Returns the canonical string representation of a Role (for audit payloads).
inline QString roleToString(Role r) {
    switch (r) {
        case Role::FrontDeskOperator:     return QStringLiteral("FRONT_DESK_OPERATOR");
        case Role::Proctor:               return QStringLiteral("PROCTOR");
        case Role::ContentManager:        return QStringLiteral("CONTENT_MANAGER");
        case Role::SecurityAdministrator: return QStringLiteral("SECURITY_ADMINISTRATOR");
    }
    return QStringLiteral("UNKNOWN");
}

// ── AuditEventType ────────────────────────────────────────────────────────────
enum class AuditEventType {
    // Auth
    Login, LoginFailed, LoginLocked,
    CaptchaChallenge, CaptchaSolved, CaptchaFailed,
    Logout, ConsoleLocked, ConsoleUnlocked,
    StepUpInitiated, StepUpPassed, StepUpFailed,

    // User management
    UserCreated, UserUpdated, UserDeactivated, RoleChanged,
    UserUnlocked, PasswordReset,
    MemberFreezeApplied, MemberFreezeThawed,

    // Check-in
    CheckInAttempted, CheckInSuccess,
    CheckInDuplicateBlocked, CheckInFrozenBlocked,
    CheckInExpiredBlocked, CheckInTermCardInvalid,
    CheckInPunchCardExhausted,
    DeductionCreated, DeductionReversed,
    CorrectionRequested, CorrectionApproved,
    CorrectionRejected, CorrectionApplied,

    // Question governance
    QuestionCreated, QuestionUpdated, QuestionDeleted,
    KnowledgePointCreated, KnowledgePointUpdated, KnowledgePointDeleted,
    KnowledgePointMapped, KnowledgePointUnmapped,
    TagCreated, TagApplied, TagRemoved,

    // Ingestion
    JobCreated, JobStarted, JobCompleted,
    JobFailed, JobCancelled, JobInterrupted,

    // Sync and packages
    SyncExport, SyncImport, SyncConflictResolved,

    // Update and rollback
    UpdateImported, UpdateStaged, UpdateApplied, UpdateRolledBack,

    // Crypto / trust store
    KeyImported, KeyRevoked, KeyRotated,

    // Export and compliance
    ExportRequested, ExportCompleted,
    DeletionRequested, DeletionApproved, DeletionCompleted,

    // Audit itself
    ChainVerified, AuditExport,
};

/// Returns the canonical string representation for audit_entries.event_type column.
QString auditEventTypeToString(AuditEventType t);
