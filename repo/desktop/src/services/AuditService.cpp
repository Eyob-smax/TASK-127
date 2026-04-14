// AuditService.cpp — ProctorOps
// Audit event recording with hash chain and PII encryption.

#include "AuditService.h"
#include "services/AuthService.h"
#include "crypto/HashChain.h"
#include "utils/Logger.h"
#include "utils/Validation.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

const QStringList AuditService::s_piiFieldNames = {
    QStringLiteral("member_id"),
    QStringLiteral("memberId"),
    QStringLiteral("mobile"),
    QStringLiteral("barcode"),
    QStringLiteral("name"),
    QStringLiteral("memberIdEncrypted"),
    QStringLiteral("mobileEncrypted"),
    QStringLiteral("barcodeEncrypted"),
    QStringLiteral("nameEncrypted"),
};

AuditService::AuditService(IAuditRepository& auditRepo, AesGcmCipher& cipher)
    : m_auditRepo(auditRepo)
    , m_cipher(cipher)
{
}

void AuditService::setAuthService(AuthService* auth)
{
    m_authService = auth;
}

Result<void> AuditService::recordEvent(const QString& actorUserId,
                                        AuditEventType eventType,
                                        const QString& entityType,
                                        const QString& entityId,
                                        const QJsonObject& beforePayload,
                                        const QJsonObject& afterPayload)
{
    // Get chain head for linking
    auto headResult = m_auditRepo.getChainHeadHash();
    if (headResult.isErr())
        return Result<void>::err(headResult.errorCode(), headResult.errorMessage());

    AuditEntry entry;
    entry.id                = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.timestamp         = QDateTime::currentDateTimeUtc();
    entry.actorUserId       = actorUserId;
    entry.eventType         = eventType;
    entry.entityType        = entityType;
    entry.entityId          = entityId;
    entry.beforePayloadJson = encryptPayload(beforePayload);
    entry.afterPayloadJson  = encryptPayload(afterPayload);
    entry.previousEntryHash = headResult.value();

    // Compute entry hash
    entry.entryHash = HashChain::computeEntryHash(entry);

    auto insertResult = m_auditRepo.insertEntry(entry);
    if (insertResult.isErr()) {
        Logger::instance().error(QStringLiteral("AuditService"),
            QStringLiteral("Failed to insert audit entry"),
            {{QStringLiteral("eventType"), auditEventTypeToString(eventType)},
             {QStringLiteral("entityId"), entityId}});
        return Result<void>::err(insertResult.errorCode(), insertResult.errorMessage());
    }

    return Result<void>::ok();
}

Result<void> AuditService::record(AuditEventType eventType,
                                   const QString& entityType,
                                   const QString& entityId,
                                   const QString& actorUserId,
                                   const QJsonObject& beforePayload,
                                   const QJsonObject& afterPayload)
{
    return recordEvent(actorUserId, eventType, entityType, entityId, beforePayload, afterPayload);
}

Result<QList<AuditEntry>> AuditService::queryEvents(const QString& actorUserId,
                                                      const AuditFilter& filter)
{
    if (m_authService) {
        auto roleCheck = m_authService->requireRoleForActor(actorUserId,
                                                             Role::SecurityAdministrator);
        if (roleCheck.isErr())
            return Result<QList<AuditEntry>>::err(
                ErrorCode::AuthorizationDenied,
                QStringLiteral("Audit log access requires SecurityAdministrator role"));
    }
    return m_auditRepo.queryEntries(filter);
}

