// CheckInService.cpp — ProctorOps
// Entry validation engine with atomic punch-card deduction, 30-second duplicate
// suppression, term-card enforcement, freeze blocking, and correction workflow.

#include "CheckInService.h"
#include "AuthService.h"
#include "utils/Validation.h"
#include "utils/MaskingPolicy.h"
#include "utils/Logger.h"

#include <QUuid>
#include <QDateTime>
#include <QDate>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSqlQuery>

CheckInService::CheckInService(IMemberRepository& memberRepo,
                                 ICheckInRepository& checkInRepo,
                                 AuthService& authService,
                                 AuditService& auditService,
                                 AesGcmCipher& cipher,
                                 QSqlDatabase& db)
    : m_memberRepo(memberRepo)
    , m_checkInRepo(checkInRepo)
    , m_authService(authService)
    , m_auditService(auditService)
    , m_cipher(cipher)
    , m_db(db)
{
}

// ── Mobile normalization ────────────────────────────────────────────────────

QString CheckInService::normalizeMobile(const QString& input)
{
    // Strip all non-digit characters
    QString digits;
    for (const auto& ch : input) {
        if (ch.isDigit())
            digits.append(ch);
    }

    // Must be exactly 10 digits for US format
    if (digits.length() != 10)
        return {};

    // Format as (###) ###-####
    return QStringLiteral("(%1) %2-%3")
        .arg(digits.left(3), digits.mid(3, 3), digits.mid(6, 4));
}

// ── Member resolution ────────────────────────────────────────────────────────

Result<Member> CheckInService::resolveMember(const MemberIdentifier& identifier)
{
    switch (identifier.type) {
    case MemberIdentifier::Type::MemberId:
        return m_memberRepo.findMemberByMemberId(identifier.value);

    case MemberIdentifier::Type::Barcode:
        return m_memberRepo.findMemberByBarcode(identifier.value);

    case MemberIdentifier::Type::Mobile: {
        QString normalized = normalizeMobile(identifier.value);
        if (normalized.isEmpty())
            return Result<Member>::err(ErrorCode::ValidationFailed,
                                        QStringLiteral("Invalid mobile number format"));
        return m_memberRepo.findMemberByMobileNormalized(normalized);
    }
    }

    return Result<Member>::err(ErrorCode::ValidationFailed,
                                QStringLiteral("Unknown identifier type"));
}

// ── Failed attempt helper ────────────────────────────────────────────────────

Result<void> CheckInService::recordFailedAttempt(const QString& memberId,
                                                   const QString& sessionId,
                                                   const QString& operatorUserId,
                                                   CheckInStatus status,
                                                   const QString& failureReason,
                                                   AuditEventType auditEvent)
{
    CheckInAttempt attempt;
    attempt.id             = QUuid::createUuid().toString(QUuid::WithoutBraces);
    attempt.memberId       = memberId;
    attempt.sessionId      = sessionId;
    attempt.operatorUserId = operatorUserId;
    attempt.status         = status;
    attempt.attemptedAt    = QDateTime::currentDateTimeUtc();
    attempt.failureReason  = failureReason;

    auto insertResult = m_checkInRepo.insertAttempt(attempt);
    if (insertResult.isErr())
        Logger::instance().error(QStringLiteral("CheckInService"),
                                  QStringLiteral("Failed to record attempt"),
                                  {{QStringLiteral("error"), insertResult.errorMessage()}});

    QJsonObject payload;
    payload[QStringLiteral("member_id")] = memberId;
    payload[QStringLiteral("session_id")] = sessionId;
    payload[QStringLiteral("reason")] = failureReason;
    m_auditService.recordEvent(operatorUserId, auditEvent,
                                QStringLiteral("CheckIn"), attempt.id,
                                {}, payload);

    return Result<void>::ok();
}

// ── Check-in flow ────────────────────────────────────────────────────────────

