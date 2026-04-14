#pragma once
// CheckIn.h — ProctorOps
// Domain models for check-in attempts, deduction events, and correction workflows.

#include <QString>
#include <QDateTime>
#include <QStringList>

// ── CheckInStatus ────────────────────────────────────────────────────────────
enum class CheckInStatus {
    Success,
    DuplicateBlocked,  // same member + same sessionId within 30-second window
    FrozenBlocked,     // member has an active MemberFreezeRecord
    TermCardExpired,   // no valid term card for today's date
    TermCardMissing,   // no term card on file
    PunchCardExhausted, // currentBalance == 0
    Failed,            // other failure (see failureReason)
};

// ── CheckInAttempt ────────────────────────────────────────────────────────────
// Immutable record of each check-in attempt (success or failure).
struct CheckInAttempt {
    QString        id;
    QString        memberId;
    QString        sessionId;       // external session or roster identifier
    QString        operatorUserId;
    CheckInStatus  status;
    QDateTime      attemptedAt;     // UTC; used for 30-second dedup window
    QString        deductionEventId; // empty on non-success
    QString        failureReason;    // human-readable; empty on success
};

// ── DeductionEvent ────────────────────────────────────────────────────────────
// Immutable record of each successful punch-card session deduction.
// Corrections write a compensating record rather than editing this row.
struct DeductionEvent {
    QString   id;
    QString   memberId;
    QString   punchCardId;
    QString   checkInAttemptId;
    int       sessionsDeducted;        // always 1 for a single check-in
    int       balanceBefore;
    int       balanceAfter;            // balanceBefore - sessionsDeducted
    QDateTime deductedAt;
    QString   reversedByCorrectionId;  // empty if not reversed
    // Invariant: balanceAfter == balanceBefore - sessionsDeducted
    // Invariant: balanceAfter >= 0
};

// ── CorrectionStatus ─────────────────────────────────────────────────────────
enum class CorrectionStatus {
    Pending,   // submitted, awaiting security-admin review
    Approved,  // approved with step-up; awaiting application
    Applied,   // compensating deduction written; punch-card balance restored
    Rejected,  // denied by security administrator
};

// ── CorrectionRequest ────────────────────────────────────────────────────────
struct CorrectionRequest {
    QString          id;
    QString          deductionEventId;
    QString          requestedByUserId; // PROCTOR or SECURITY_ADMINISTRATOR
    QString          rationale;
    CorrectionStatus status;
    QDateTime        createdAt;
};

// ── CorrectionApproval ────────────────────────────────────────────────────────
// Written when a security administrator approves a correction with step-up auth.
// Records the before/after state for audit immutability.
struct CorrectionApproval {
    QString   correctionRequestId;
    QString   approvedByUserId;
    QString   stepUpWindowId;       // the StepUpWindow id that authorized this action
    QString   rationale;
    QDateTime approvedAt;
    QString   beforePayloadJson;    // DeductionEvent state before reversal
    QString   afterPayloadJson;     // PunchCard balance state after reversal
};

// ── CheckInResult ────────────────────────────────────────────────────────────
// Returned by CheckInService::checkIn() on success.
struct CheckInResult {
    QString   memberId;
    QString   memberNameMasked;     // last 4 characters; full name requires step-up
    QString   sessionId;
    QString   deductionEventId;
    int       remainingBalance;
    QDateTime checkInTimestamp;     // UTC
};
