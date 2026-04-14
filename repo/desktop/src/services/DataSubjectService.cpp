// DataSubjectService.cpp — ProctorOps

#include "DataSubjectService.h"

#include "AuthService.h"
#include "utils/Logger.h"
#include "utils/MaskingPolicy.h"
#include "models/CommonTypes.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QDateTime>

DataSubjectService::DataSubjectService(IAuditRepository& auditRepo,
                                        IMemberRepository& memberRepo,
                                        AuthService& authService,
                                        AuditService& auditService,
                                        AesGcmCipher& cipher)
    : m_auditRepo(auditRepo)
    , m_memberRepo(memberRepo)
    , m_authService(authService)
    , m_auditService(auditService)
    , m_cipher(cipher)
{}

// ── Export requests ────────────────────────────────────────────────────────────

Result<ExportRequest> DataSubjectService::createExportRequest(const QString& memberId,
                                                               const QString& rationale,
                                                               const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::FrontDeskOperator);
    if (authResult.isErr())
        return Result<ExportRequest>::err(authResult.errorCode(), authResult.errorMessage());

    if (rationale.trimmed().isEmpty())
        return Result<ExportRequest>::err(ErrorCode::ValidationFailed,
                                          QStringLiteral("Export request rationale is required"));

    // Validate member exists before creating the request (same pattern as deletion request)
    auto memberRes = m_memberRepo.findMemberById(memberId);
    if (!memberRes.isOk())
        return Result<ExportRequest>::err(memberRes.errorCode(),
                                          QStringLiteral("Member not found"));

    ExportRequest req;
    req.id              = QUuid::createUuid().toString(QUuid::WithoutBraces);
    req.memberId        = memberId;
    req.requesterUserId = actorUserId;
    req.status          = QStringLiteral("PENDING");
    req.rationale       = rationale;
    req.createdAt       = QDateTime::currentDateTimeUtc();

    auto res = m_auditRepo.insertExportRequest(req);
    if (!res.isOk())
        return Result<ExportRequest>::err(res.errorCode(), res.errorMessage());

    m_auditService.recordEvent(actorUserId,
                               AuditEventType::ExportRequested,
                               QStringLiteral("ExportRequest"), req.id,
                               {}, {
                                   {QStringLiteral("member_id"), memberId},
                                   {QStringLiteral("rationale"), rationale}
                               });

    Logger::instance().info(QStringLiteral("DataSubjectService"),
                             QStringLiteral("Data export request created"),
                             {{QStringLiteral("request_id"), req.id}});

    return res;
}