Result<CheckInResult> CheckInService::checkIn(const MemberIdentifier& identifier,
                                                const QString& sessionId,
                                                const QString& operatorUserId,
                                                const QString& punchCardId)
{
    auto authResult = m_authService.requireRoleForActor(operatorUserId, Role::FrontDeskOperator);
    if (authResult.isErr())
        return Result<CheckInResult>::err(authResult.errorCode(), authResult.errorMessage());

    // Step 1: Resolve member
    auto memberResult = resolveMember(identifier);
    if (memberResult.isErr())
        return Result<CheckInResult>::err(memberResult.errorCode(),
                                           memberResult.errorMessage());

    const Member& member = memberResult.value();

    // Step 2: Check soft-delete
    if (member.deleted)
        return Result<CheckInResult>::err(ErrorCode::NotFound,
                                           QStringLiteral("Member account is deactivated"));

    // Step 3: Check active freeze
    auto freezeResult = m_memberRepo.getActiveFreezeRecord(member.id);
    if (freezeResult.isErr())
        return Result<CheckInResult>::err(freezeResult.errorCode(), freezeResult.errorMessage());

    if (freezeResult.value().has_value()) {
        recordFailedAttempt(member.id, sessionId, operatorUserId,
                             CheckInStatus::FrozenBlocked,
                             QStringLiteral("Member account is frozen"),
                             AuditEventType::CheckInFrozenBlocked);
        return Result<CheckInResult>::err(ErrorCode::AccountFrozen,
                                           QStringLiteral("Member account is frozen"));
    }

    // Step 4: Check term cards
    QDate today = QDate::currentDate();
    auto termResult = m_memberRepo.getActiveTermCards(member.id, today);
    if (termResult.isErr())
        return Result<CheckInResult>::err(termResult.errorCode(), termResult.errorMessage());

    if (termResult.value().isEmpty()) {
        // Check if they have any term cards at all
        auto allTerms = m_memberRepo.getAllTermCards(member.id);
        bool hasAnyCards = allTerms.isOk() && !allTerms.value().isEmpty();

        if (hasAnyCards) {
            recordFailedAttempt(member.id, sessionId, operatorUserId,
                                 CheckInStatus::TermCardExpired,
                                 QStringLiteral("Term card expired"),
                                 AuditEventType::CheckInExpiredBlocked);
            return Result<CheckInResult>::err(ErrorCode::TermCardExpired,
                                               QStringLiteral("Term card expired"));
        } else {
            recordFailedAttempt(member.id, sessionId, operatorUserId,
                                 CheckInStatus::TermCardMissing,
                                 QStringLiteral("No term card on file"),
                                 AuditEventType::CheckInTermCardInvalid);
            return Result<CheckInResult>::err(ErrorCode::TermCardMissing,
                                               QStringLiteral("No term card on file"));
        }
    }

    // Step 5: Check 30-second duplicate window
    QDateTime since = QDateTime::currentDateTimeUtc()
                        .addSecs(-Validation::DuplicateWindowSeconds);
    auto dupResult = m_checkInRepo.findRecentSuccess(member.id, sessionId, since);
    if (dupResult.isErr())
        return Result<CheckInResult>::err(dupResult.errorCode(), dupResult.errorMessage());

    if (dupResult.value().has_value()) {
        recordFailedAttempt(member.id, sessionId, operatorUserId,
                             CheckInStatus::DuplicateBlocked,
                             QStringLiteral("Duplicate check-in within 30-second window"),
                             AuditEventType::CheckInDuplicateBlocked);
        return Result<CheckInResult>::err(ErrorCode::DuplicateCheckIn,
                                           QStringLiteral("Duplicate check-in blocked"));
    }

    // Step 6: Find active punch card
    PunchCard punchCard;
    if (!punchCardId.isEmpty()) {
        auto pcResult = m_memberRepo.getPunchCard(punchCardId);
        if (pcResult.isErr())
            return Result<CheckInResult>::err(pcResult.errorCode(), pcResult.errorMessage());
        punchCard = pcResult.value();
        if (punchCard.currentBalance <= 0) {
            recordFailedAttempt(member.id, sessionId, operatorUserId,
                                 CheckInStatus::PunchCardExhausted,
                                 QStringLiteral("Punch card balance is zero"),
                                 AuditEventType::CheckInPunchCardExhausted);
            return Result<CheckInResult>::err(ErrorCode::PunchCardExhausted,
                                               QStringLiteral("Punch card exhausted"));
        }
    } else {
        auto activePCs = m_memberRepo.getActivePunchCards(member.id);
        if (activePCs.isErr())
            return Result<CheckInResult>::err(activePCs.errorCode(), activePCs.errorMessage());
        if (activePCs.value().isEmpty()) {
            recordFailedAttempt(member.id, sessionId, operatorUserId,
                                 CheckInStatus::PunchCardExhausted,
                                 QStringLiteral("No active punch card with balance"),
                                 AuditEventType::CheckInPunchCardExhausted);
            return Result<CheckInResult>::err(ErrorCode::PunchCardExhausted,
                                               QStringLiteral("No active punch card"));
        }
        punchCard = activePCs.value().first();
    }

    // Step 7: Atomic transaction — deduct, record deduction, record attempt
    int balanceBefore = punchCard.currentBalance;

    // Ensure deterministic duplicate guard table exists across all schemas used by tests.
    QSqlQuery ensureGuard(m_db);
    if (!ensureGuard.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS checkin_duplicate_guards ("
            "member_id TEXT NOT NULL,"
            "session_id TEXT NOT NULL,"
            "last_success_at TEXT NOT NULL,"
            "PRIMARY KEY(member_id, session_id)"
            ")"))) {
        return Result<CheckInResult>::err(ErrorCode::DbError,
                                           QStringLiteral("Failed to initialize duplicate guard table"));
    }

    QSqlQuery beginTx(m_db);
    if (!beginTx.exec(QStringLiteral("BEGIN IMMEDIATE TRANSACTION")))
        return Result<CheckInResult>::err(ErrorCode::DbError,
                                           QStringLiteral("Failed to begin transaction"));

    // Re-check duplicate after write lock acquisition to prevent race duplicates.
    auto txDupResult = m_checkInRepo.findRecentSuccess(member.id, sessionId, since);
    if (txDupResult.isErr()) {
        m_db.rollback();
        return Result<CheckInResult>::err(txDupResult.errorCode(), txDupResult.errorMessage());
    }

    if (txDupResult.value().has_value()) {
        m_db.rollback();
        recordFailedAttempt(member.id, sessionId, operatorUserId,
                             CheckInStatus::DuplicateBlocked,
                             QStringLiteral("Duplicate check-in within 30-second window"),
                             AuditEventType::CheckInDuplicateBlocked);
        return Result<CheckInResult>::err(ErrorCode::DuplicateCheckIn,
                                           QStringLiteral("Duplicate check-in blocked"));
    }

    // Deterministic DB-side guard: allow one success per member/session within
    // the rolling duplicate window. Transaction rollback reverts guard updates.
    const QString nowIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QString thresholdIso = QDateTime::currentDateTimeUtc()
        .addSecs(-Validation::DuplicateWindowSeconds)
        .toString(Qt::ISODateWithMs);

    QSqlQuery guardUpsert(m_db);
    guardUpsert.prepare(QStringLiteral(
        "INSERT INTO checkin_duplicate_guards (member_id, session_id, last_success_at) "
        "VALUES (?, ?, ?) "
        "ON CONFLICT(member_id, session_id) DO UPDATE SET last_success_at = excluded.last_success_at "
        "WHERE checkin_duplicate_guards.last_success_at < ?"));
    guardUpsert.addBindValue(member.id);
    guardUpsert.addBindValue(sessionId);
    guardUpsert.addBindValue(nowIso);
    guardUpsert.addBindValue(thresholdIso);
    if (!guardUpsert.exec()) {
        m_db.rollback();
        return Result<CheckInResult>::err(ErrorCode::DbError,
                                           QStringLiteral("Failed to update duplicate guard"));
    }

    QSqlQuery changesQuery(m_db);
    if (!changesQuery.exec(QStringLiteral("SELECT changes()")) || !changesQuery.next()) {
        m_db.rollback();
        return Result<CheckInResult>::err(ErrorCode::DbError,
                                           QStringLiteral("Failed to evaluate duplicate guard result"));
    }
    const int guardChanged = changesQuery.value(0).toInt();
    if (guardChanged == 0) {
        m_db.rollback();
        recordFailedAttempt(member.id, sessionId, operatorUserId,
                             CheckInStatus::DuplicateBlocked,
                             QStringLiteral("Duplicate check-in within 30-second window"),
                             AuditEventType::CheckInDuplicateBlocked);
        return Result<CheckInResult>::err(ErrorCode::DuplicateCheckIn,
                                           QStringLiteral("Duplicate check-in blocked"));
    }

    // Deduct session
    auto deductResult = m_memberRepo.deductSession(punchCard.id);
    if (deductResult.isErr()) {
        m_db.rollback();
        return Result<CheckInResult>::err(deductResult.errorCode(),
                                           deductResult.errorMessage());
    }

    int balanceAfter = deductResult.value().currentBalance;

    // Create IDs
    QString attemptId   = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString deductionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDateTime now       = QDateTime::currentDateTimeUtc();

    // Record successful check-in attempt first so deduction FK can reference it.
    CheckInAttempt attempt;
    attempt.id               = attemptId;
    attempt.memberId         = member.id;
    attempt.sessionId        = sessionId;
    attempt.operatorUserId   = operatorUserId;
    attempt.status           = CheckInStatus::Success;
    attempt.attemptedAt      = now;
    attempt.deductionEventId.clear();

    auto attInsert = m_checkInRepo.insertAttempt(attempt);
    if (attInsert.isErr()) {
        m_db.rollback();
        return Result<CheckInResult>::err(attInsert.errorCode(), attInsert.errorMessage());
    }

    // Record deduction event
    DeductionEvent deduction;
    deduction.id                    = deductionId;
    deduction.memberId              = member.id;
    deduction.punchCardId           = punchCard.id;
    deduction.checkInAttemptId      = attemptId;
    deduction.sessionsDeducted      = 1;
    deduction.balanceBefore         = balanceBefore;
    deduction.balanceAfter          = balanceAfter;
    deduction.deductedAt            = now;

    auto dedInsert = m_checkInRepo.insertDeduction(deduction);
    if (dedInsert.isErr()) {
        m_db.rollback();
        return Result<CheckInResult>::err(dedInsert.errorCode(), dedInsert.errorMessage());
    }

    QSqlQuery linkAttempt(m_db);
    linkAttempt.prepare(QStringLiteral(
        "UPDATE checkin_attempts SET deduction_event_id = ? WHERE id = ?"));
    linkAttempt.addBindValue(deductionId);
    linkAttempt.addBindValue(attemptId);
    if (!linkAttempt.exec()) {
        m_db.rollback();
        return Result<CheckInResult>::err(ErrorCode::DbError,
                                           QStringLiteral("Failed to link deduction to attempt"));
    }

    if (!m_db.commit()) {
        m_db.rollback();
        return Result<CheckInResult>::err(ErrorCode::DbError,
                                           QStringLiteral("Failed to commit transaction"));
    }

    // Audit events (outside transaction — audit has its own chain)
    QJsonObject dedPayload;
    dedPayload[QStringLiteral("deduction_id")] = deductionId;
    dedPayload[QStringLiteral("member_id")] = member.id;
    dedPayload[QStringLiteral("punch_card_id")] = punchCard.id;
    dedPayload[QStringLiteral("balance_before")] = balanceBefore;
    dedPayload[QStringLiteral("balance_after")] = balanceAfter;
    m_auditService.recordEvent(operatorUserId, AuditEventType::DeductionCreated,
                                QStringLiteral("DeductionEvent"), deductionId,
                                {}, dedPayload);

    QJsonObject ciPayload;
    ciPayload[QStringLiteral("attempt_id")] = attemptId;
    ciPayload[QStringLiteral("member_id")] = member.id;
    ciPayload[QStringLiteral("session_id")] = sessionId;
    m_auditService.recordEvent(operatorUserId, AuditEventType::CheckInSuccess,
                                QStringLiteral("CheckIn"), attemptId,
                                {}, ciPayload);

    // Build result
    // Decrypt name for masking
    QString maskedName;
    if (!member.nameEncrypted.isEmpty()) {
        auto nameBytes = QByteArray::fromBase64(member.nameEncrypted.toLatin1());
        auto decResult = m_cipher.decrypt(nameBytes, QByteArrayLiteral("member.name"));
        if (decResult.isOk())
            maskedName = MaskingPolicy::maskName(decResult.value());
        else
            maskedName = QStringLiteral("***");
    }

    CheckInResult result;
    result.memberId         = member.id;
    result.memberNameMasked = maskedName;
    result.sessionId        = sessionId;
    result.deductionEventId = deductionId;
    result.remainingBalance = balanceAfter;
    result.checkInTimestamp  = now;

    Logger::instance().info(QStringLiteral("CheckInService"),
                             QStringLiteral("Check-in success"),
                             {{QStringLiteral("member_id"), member.id},
                              {QStringLiteral("session_id"), sessionId},
                              {QStringLiteral("remaining"), balanceAfter}});

    return Result<CheckInResult>::ok(std::move(result));
}

