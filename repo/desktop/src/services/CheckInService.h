#pragma once
// CheckInService.h — ProctorOps
// Entry validation engine: member resolution, term-card enforcement, freeze blocking,
// atomic punch-card deduction with 30-second duplicate suppression, and correction workflow.

#include "repositories/IMemberRepository.h"
#include "repositories/ICheckInRepository.h"
#include "services/AuditService.h"
#include "crypto/AesGcmCipher.h"
#include "models/CheckIn.h"
#include "models/Member.h"
#include "utils/Result.h"

#include <QSqlDatabase>

class AuthService;

struct FreezeRecordView {
    QString   memberId;  // human-readable member id
    QString   reason;
    QDateTime frozenAt;
    QDateTime thawedAt;
};

class CheckInService {
public:
    CheckInService(IMemberRepository& memberRepo,
                   ICheckInRepository& checkInRepo,
                   AuthService& authService,
                   AuditService& auditService,
                   AesGcmCipher& cipher,
                   QSqlDatabase& db);

    // ── Check-in ───────────────────────────────────────────────────────────

    /// Full check-in flow: resolve member → validate → deduct → audit.
    /// punchCardId is optional; if empty, selects first available.
    [[nodiscard]] Result<CheckInResult> checkIn(const MemberIdentifier& identifier,
                                                  const QString& sessionId,
                                                  const QString& operatorUserId,
                                                  const QString& punchCardId = {});

    /// Resolve a member from any identifier type (barcode, memberId, mobile).
    [[nodiscard]] Result<Member> resolveMember(const MemberIdentifier& identifier);

    /// Normalize a phone number to (###) ###-#### format.
    [[nodiscard]] static QString normalizeMobile(const QString& input);

    // ── Corrections ────────────────────────────────────────────────────────

    /// Create a correction request for a wrong deduction.
    [[nodiscard]] Result<CorrectionRequest> requestCorrection(
        const QString& deductionEventId,
        const QString& rationale,
        const QString& requestedByUserId);

    /// Approve a correction (requires SecurityAdministrator + step-up).
    [[nodiscard]] Result<void> approveCorrection(
        const QString& correctionRequestId,
        const QString& rationale,
        const QString& approvedByUserId,
        const QString& stepUpWindowId);

    /// Apply an approved correction (SecurityAdministrator + step-up):
    /// restore punch-card balance, mark deduction reversed.
    [[nodiscard]] Result<void> applyCorrection(const QString& correctionRequestId,
                                                const QString& actorUserId,
                                                const QString& stepUpWindowId);

    /// Reject a pending correction request.
    [[nodiscard]] Result<void> rejectCorrection(
        const QString& correctionRequestId,
        const QString& rationale,
        const QString& rejectedByUserId,
        const QString& stepUpWindowId);

    /// List all pending correction requests.
    [[nodiscard]] Result<QList<CorrectionRequest>> listPendingCorrections();

    // ── Security administration: member freeze controls ─────────────────

    /// Apply a member freeze with explicit rationale (SecurityAdministrator + step-up).
    [[nodiscard]] Result<void> freezeMemberAccount(
        const QString& memberHumanId,
        const QString& reason,
        const QString& actorUserId,
        const QString& stepUpWindowId);

    /// Thaw an active member freeze (SecurityAdministrator + step-up).
    [[nodiscard]] Result<void> thawMemberAccount(
        const QString& memberHumanId,
        const QString& actorUserId,
        const QString& stepUpWindowId);

    /// List recent freeze records for security administration (SecurityAdministrator).
    [[nodiscard]] Result<QList<FreezeRecordView>> listRecentFreezeRecords(
        const QString& actorUserId,
        int limit = 50);

private:
    IMemberRepository&   m_memberRepo;
    ICheckInRepository&  m_checkInRepo;
    AuthService&         m_authService;
    AuditService&        m_auditService;
    AesGcmCipher&        m_cipher;
    QSqlDatabase&        m_db;

    /// Record a failed check-in attempt with status and failure reason.
    Result<void> recordFailedAttempt(const QString& memberId,
                                      const QString& sessionId,
                                      const QString& operatorUserId,
                                      CheckInStatus status,
                                      const QString& failureReason,
                                      AuditEventType auditEvent);
};