Result<ChainVerifyReport> AuditService::verifyChain(const QString& actorUserId,
                                                     std::optional<int> limit)
{
    if (m_authService) {
        auto roleCheck = m_authService->requireRoleForActor(actorUserId,
                                                             Role::SecurityAdministrator);
        if (roleCheck.isErr())
            return Result<ChainVerifyReport>::err(
                ErrorCode::AuthorizationDenied,
                QStringLiteral("Audit chain verification requires SecurityAdministrator role"));
    }

    AuditFilter filter;
    filter.limit = limit.value_or(10000);
    filter.offset = 0;

    auto entriesResult = m_auditRepo.queryEntries(filter);
    if (entriesResult.isErr())
        return Result<ChainVerifyReport>::err(entriesResult.errorCode(),
                                               entriesResult.errorMessage());

    const QList<AuditEntry>& entries = entriesResult.value();

    ChainVerifyReport report;
    report.entriesVerified = 0;
    report.integrityOk = true;

    if (entries.isEmpty()) {
        return Result<ChainVerifyReport>::ok(std::move(report));
    }

    report.firstEntryId = entries.first().id;

    QString expectedPrevHash;

    for (const AuditEntry& entry : entries) {
        // Verify previous hash linkage
        if (entry.previousEntryHash != expectedPrevHash) {
            report.integrityOk = false;
            report.firstBrokenEntryId = entry.id;
            report.lastEntryId = entry.id;
            break;
        }

        // Recompute entry hash
        QString recomputed = HashChain::computeEntryHash(entry);
        if (recomputed != entry.entryHash) {
            report.integrityOk = false;
            report.firstBrokenEntryId = entry.id;
            report.lastEntryId = entry.id;
            break;
        }

        expectedPrevHash = entry.entryHash;
        report.entriesVerified++;
        report.lastEntryId = entry.id;
    }

    Logger::instance().info(QStringLiteral("AuditService"),
        QStringLiteral("Chain verification complete"),
        {{QStringLiteral("entriesVerified"), static_cast<qint64>(report.entriesVerified)},
         {QStringLiteral("integrityOk"), report.integrityOk}});

    return Result<ChainVerifyReport>::ok(std::move(report));
}

Result<void> AuditService::purgeAuditEntries(const QString& actorUserId,
                                               const QString& stepUpWindowId,
                                               const QDateTime& threshold)
{
    // Service-layer gate: requires an AuthService to be wired in.
    if (!m_authService)
        return Result<void>::err(ErrorCode::InternalError,
            QStringLiteral("Retention purge unavailable: auth service not initialized"));

    // RBAC + step-up: SecurityAdministrator with a valid consumed step-up window.
    auto authCheck = m_authService->authorizePrivilegedAction(
        actorUserId, Role::SecurityAdministrator, stepUpWindowId);
    if (authCheck.isErr())
        return authCheck;

    // Enforce minimum retention period: threshold must not be more recent than
    // AuditRetentionYears years ago. This prevents accidental or malicious
    // shortening of the mandatory retention window.
    const QDateTime minimumPurgeDate =
        QDateTime::currentDateTimeUtc().addYears(-Validation::AuditRetentionYears);
    if (threshold > minimumPurgeDate)
        return Result<void>::err(ErrorCode::ValidationFailed,
            QStringLiteral("Retention purge threshold must be at least %1 years in the past "
                           "(minimum retention policy)")
                .arg(Validation::AuditRetentionYears));

    // Delegate to repository: records chain-anchor snapshot before deletion.
    return m_auditRepo.purgeEntriesOlderThan(threshold);
}

QString AuditService::encryptPayload(const QJsonObject& payload)
{
    if (payload.isEmpty())
        return QStringLiteral("{}");

    QJsonObject encrypted = payload;

    for (const QString& fieldName : s_piiFieldNames) {
        if (encrypted.contains(fieldName)) {
            QString plainValue = encrypted[fieldName].toString();
            if (!plainValue.isEmpty()) {
                auto result = m_cipher.encrypt(plainValue,
                    QStringLiteral("audit.%1").arg(fieldName).toUtf8());
                if (result.isOk()) {
                    encrypted[fieldName] = QString::fromLatin1(result.value().toBase64());
                } else {
                    // On encryption failure, redact the field entirely
                    encrypted[fieldName] = QStringLiteral("[ENCRYPTION_FAILED]");
                    Logger::instance().error(QStringLiteral("AuditService"),
                        QStringLiteral("Failed to encrypt PII field in audit payload"),
                        {{QStringLiteral("field"), fieldName}});
                }
            }
        }
    }

    return QString::fromUtf8(QJsonDocument(encrypted).toJson(QJsonDocument::Compact));
}