// ── Correction workflow ──────────────────────────────────────────────────────

Result<CorrectionRequest> CheckInService::requestCorrection(
    const QString& deductionEventId,
    const QString& rationale,
    const QString& requestedByUserId)
{
    auto authResult = m_authService.requireRoleForActor(requestedByUserId, Role::FrontDeskOperator);
    if (authResult.isErr())
        return Result<CorrectionRequest>::err(authResult.errorCode(), authResult.errorMessage());

    // Validate deduction exists
    auto dedResult = m_checkInRepo.getDeduction(deductionEventId);
    if (dedResult.isErr())
        return Result<CorrectionRequest>::err(dedResult.errorCode(), dedResult.errorMessage());

    // Check not already reversed
    if (!dedResult.value().reversedByCorrectionId.isEmpty())
        return Result<CorrectionRequest>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Deduction already reversed by correction"));

    CorrectionRequest request;
    request.id                = QUuid::createUuid().toString(QUuid::WithoutBraces);
    request.deductionEventId  = deductionEventId;
    request.requestedByUserId = requestedByUserId;
    request.rationale         = rationale;
    request.status            = CorrectionStatus::Pending;
    request.createdAt         = QDateTime::currentDateTimeUtc();

    auto result = m_checkInRepo.insertCorrectionRequest(request);
    if (result.isErr())
        return result;

    QJsonObject payload;
    payload[QStringLiteral("correction_id")] = request.id;
    payload[QStringLiteral("deduction_event_id")] = deductionEventId;
    payload[QStringLiteral("rationale")] = rationale;
    m_auditService.recordEvent(requestedByUserId, AuditEventType::CorrectionRequested,
                                QStringLiteral("CorrectionRequest"), request.id,
                                {}, payload);

    return result;
}

