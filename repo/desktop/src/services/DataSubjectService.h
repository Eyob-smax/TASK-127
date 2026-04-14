#pragma once
// DataSubjectService.h — ProctorOps
// GDPR / China MLPS data-subject workflow service.
//
// Governs two workflows:
//   1. Subject access / export — member requests a copy of their personal data.
//      - Requester creates an ExportRequest (any authenticated operator).
//      - SecurityAdministrator fulfills it by generating a redacted JSON file.
//      - Exported file is watermarked and includes only non-PII or explicitly
//        authorized PII fields; sensitive fields are masked per MaskingPolicy.
//
//   2. Erasure / deletion — member requests removal of their personal data.
//      - Requester creates a DeletionRequest (any authenticated operator).
//      - SecurityAdministrator approves it (step-up required).
//      - On completion, all PII fields are anonymized via MemberRepository::anonymizeMember.
//      - Audit tombstones are retained per AuditRetentionYears (3 years).
//      - The deletion record itself is retained for compliance evidence.
//
// Compliance scope: aligns with GDPR right-of-access (Art. 15), right-to-erasure
// (Art. 17), and China MLPS personal information protection (GB/T 22239-2019 §8).
// All operations are bounded to local data — no external transfers occur.

#include "repositories/IAuditRepository.h"
#include "repositories/IMemberRepository.h"
#include "services/AuditService.h"
#include "crypto/AesGcmCipher.h"
#include "utils/Result.h"
#include "models/Audit.h"

#include <QString>
#include <QList>

class AuthService;

class DataSubjectService {
public:
    DataSubjectService(IAuditRepository& auditRepo,
                        IMemberRepository& memberRepo,
                        AuthService& authService,
                        AuditService& auditService,
                        AesGcmCipher& cipher);

    // ── Export requests ────────────────────────────────────────────────────────
    /// Create a new data-subject export request. Any authenticated operator may
    /// submit; fulfillment requires SecurityAdministrator role.
    [[nodiscard]] Result<ExportRequest> createExportRequest(const QString& memberId,
                                                             const QString& rationale,
                                                             const QString& actorUserId);

    /// Fulfill the export request: decrypt member PII, write a redacted/watermarked
    /// JSON export file to outputFilePath. Requires SecurityAdministrator + step-up.
    [[nodiscard]] Result<ExportRequest> fulfillExportRequest(const QString& requestId,
                                                              const QString& outputFilePath,
                                                              const QString& actorUserId,
                                                              const QString& stepUpWindowId);

    /// Reject an export request (e.g. member not found or request is invalid).
    [[nodiscard]] Result<void> rejectExportRequest(const QString& requestId,
                                                    const QString& actorUserId,
                                                    const QString& stepUpWindowId);

    [[nodiscard]] Result<QList<ExportRequest>> listExportRequests(
                                                    const QString& actorUserId,
                                                    const QString& statusFilter = {});

    // ── Deletion requests ──────────────────────────────────────────────────────
    /// Create a new erasure request. Any authenticated operator may submit;
    /// approval and completion require SecurityAdministrator + step-up.
    [[nodiscard]] Result<DeletionRequest> createDeletionRequest(const QString& memberId,
                                                                 const QString& rationale,
                                                                 const QString& actorUserId);

    /// Approve a deletion request. Requires SecurityAdministrator + step-up.
    [[nodiscard]] Result<DeletionRequest> approveDeletionRequest(const QString& requestId,
                                                                  const QString& approverUserId,
                                                                  const QString& stepUpWindowId);

    /// Execute the approved deletion: anonymize member PII fields.
    /// Audit tombstones and the deletion record are retained.
    [[nodiscard]] Result<void> completeDeletion(const QString& requestId,
                                                 const QString& actorUserId,
                                                 const QString& stepUpWindowId);

    /// Reject a pending deletion request.
    [[nodiscard]] Result<void> rejectDeletionRequest(const QString& requestId,
                                                      const QString& actorUserId,
                                                      const QString& stepUpWindowId);

    [[nodiscard]] Result<QList<DeletionRequest>> listDeletionRequests(
                                                      const QString& actorUserId,
                                                      const QString& statusFilter = {});

private:
    IAuditRepository& m_auditRepo;
    IMemberRepository& m_memberRepo;
    AuthService&      m_authService;
    AuditService&      m_auditService;
    AesGcmCipher&      m_cipher;
};