Result<ExportRequest> DataSubjectService::fulfillExportRequest(const QString& requestId,
                                                                const QString& outputFilePath,
                                                                const QString& actorUserId,
                                                                const QString& stepUpWindowId)
{
    auto authResult = m_authService.authorizePrivilegedAction(
        actorUserId,
        Role::SecurityAdministrator,
        stepUpWindowId);
    if (authResult.isErr())
        return Result<ExportRequest>::err(authResult.errorCode(), authResult.errorMessage());

    auto reqRes = m_auditRepo.getExportRequest(requestId);
    if (!reqRes.isOk())
        return Result<ExportRequest>::err(reqRes.errorCode(), reqRes.errorMessage());

    ExportRequest req = reqRes.value();
    if (req.status != QLatin1String("PENDING"))
        return Result<ExportRequest>::err(ErrorCode::InvalidState,
                                          QStringLiteral("Export request is not in PENDING state"));

    // Look up the member
    auto memberRes = m_memberRepo.findMemberById(req.memberId);
    if (!memberRes.isOk())
        return Result<ExportRequest>::err(memberRes.errorCode(),
                                          QStringLiteral("Member not found for export"));

    const Member& member = memberRes.value();

    // Decrypt PII for the export file
    // Masking: show all fields in the export (this is an authorized access request)
    // but mark the file as authorized export so it is not treated as a general printout.
    QString decryptedName;
    QString decryptedMobile;
    QString decryptedBarcode;

    if (!member.nameEncrypted.isEmpty()) {
        auto dec = m_cipher.decrypt(QByteArray::fromBase64(member.nameEncrypted.toLatin1()),
                                    QByteArrayLiteral("member.name"));
        if (dec.isOk()) decryptedName = dec.value();
    }
    if (!member.mobileEncrypted.isEmpty()) {
        auto dec = m_cipher.decrypt(QByteArray::fromBase64(member.mobileEncrypted.toLatin1()),
                                    QByteArrayLiteral("member.mobile"));
        if (dec.isOk()) decryptedMobile = dec.value();
    }
    if (!member.barcodeEncrypted.isEmpty()) {
        auto dec = m_cipher.decrypt(QByteArray::fromBase64(member.barcodeEncrypted.toLatin1()),
                                    QByteArrayLiteral("member.barcode"));
        if (dec.isOk()) decryptedBarcode = dec.value();
    }

    // Build export document
    QJsonObject exportDoc;
    exportDoc[QStringLiteral("export_type")]    = QStringLiteral("GDPR_SUBJECT_ACCESS");
    exportDoc[QStringLiteral("generated_at")]   = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    exportDoc[QStringLiteral("request_id")]     = requestId;
    exportDoc[QStringLiteral("generated_by")]   = actorUserId;
    exportDoc[QStringLiteral("WATERMARK")]      = QStringLiteral("AUTHORIZED_EXPORT_ONLY — NOT_FOR_REDISTRIBUTION");

    QJsonObject memberData;
    memberData[QStringLiteral("member_id")] = member.memberId;
    memberData[QStringLiteral("name")]      = decryptedName;
    memberData[QStringLiteral("mobile")]    = MaskingPolicy::maskMobile(decryptedMobile);
    memberData[QStringLiteral("barcode")]   = MaskingPolicy::maskBarcode(decryptedBarcode);
    memberData[QStringLiteral("active")]    = !member.deleted;
    exportDoc[QStringLiteral("member")]     = memberData;

    QFile out(outputFilePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return Result<ExportRequest>::err(ErrorCode::IoError,
                                          QStringLiteral("Cannot write export file: ") + outputFilePath);
    out.write(QJsonDocument(exportDoc).toJson(QJsonDocument::Indented));
    out.close();

    req.status         = QStringLiteral("COMPLETED");
    req.fulfilledAt    = QDateTime::currentDateTimeUtc();
    req.outputFilePath = outputFilePath;

    auto updRes = m_auditRepo.updateExportRequest(req);
    if (!updRes.isOk())
        return Result<ExportRequest>::err(updRes.errorCode(), updRes.errorMessage());

    m_auditService.recordEvent(actorUserId,
                               AuditEventType::ExportCompleted,
                               QStringLiteral("ExportRequest"), requestId,
                               {}, {
                                   {QStringLiteral("output_path"), outputFilePath}
                               });

    Logger::instance().info(QStringLiteral("DataSubjectService"),
                             QStringLiteral("Data export fulfilled"),
                             {{QStringLiteral("request_id"), requestId}});

    return Result<ExportRequest>::ok(req);
}

Result<void> DataSubjectService::rejectExportRequest(const QString& requestId,
                                                       const QString& actorUserId,
                                                       const QString& stepUpWindowId)
{
    auto authResult = m_authService.authorizePrivilegedAction(
        actorUserId,
        Role::SecurityAdministrator,
        stepUpWindowId);
    if (authResult.isErr())
        return Result<void>::err(authResult.errorCode(), authResult.errorMessage());

    auto reqRes = m_auditRepo.getExportRequest(requestId);
    if (!reqRes.isOk())
        return Result<void>::err(reqRes.errorCode(), reqRes.errorMessage());

    ExportRequest req = reqRes.value();
    if (req.status != QLatin1String("PENDING"))
        return Result<void>::err(ErrorCode::InvalidState,
                                  QStringLiteral("Only PENDING export requests can be rejected"));

    req.status = QStringLiteral("REJECTED");
    auto updRes = m_auditRepo.updateExportRequest(req);
    if (!updRes.isOk())
        return Result<void>::err(updRes.errorCode(), updRes.errorMessage());

    m_auditService.recordEvent(actorUserId,
                               AuditEventType::ExportCompleted,
                               QStringLiteral("ExportRequest"), requestId,
                               {}, {
                                   {QStringLiteral("outcome"), QStringLiteral("REJECTED")}
                               });

    return Result<void>::ok();
}

Result<QList<ExportRequest>> DataSubjectService::listExportRequests(const QString& actorUserId,
                                                                    const QString& statusFilter)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::SecurityAdministrator);
    if (authResult.isErr())
        return Result<QList<ExportRequest>>::err(authResult.errorCode(), authResult.errorMessage());

    return m_auditRepo.listExportRequests(statusFilter);
}