Result<void> CheckInService::approveCorrection(
    const QString& correctionRequestId,
    const QString& rationale,
    const QString& approvedByUserId,
    const QString& stepUpWindowId)
{
    auto authResult = m_authService.authorizePrivilegedAction(
        approvedByUserId,
        Role::SecurityAdministrator,
        stepUpWindowId);
    if (authResult.isErr())
        return authResult;

    // Validate request is Pending
    auto reqResult = m_checkInRepo.getCorrectionRequest(correctionRequestId);
    if (reqResult.isErr())
        return Result<void>::err(reqResult.errorCode(), reqResult.errorMessage());

    if (reqResult.value().status != CorrectionStatus::Pending)
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("Correction is not in Pending status"));

    // Get deduction for before snapshot
    auto dedResult = m_checkInRepo.getDeduction(reqResult.value().deductionEventId);
    if (dedResult.isErr())
        return Result<void>::err(dedResult.errorCode(), dedResult.errorMessage());

    // Get punch card for after snapshot
    auto pcResult = m_memberRepo.getPunchCard(dedResult.value().punchCardId);
    if (pcResult.isErr())
        return Result<void>::err(pcResult.errorCode(), pcResult.errorMessage());

    // Build before/after snapshots
    QJsonObject before;
    before[QStringLiteral("deduction_id")] = dedResult.value().id;
    before[QStringLiteral("balance_before")] = dedResult.value().balanceBefore;
    before[QStringLiteral("balance_after")] = dedResult.value().balanceAfter;

    QJsonObject after;
    after[QStringLiteral("punch_card_id")] = pcResult.value().id;
    after[QStringLiteral("current_balance")] = pcResult.value().currentBalance;
    after[QStringLiteral("will_restore_to")] = pcResult.value().currentBalance + 1;

    CorrectionApproval approval;
    approval.correctionRequestId = correctionRequestId;
    approval.approvedByUserId    = approvedByUserId;
    approval.stepUpWindowId      = stepUpWindowId;
    approval.rationale           = rationale;
    approval.approvedAt          = QDateTime::currentDateTimeUtc();
    approval.beforePayloadJson   = QString::fromUtf8(
        QJsonDocument(before).toJson(QJsonDocument::Compact));
    approval.afterPayloadJson    = QString::fromUtf8(
        QJsonDocument(after).toJson(QJsonDocument::Compact));

    if (!m_db.transaction()) {
        return Result<void>::err(
            ErrorCode::DbError,
            QStringLiteral("Failed to begin correction approval transaction"));
    }

    auto insertResult = m_checkInRepo.insertCorrectionApproval(approval);
    if (insertResult.isErr()) {
        m_db.rollback();
        return Result<void>::err(insertResult.errorCode(), insertResult.errorMessage());
    }

    // Update status to Approved
    auto statusResult = m_checkInRepo.updateCorrectionStatus(
        correctionRequestId, CorrectionStatus::Approved);
    if (statusResult.isErr()) {
        m_db.rollback();
        return statusResult;
    }

    if (!m_db.commit()) {
        m_db.rollback();
        return Result<void>::err(
            ErrorCode::DbError,
            QStringLiteral("Failed to commit correction approval transaction"));
    }

    QJsonObject auditPayload;
    auditPayload[QStringLiteral("correction_id")] = correctionRequestId;
    auditPayload[QStringLiteral("approved_by")] = approvedByUserId;
    auditPayload[QStringLiteral("step_up_id")] = stepUpWindowId;
    m_auditService.recordEvent(approvedByUserId, AuditEventType::CorrectionApproved,
                                QStringLiteral("CorrectionRequest"), correctionRequestId,
                                before, after);

    return Result<void>::ok();
}

