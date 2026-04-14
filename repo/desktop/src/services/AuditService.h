#pragma once
// AuditService.h — ProctorOps
// High-level audit event recording with hash chain computation and PII encryption.
// All audit entries flow through this service to ensure chain integrity.

#include "repositories/IAuditRepository.h"
#include "crypto/AesGcmCipher.h"
#include "models/Audit.h"
#include "models/CommonTypes.h"
#include "utils/Result.h"

#include <QJsonObject>
#include <QVector>
#include <optional>

// Forward declaration — avoids circular include; pointer stored via setAuthService().
class AuthService;

class AuditService {
public:
    AuditService(IAuditRepository& auditRepo, AesGcmCipher& cipher);

    /// Wire in the AuthService for service-layer RBAC on sensitive read paths.
    /// Called once from main() after both services are constructed.
    /// If never called (e.g. in unit tests), role checks on queryEvents are skipped.
    void setAuthService(AuthService* auth);

    /// Record an audit event. PII fields in payloads are automatically encrypted.
    /// Chain head hash is fetched and entry hash computed before insertion.
    Result<void> recordEvent(const QString& actorUserId,
                              AuditEventType eventType,
                              const QString& entityType,
                              const QString& entityId,
                              const QJsonObject& beforePayload = {},
                              const QJsonObject& afterPayload = {});

    /// Backward-compatible wrapper used by services that pass actor after entity ids.
    Result<void> record(AuditEventType eventType,
                         const QString& entityType,
                         const QString& entityId,
                         const QString& actorUserId,
                         const QJsonObject& beforePayload = {},
                         const QJsonObject& afterPayload = {});

    /// Query audit entries with filters.
    /// Requires actorUserId to hold SecurityAdministrator role when an AuthService
    /// has been wired in via setAuthService(). Returns AuthorizationDenied otherwise.
    [[nodiscard]] Result<QList<AuditEntry>> queryEvents(const QString& actorUserId,
                                                         const AuditFilter& filter);

    /// Verify the audit chain integrity by recomputing all hashes forward.
    /// Requires SecurityAdministrator role when an AuthService has been wired in.
    /// If limit is set, only verifies up to that many entries.
    [[nodiscard]] Result<ChainVerifyReport> verifyChain(const QString& actorUserId,
                                                         std::optional<int> limit = {});

    /// Governance-gated retention purge.
    /// Requires SecurityAdministrator role + a valid step-up window (actorUserId must have
    /// initiated step-up before calling). Enforces that the threshold is at least
    /// Validation::AuditRetentionYears years in the past; rejects any threshold
    /// more recent than the minimum. Records a chain-anchor snapshot before deletion.
    [[nodiscard]] Result<void> purgeAuditEntries(const QString& actorUserId,
                                                  const QString& stepUpWindowId,
                                                  const QDateTime& threshold);

private:
    IAuditRepository& m_auditRepo;
    AesGcmCipher&     m_cipher;
    AuthService*      m_authService{nullptr};

    /// Encrypt known PII field values within a JSON payload.
    [[nodiscard]] QString encryptPayload(const QJsonObject& payload);

    /// Names of fields that contain PII and must be encrypted before audit storage.
    static const QStringList s_piiFieldNames;
};