// ── Deletion requests ──────────────────────────────────────────────────────────

Result<DeletionRequest> DataSubjectService::createDeletionRequest(const QString& memberId,
                                                                    const QString& rationale,
                                                                    const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::FrontDeskOperator);
    if (authResult.isErr())
        return Result<DeletionRequest>::err(authResult.errorCode(), authResult.errorMessage());

    if (rationale.trimmed().isEmpty())
        return Result<DeletionRequest>::err(ErrorCode::ValidationFailed,
                                             QStringLiteral("Deletion request rationale is required"));

    // Check member exists
    auto memberRes = m_memberRepo.findMemberById(memberId);
    if (!memberRes.isOk())
        return Result<DeletionRequest>::err(memberRes.errorCode(),
                                             QStringLiteral("Member not found"));

    DeletionRequest req;
    req.id              = QUuid::createUuid().toString(QUuid::WithoutBraces);
    req.memberId        = memberId;
    req.requesterUserId = actorUserId;
    req.status          = QStringLiteral("PENDING");
    req.rationale       = rationale;
    req.createdAt       = QDateTime::currentDateTimeUtc();

    auto res = m_auditRepo.insertDeletionRequest(req);
    if (!res.isOk())
        return Result<DeletionRequest>::err(res.errorCode(), res.errorMessage());

    m_auditService.recordEvent(actorUserId,
                               AuditEventType::DeletionRequested,
                               QStringLiteral("DeletionRequest"), req.id,
                               {}, {
                                   {QStringLiteral("member_id"), memberId},
                                   {QStringLiteral("rationale"), rationale}
                               });

    Logger::instance().info(QStringLiteral("DataSubjectService"),
                             QStringLiteral("Deletion request created"),
                             {{QStringLiteral("request_id"), req.id}});

    return res;
}

Result<DeletionRequest> DataSubjectService::approveDeletionRequest(const QString& requestId,
                                                                     const QString& approverUserId,
                                                                     const QString& stepUpWindowId)
{
    auto authResult = m_authService.authorizePrivilegedAction(
        approverUserId,
        Role::SecurityAdministrator,
        stepUpWindowId);
    if (authResult.isErr())
        return Result<DeletionRequest>::err(authResult.errorCode(), authResult.errorMessage());

    auto reqRes = m_auditRepo.getDeletionRequest(requestId);
    if (!reqRes.isOk())
        return Result<DeletionRequest>::err(reqRes.errorCode(), reqRes.errorMessage());

    DeletionRequest req = reqRes.value();
    if (req.status != QLatin1String("PENDING"))
        return Result<DeletionRequest>::err(ErrorCode::InvalidState,
                                             QStringLiteral("Only PENDING deletion requests can be approved"));

    req.status         = QStringLiteral("APPROVED");
    req.approverUserId = approverUserId;
    req.approvedAt     = QDateTime::currentDateTimeUtc();

    auto updRes = m_auditRepo.updateDeletionRequest(req);
    if (!updRes.isOk())
        return Result<DeletionRequest>::err(updRes.errorCode(), updRes.errorMessage());

    m_auditService.recordEvent(approverUserId,
                               AuditEventType::DeletionApproved,
                               QStringLiteral("DeletionRequest"), requestId,
                               {}, {});

    return Result<DeletionRequest>::ok(req);
}