Result<void> CheckInService::applyCorrection(const QString& correctionRequestId,
                                              const QString& actorUserId,
                                              const QString& stepUpWindowId)
{
    auto authResult = m_authService.authorizePrivilegedAction(
        actorUserId,
        Role::SecurityAdministrator,
        stepUpWindowId);
    if (authResult.isErr())
        return authResult;

    // Validate status is Approved
    auto reqResult = m_checkInRepo.getCorrectionRequest(correctionRequestId);
    if (reqResult.isErr())
        return Result<void>::err(reqResult.errorCode(), reqResult.errorMessage());

    if (reqResult.value().status != CorrectionStatus::Approved)
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("Correction is not in Approved status"));

    // Get the deduction
    auto dedResult = m_checkInRepo.getDeduction(reqResult.value().deductionEventId);
    if (dedResult.isErr())
        return Result<void>::err(dedResult.errorCode(), dedResult.errorMessage());

    if (!m_db.transaction()) {
        return Result<void>::err(
            ErrorCode::DbError,
            QStringLiteral("Failed to begin correction apply transaction"));
    }

    // Restore punch card balance
    auto restoreResult = m_memberRepo.restoreSession(dedResult.value().punchCardId);
    if (restoreResult.isErr()) {
        m_db.rollback();
        return restoreResult;
    }

    // Mark deduction as reversed
    auto reverseResult = m_checkInRepo.setDeductionReversed(
        dedResult.value().id, correctionRequestId);
    if (reverseResult.isErr()) {
        m_db.rollback();
        return reverseResult;
    }

    // Update status to Applied
    auto statusResult = m_checkInRepo.updateCorrectionStatus(
        correctionRequestId, CorrectionStatus::Applied);
    if (statusResult.isErr()) {
        m_db.rollback();
        return statusResult;
    }

    if (!m_db.commit()) {
        m_db.rollback();
        return Result<void>::err(
            ErrorCode::DbError,
            QStringLiteral("Failed to commit correction apply transaction"));
    }

    QJsonObject payload;
    payload[QStringLiteral("correction_id")] = correctionRequestId;
    payload[QStringLiteral("deduction_id")] = dedResult.value().id;
    payload[QStringLiteral("punch_card_id")] = dedResult.value().punchCardId;
    m_auditService.recordEvent(actorUserId,
                                AuditEventType::CorrectionApplied,
                                QStringLiteral("CorrectionRequest"), correctionRequestId,
                                {}, payload);

    Logger::instance().info(QStringLiteral("CheckInService"),
                             QStringLiteral("Correction applied"),
                             {{QStringLiteral("correction_id"), correctionRequestId}});

    return Result<void>::ok();
}

