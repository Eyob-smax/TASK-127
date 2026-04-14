#pragma once
// CheckInRepository.h — ProctorOps
// Concrete SQLite implementation of ICheckInRepository.
// Manages check-in attempts, deduction events, and correction workflows.

#include "ICheckInRepository.h"
#include <QSqlDatabase>

class CheckInRepository : public ICheckInRepository {
public:
    explicit CheckInRepository(QSqlDatabase& db);

    // ── Check-in attempts ──────────────────────────────────────────────────
    Result<CheckInAttempt>                    insertAttempt(const CheckInAttempt& attempt) override;
    Result<CheckInAttempt>                    getAttempt(const QString& id) override;
    Result<std::optional<CheckInAttempt>>     findRecentSuccess(const QString& memberId,
                                                                  const QString& sessionId,
                                                                  const QDateTime& since) override;

    // ── Deduction events ───────────────────────────────────────────────────
    Result<DeductionEvent>  insertDeduction(const DeductionEvent& ev) override;
    Result<DeductionEvent>  getDeduction(const QString& id) override;
    Result<QList<QJsonObject>> listDeductionDelta(const QDateTime& since) override;
    Result<bool>            applyIncomingDeduction(const QJsonObject& record,
                                                    const QString& actorUserId) override;
    Result<std::optional<QJsonObject>> findLocalDeductionConflict(const QString& memberId,
                                                                  const QString& sessionId) override;
    Result<void>            setDeductionReversed(const QString& deductionId,
                                                  const QString& correctionId) override;

    // ── Correction requests ────────────────────────────────────────────────
    Result<CorrectionRequest>       insertCorrectionRequest(const CorrectionRequest& r) override;
    Result<CorrectionRequest>       getCorrectionRequest(const QString& id) override;
    Result<QList<CorrectionRequest>> listCorrectionRequests(CorrectionStatus status) override;
    Result<QList<QJsonObject>>      listCorrectionDelta(const QDateTime& since) override;
    Result<bool>                    applyIncomingCorrection(const QJsonObject& record,
                                                             const QString& actorUserId) override;
    Result<void>                    updateCorrectionStatus(const QString& id,
                                                            CorrectionStatus status) override;
    Result<QString>                 createCompensatingCorrection(const QString& deductionEventId,
                                                                  const QString& actorUserId,
                                                                  const QString& rationale) override;

    // ── Correction approvals ───────────────────────────────────────────────
    Result<CorrectionApproval>  insertCorrectionApproval(const CorrectionApproval& a) override;
    Result<CorrectionApproval>  getCorrectionApproval(const QString& requestId) override;

private:
    QSqlDatabase& m_db;

    static CheckInAttempt rowToAttempt(const QSqlQuery& q);
    static DeductionEvent rowToDeduction(const QSqlQuery& q);
    static CorrectionRequest rowToCorrectionRequest(const QSqlQuery& q);

    static QString checkInStatusToString(CheckInStatus s);
    static CheckInStatus checkInStatusFromString(const QString& s);
    static QString correctionStatusToString(CorrectionStatus s);
    static CorrectionStatus correctionStatusFromString(const QString& s);
};