Result<void> DataSubjectService::completeDeletion(const QString& requestId,
                                                    const QString& actorUserId,
                                                    const QString& stepUpWindowId)
{
    auto authResult = m_authService.authorizePrivilegedAction(
        actorUserId,
        Role::SecurityAdministrator,
        stepUpWindowId);
    if (authResult.isErr())
        return Result<void>::err(authResult.errorCode(), authResult.errorMessage());

    auto reqRes = m_auditRepo.getDeletionRequest(requestId);
    if (!reqRes.isOk())
        return Result<void>::err(reqRes.errorCode(), reqRes.errorMessage());

    DeletionRequest req = reqRes.value();
    if (req.status != QLatin1String("APPROVED"))
        return Result<void>::err(ErrorCode::InvalidState,
                                  QStringLiteral("Deletion must be APPROVED before completion"));

    // Anonymize PII in the member record
    auto anonRes = m_memberRepo.anonymizeMember(req.memberId);
    if (!anonRes.isOk())
        return Result<void>::err(anonRes.errorCode(), anonRes.errorMessage());

    req.status          = QStringLiteral("COMPLETED");
    req.completedAt     = QDateTime::currentDateTimeUtc();
    req.fieldsAnonymized = {QStringLiteral("name"), QStringLiteral("mobile"),
                             QStringLiteral("barcode"), QStringLiteral("member_id")};

    auto updRes = m_auditRepo.updateDeletionRequest(req);
    if (!updRes.isOk())
        return Result<void>::err(updRes.errorCode(), updRes.errorMessage());

    m_auditService.recordEvent(actorUserId,
                               AuditEventType::DeletionCompleted,
                               QStringLiteral("DeletionRequest"), requestId,
                               {}, {
                                   {QStringLiteral("fields_anonymized"),
                                    req.fieldsAnonymized.join(QStringLiteral(","))}
                               });

    Logger::instance().info(QStringLiteral("DataSubjectService"),
                             QStringLiteral("Member PII anonymized"),
                             {{QStringLiteral("request_id"), requestId}});

    return Result<void>::ok();
}

Result<void> DataSubjectService::rejectDeletionRequest(const QString& requestId,
                                                         const QString& actorUserId,
                                                         const QString& stepUpWindowId)
{
    auto authResult = m_authService.authorizePrivilegedAction(
        actorUserId,
        Role::SecurityAdministrator,
        stepUpWindowId);
    if (authResult.isErr())
        return Result<void>::err(authResult.errorCode(), authResult.errorMessage());

    auto reqRes = m_auditRepo.getDeletionRequest(requestId);
    if (!reqRes.isOk())
        return Result<void>::err(reqRes.errorCode(), reqRes.errorMessage());

    DeletionRequest req = reqRes.value();
    if (req.status != QLatin1String("PENDING"))
        return Result<void>::err(ErrorCode::InvalidState,
                                  QStringLiteral("Only PENDING deletion requests can be rejected"));

    req.status = QStringLiteral("REJECTED");
    auto updRes = m_auditRepo.updateDeletionRequest(req);
    if (!updRes.isOk())
        return Result<void>::err(updRes.errorCode(), updRes.errorMessage());

    m_auditService.recordEvent(actorUserId,
                               AuditEventType::DeletionCompleted,
                               QStringLiteral("DeletionRequest"), requestId,
                               {}, {
                                   {QStringLiteral("outcome"), QStringLiteral("REJECTED")}
                               });

    return Result<void>::ok();
}

Result<QList<DeletionRequest>> DataSubjectService::listDeletionRequests(const QString& actorUserId,
                                                                        const QString& statusFilter)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::SecurityAdministrator);
    if (authResult.isErr())
        return Result<QList<DeletionRequest>>::err(authResult.errorCode(), authResult.errorMessage());

    return m_auditRepo.listDeletionRequests(statusFilter);
}