Result<void> CheckInService::rejectCorrection(
    const QString& correctionRequestId,
    const QString& rationale,
    const QString& rejectedByUserId,
    const QString& stepUpWindowId)
{
    auto authResult = m_authService.authorizePrivilegedAction(
        rejectedByUserId,
        Role::SecurityAdministrator,
        stepUpWindowId);
    if (authResult.isErr())
        return authResult;

    auto reqResult = m_checkInRepo.getCorrectionRequest(correctionRequestId);
    if (reqResult.isErr())
        return Result<void>::err(reqResult.errorCode(), reqResult.errorMessage());

    if (reqResult.value().status != CorrectionStatus::Pending)
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("Correction is not in Pending status"));

    auto statusResult = m_checkInRepo.updateCorrectionStatus(
        correctionRequestId, CorrectionStatus::Rejected);
    if (statusResult.isErr())
        return statusResult;

    QJsonObject payload;
    payload[QStringLiteral("correction_id")] = correctionRequestId;
    payload[QStringLiteral("rationale")] = rationale;
    m_auditService.recordEvent(rejectedByUserId, AuditEventType::CorrectionRejected,
                                QStringLiteral("CorrectionRequest"), correctionRequestId,
                                {}, payload);

    return Result<void>::ok();
}

Result<QList<CorrectionRequest>> CheckInService::listPendingCorrections()
{
    return m_checkInRepo.listCorrectionRequests(CorrectionStatus::Pending);
}

