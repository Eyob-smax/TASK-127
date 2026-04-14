#pragma once
// ICheckInRepository.h — ProctorOps
// Pure interface for check-in attempts, deduction events, and correction workflows.

#include "models/CheckIn.h"
#include "utils/Result.h"
#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <optional>

class ICheckInRepository {
public:
    virtual ~ICheckInRepository() = default;

    // ── Check-in attempts ──────────────────────────────────────────────────
    virtual Result<CheckInAttempt>          insertAttempt(const CheckInAttempt& attempt) = 0;
    virtual Result<CheckInAttempt>          getAttempt(const QString& id)                = 0;

    // Duplicate detection: returns the most recent successful check-in for
    // (memberId, sessionId) within the last Validation::DuplicateWindowSeconds seconds.
    // Returns nullopt if no duplicate is found.
    virtual Result<std::optional<CheckInAttempt>>
                                            findRecentSuccess(const QString& memberId,
                                                              const QString& sessionId,
                                                              const QDateTime& since) = 0;

    // ── Deduction events ───────────────────────────────────────────────────
    // insertDeduction must be called inside the same transaction as deductSession().
    virtual Result<DeductionEvent>          insertDeduction(const DeductionEvent& ev)    = 0;
    virtual Result<DeductionEvent>          getDeduction(const QString& id)              = 0;
    virtual Result<QList<QJsonObject>>      listDeductionDelta(const QDateTime& since)   = 0;
    // Apply a deduction imported from a sync package.
    // Returns true when a new deduction is materialized, false when already applied.
    virtual Result<bool>                    applyIncomingDeduction(const QJsonObject& record,
                                                                    const QString& actorUserId) = 0;
    virtual Result<std::optional<QJsonObject>>
                                            findLocalDeductionConflict(const QString& memberId,
                                                                       const QString& sessionId) = 0;
    // Mark a deduction as reversed by a correction.
    virtual Result<void>                    setDeductionReversed(const QString& deductionId,
                                                                  const QString& correctionId) = 0;

    // ── Correction requests ────────────────────────────────────────────────
    virtual Result<CorrectionRequest>       insertCorrectionRequest(const CorrectionRequest&)  = 0;
    virtual Result<CorrectionRequest>       getCorrectionRequest(const QString& id)             = 0;
    virtual Result<QList<CorrectionRequest>> listCorrectionRequests(CorrectionStatus status)    = 0;
    virtual Result<QList<QJsonObject>>      listCorrectionDelta(const QDateTime& since)  = 0;
    // Apply a correction imported from a sync package.
    // Returns true when a new correction is materialized, false when already applied.
    virtual Result<bool>                    applyIncomingCorrection(const QJsonObject& record,
                                                                     const QString& actorUserId) = 0;
    virtual Result<void>                    updateCorrectionStatus(const QString& id,
                                                                    CorrectionStatus status)    = 0;

    // Create and apply a compensating correction that reverses a local deduction.
    virtual Result<QString>                 createCompensatingCorrection(const QString& deductionEventId,
                                                                         const QString& actorUserId,
                                                                         const QString& rationale) = 0;

    // ── Correction approvals ───────────────────────────────────────────────
    virtual Result<CorrectionApproval>      insertCorrectionApproval(const CorrectionApproval&) = 0;
    virtual Result<CorrectionApproval>      getCorrectionApproval(const QString& requestId)     = 0;
};