Result<void> CheckInService::freezeMemberAccount(
    const QString& memberHumanId,
    const QString& reason,
    const QString& actorUserId,
    const QString& stepUpWindowId)
{
    if (memberHumanId.trimmed().isEmpty() || reason.trimmed().isEmpty()) {
        return Result<void>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Member ID and freeze rationale are required"));
    }

    auto authResult = m_authService.authorizePrivilegedAction(
        actorUserId,
        Role::SecurityAdministrator,
        stepUpWindowId);
    if (authResult.isErr())
        return authResult;

    auto memberResult = m_memberRepo.findMemberByMemberId(memberHumanId.trimmed());
    if (memberResult.isErr())
        return Result<void>::err(memberResult.errorCode(), memberResult.errorMessage());

    auto freezeResult = m_memberRepo.getActiveFreezeRecord(memberResult.value().id);
    if (freezeResult.isErr())
        return Result<void>::err(freezeResult.errorCode(), freezeResult.errorMessage());

    if (freezeResult.value().has_value()) {
        return Result<void>::err(
            ErrorCode::InvalidState,
            QStringLiteral("Member already has an active freeze"));
    }

    MemberFreezeRecord record;
    record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.memberId = memberResult.value().id;
    record.reason = reason.trimmed();
    record.frozenByUserId = actorUserId;
    record.frozenAt = QDateTime::currentDateTimeUtc();

    auto insertResult = m_memberRepo.insertFreezeRecord(record);
    if (insertResult.isErr())
        return Result<void>::err(insertResult.errorCode(), insertResult.errorMessage());

    QJsonObject payload;
    payload[QStringLiteral("member_id")] = memberResult.value().memberId;
    payload[QStringLiteral("reason")] = record.reason;
    m_auditService.recordEvent(actorUserId,
                                AuditEventType::MemberFreezeApplied,
                                QStringLiteral("Member"),
                                memberResult.value().id,
                                {},
                                payload);

    return Result<void>::ok();
}

Result<void> CheckInService::thawMemberAccount(
    const QString& memberHumanId,
    const QString& actorUserId,
    const QString& stepUpWindowId)
{
    if (memberHumanId.trimmed().isEmpty()) {
        return Result<void>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Member ID is required"));
    }

    auto authResult = m_authService.authorizePrivilegedAction(
        actorUserId,
        Role::SecurityAdministrator,
        stepUpWindowId);
    if (authResult.isErr())
        return authResult;

    auto memberResult = m_memberRepo.findMemberByMemberId(memberHumanId.trimmed());
    if (memberResult.isErr())
        return Result<void>::err(memberResult.errorCode(), memberResult.errorMessage());

    auto thawResult = m_memberRepo.thawMember(memberResult.value().id, actorUserId);
    if (thawResult.isErr())
        return Result<void>::err(thawResult.errorCode(), thawResult.errorMessage());

    QJsonObject payload;
    payload[QStringLiteral("member_id")] = memberResult.value().memberId;
    m_auditService.recordEvent(actorUserId,
                                AuditEventType::MemberFreezeThawed,
                                QStringLiteral("Member"),
                                memberResult.value().id,
                                {},
                                payload);

    return Result<void>::ok();
}

Result<QList<FreezeRecordView>> CheckInService::listRecentFreezeRecords(
    const QString& actorUserId,
    int limit)
{
    auto authResult = m_authService.requireRoleForActor(
        actorUserId,
        Role::SecurityAdministrator);
    if (authResult.isErr())
        return Result<QList<FreezeRecordView>>::err(authResult.errorCode(), authResult.errorMessage());

    auto recordsResult = m_memberRepo.listRecentFreezeRecords(limit);
    if (recordsResult.isErr())
        return Result<QList<FreezeRecordView>>::err(recordsResult.errorCode(), recordsResult.errorMessage());

    QList<FreezeRecordView> views;
    views.reserve(recordsResult.value().size());

    for (const MemberFreezeRecord& record : recordsResult.value()) {
        FreezeRecordView view;
        view.memberId = record.memberId;
        view.reason = record.reason;
        view.frozenAt = record.frozenAt;
        view.thawedAt = record.thawedAt;

        auto memberResult = m_memberRepo.findMemberById(record.memberId);
        if (memberResult.isOk())
            view.memberId = memberResult.value().memberId;

        views.push_back(view);
    }

    return Result<QList<FreezeRecordView>>::ok(views);
}
