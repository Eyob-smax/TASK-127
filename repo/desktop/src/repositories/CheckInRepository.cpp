// CheckInRepository.cpp — ProctorOps
// Concrete SQLite implementation for check-in attempts, deduction events,
// and correction workflows.

#include "CheckInRepository.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <algorithm>

CheckInRepository::CheckInRepository(QSqlDatabase& db)
    : m_db(db)
{
}

// ── Enum string conversions ──────────────────────────────────────────────────

QString CheckInRepository::checkInStatusToString(CheckInStatus s)
{
    switch (s) {
    case CheckInStatus::Success:            return QStringLiteral("Success");
    case CheckInStatus::DuplicateBlocked:   return QStringLiteral("DuplicateBlocked");
    case CheckInStatus::FrozenBlocked:      return QStringLiteral("FrozenBlocked");
    case CheckInStatus::TermCardExpired:    return QStringLiteral("TermCardExpired");
    case CheckInStatus::TermCardMissing:    return QStringLiteral("TermCardMissing");
    case CheckInStatus::PunchCardExhausted: return QStringLiteral("PunchCardExhausted");
    case CheckInStatus::Failed:             return QStringLiteral("Failed");
    }
    return QStringLiteral("Failed");
}

CheckInStatus CheckInRepository::checkInStatusFromString(const QString& s)
{
    if (s == QStringLiteral("Success"))            return CheckInStatus::Success;
    if (s == QStringLiteral("DuplicateBlocked"))   return CheckInStatus::DuplicateBlocked;
    if (s == QStringLiteral("FrozenBlocked"))      return CheckInStatus::FrozenBlocked;
    if (s == QStringLiteral("TermCardExpired"))     return CheckInStatus::TermCardExpired;
    if (s == QStringLiteral("TermCardMissing"))     return CheckInStatus::TermCardMissing;
    if (s == QStringLiteral("PunchCardExhausted")) return CheckInStatus::PunchCardExhausted;
    return CheckInStatus::Failed;
}

QString CheckInRepository::correctionStatusToString(CorrectionStatus s)
{
    switch (s) {
    case CorrectionStatus::Pending:  return QStringLiteral("Pending");
    case CorrectionStatus::Approved: return QStringLiteral("Approved");
    case CorrectionStatus::Applied:  return QStringLiteral("Applied");
    case CorrectionStatus::Rejected: return QStringLiteral("Rejected");
    }
    return QStringLiteral("Pending");
}

CorrectionStatus CheckInRepository::correctionStatusFromString(const QString& s)
{
    if (s == QStringLiteral("Approved")) return CorrectionStatus::Approved;
    if (s == QStringLiteral("Applied"))  return CorrectionStatus::Applied;
    if (s == QStringLiteral("Rejected")) return CorrectionStatus::Rejected;
    return CorrectionStatus::Pending;
}

// ── Row mapping helpers ──────────────────────────────────────────────────────

CheckInAttempt CheckInRepository::rowToAttempt(const QSqlQuery& q)
{
    CheckInAttempt a;
    a.id               = q.value(0).toString();
    a.memberId         = q.value(1).toString();
    a.sessionId        = q.value(2).toString();
    a.operatorUserId   = q.value(3).toString();
    a.status           = checkInStatusFromString(q.value(4).toString());
    a.attemptedAt      = QDateTime::fromString(q.value(5).toString(), Qt::ISODateWithMs);
    a.deductionEventId = q.value(6).toString();
    a.failureReason    = q.value(7).toString();
    return a;
}

DeductionEvent CheckInRepository::rowToDeduction(const QSqlQuery& q)
{
    DeductionEvent e;
    e.id                    = q.value(0).toString();
    e.memberId              = q.value(1).toString();
    e.punchCardId           = q.value(2).toString();
    e.checkInAttemptId      = q.value(3).toString();
    e.sessionsDeducted      = q.value(4).toInt();
    e.balanceBefore         = q.value(5).toInt();
    e.balanceAfter          = q.value(6).toInt();
    e.deductedAt            = QDateTime::fromString(q.value(7).toString(), Qt::ISODateWithMs);
    e.reversedByCorrectionId = q.value(8).toString();
    return e;
}

CorrectionRequest CheckInRepository::rowToCorrectionRequest(const QSqlQuery& q)
{
    CorrectionRequest r;
    r.id                = q.value(0).toString();
    r.deductionEventId  = q.value(1).toString();
    r.requestedByUserId = q.value(2).toString();
    r.rationale         = q.value(3).toString();
    r.status            = correctionStatusFromString(q.value(4).toString());
    r.createdAt         = QDateTime::fromString(q.value(5).toString(), Qt::ISODateWithMs);
    return r;
}

// ── Check-in attempts ────────────────────────────────────────────────────────

Result<CheckInAttempt> CheckInRepository::insertAttempt(const CheckInAttempt& attempt)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO checkin_attempts "
        "(id, member_id, session_id, operator_user_id, status, attempted_at, "
        " deduction_event_id, failure_reason) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(attempt.id);
    q.addBindValue(attempt.memberId);
    q.addBindValue(attempt.sessionId);
    q.addBindValue(attempt.operatorUserId);
    q.addBindValue(checkInStatusToString(attempt.status));
    q.addBindValue(attempt.attemptedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(attempt.deductionEventId.isEmpty() ? QVariant() : attempt.deductionEventId);
    q.addBindValue(attempt.failureReason.isEmpty() ? QVariant() : attempt.failureReason);

    if (!q.exec())
        return Result<CheckInAttempt>::err(ErrorCode::DbError, q.lastError().text());

    return Result<CheckInAttempt>::ok(attempt);
}

Result<CheckInAttempt> CheckInRepository::getAttempt(const QString& id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, member_id, session_id, operator_user_id, status, "
        "       attempted_at, deduction_event_id, failure_reason "
        "FROM checkin_attempts WHERE id = ?"));
    q.addBindValue(id);

    if (!q.exec())
        return Result<CheckInAttempt>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<CheckInAttempt>::err(ErrorCode::NotFound);

    return Result<CheckInAttempt>::ok(rowToAttempt(q));
}

Result<std::optional<CheckInAttempt>>
CheckInRepository::findRecentSuccess(const QString& memberId,
                                       const QString& sessionId,
                                       const QDateTime& since)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, member_id, session_id, operator_user_id, status, "
        "       attempted_at, deduction_event_id, failure_reason "
        "FROM checkin_attempts "
        "WHERE member_id = ? AND session_id = ? AND status = 'Success' "
        "  AND attempted_at >= ? "
        "ORDER BY attempted_at DESC LIMIT 1"));
    q.addBindValue(memberId);
    q.addBindValue(sessionId);
    q.addBindValue(since.toString(Qt::ISODateWithMs));

    if (!q.exec())
        return Result<std::optional<CheckInAttempt>>::err(
            ErrorCode::DbError, q.lastError().text());

    if (!q.next())
        return Result<std::optional<CheckInAttempt>>::ok(std::nullopt);

    return Result<std::optional<CheckInAttempt>>::ok(rowToAttempt(q));
}

// ── Deduction events ─────────────────────────────────────────────────────────

Result<DeductionEvent> CheckInRepository::insertDeduction(const DeductionEvent& ev)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO deduction_events "
        "(id, member_id, punch_card_id, checkin_attempt_id, sessions_deducted, "
        " balance_before, balance_after, deducted_at, reversed_by_correction_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(ev.id);
    q.addBindValue(ev.memberId);
    q.addBindValue(ev.punchCardId);
    q.addBindValue(ev.checkInAttemptId);
    q.addBindValue(ev.sessionsDeducted);
    q.addBindValue(ev.balanceBefore);
    q.addBindValue(ev.balanceAfter);
    q.addBindValue(ev.deductedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(ev.reversedByCorrectionId.isEmpty()
                     ? QVariant() : ev.reversedByCorrectionId);

    if (!q.exec())
        return Result<DeductionEvent>::err(ErrorCode::DbError, q.lastError().text());

    return Result<DeductionEvent>::ok(ev);
}

Result<DeductionEvent> CheckInRepository::getDeduction(const QString& id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, member_id, punch_card_id, checkin_attempt_id, sessions_deducted, "
        "       balance_before, balance_after, deducted_at, reversed_by_correction_id "
        "FROM deduction_events WHERE id = ?"));
    q.addBindValue(id);

    if (!q.exec())
        return Result<DeductionEvent>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<DeductionEvent>::err(ErrorCode::NotFound);

    return Result<DeductionEvent>::ok(rowToDeduction(q));
}

Result<QList<QJsonObject>> CheckInRepository::listDeductionDelta(const QDateTime& since)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT de.id, de.member_id, ci.session_id, de.punch_card_id, "
        "       de.checkin_attempt_id, de.sessions_deducted, de.balance_before, "
        "       de.balance_after, de.deducted_at, de.reversed_by_correction_id "
        "FROM deduction_events de "
        "JOIN checkin_attempts ci ON ci.id = de.checkin_attempt_id "
        "WHERE de.deducted_at >= ? "
        "ORDER BY de.deducted_at ASC"));
    q.addBindValue(since.toString(Qt::ISODateWithMs));

    if (!q.exec())
        return Result<QList<QJsonObject>>::err(ErrorCode::DbError, q.lastError().text());

    QList<QJsonObject> records;
    while (q.next()) {
        QJsonObject record;
        record[QStringLiteral("id")] = q.value(0).toString();
        record[QStringLiteral("member_id")] = q.value(1).toString();
        record[QStringLiteral("session_id")] = q.value(2).toString();
        record[QStringLiteral("punch_card_id")] = q.value(3).toString();
        record[QStringLiteral("checkin_attempt_id")] = q.value(4).toString();
        record[QStringLiteral("sessions_deducted")] = q.value(5).toInt();
        record[QStringLiteral("balance_before")] = q.value(6).toInt();
        record[QStringLiteral("balance_after")] = q.value(7).toInt();
        record[QStringLiteral("deducted_at")] = q.value(8).toString();
        record[QStringLiteral("reversed_by_correction_id")] = q.value(9).toString();
        records.append(std::move(record));
    }

    return Result<QList<QJsonObject>>::ok(std::move(records));
}

Result<bool> CheckInRepository::applyIncomingDeduction(const QJsonObject& record,
                                                       const QString& actorUserId)
{
    const QString deductionId = record[QStringLiteral("id")].toString().trimmed();
    const QString memberId = record[QStringLiteral("member_id")].toString().trimmed();
    const QString sessionId = record[QStringLiteral("session_id")].toString().trimmed();
    const QString punchCardId = record[QStringLiteral("punch_card_id")].toString().trimmed();
    QString attemptId = record[QStringLiteral("checkin_attempt_id")].toString().trimmed();
    const int sessionsDeducted = std::max(1, record[QStringLiteral("sessions_deducted")].toInt(1));
    const QString reversedByCorrectionId = record[QStringLiteral("reversed_by_correction_id")].toString().trimmed();

    if (deductionId.isEmpty() || memberId.isEmpty() || sessionId.isEmpty() || punchCardId.isEmpty()) {
        return Result<bool>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Incoming deduction is missing required fields"));
    }

    if (attemptId.isEmpty())
        attemptId = QStringLiteral("sync-") + deductionId;

    QDateTime deductedAt = QDateTime::fromString(
        record[QStringLiteral("deducted_at")].toString(), Qt::ISODateWithMs);
    if (!deductedAt.isValid()) {
        deductedAt = QDateTime::fromString(
            record[QStringLiteral("deducted_at")].toString(), Qt::ISODate);
    }
    if (!deductedAt.isValid())
        deductedAt = QDateTime::currentDateTimeUtc();

    // Idempotency: deduction id already exists.
    {
        QSqlQuery exists(m_db);
        exists.prepare(QStringLiteral("SELECT 1 FROM deduction_events WHERE id = ?"));
        exists.addBindValue(deductionId);
        if (!exists.exec())
            return Result<bool>::err(ErrorCode::DbError, exists.lastError().text());
        if (exists.next())
            return Result<bool>::ok(false);
    }

    // The synthetic check-in attempt references an existing user.
    {
        QSqlQuery userQuery(m_db);
        userQuery.prepare(QStringLiteral("SELECT 1 FROM users WHERE id = ?"));
        userQuery.addBindValue(actorUserId);
        if (!userQuery.exec())
            return Result<bool>::err(ErrorCode::DbError, userQuery.lastError().text());
        if (!userQuery.next()) {
            return Result<bool>::err(
                ErrorCode::NotFound,
                QStringLiteral("Sync actor user was not found for incoming deduction"));
        }
    }

    if (!m_db.transaction()) {
        return Result<bool>::err(
            ErrorCode::DbError,
            QStringLiteral("Failed to begin incoming deduction transaction"));
    }

    QSqlQuery punchQuery(m_db);
    punchQuery.prepare(QStringLiteral(
        "SELECT current_balance FROM punch_cards WHERE id = ? AND member_id = ?"));
    punchQuery.addBindValue(punchCardId);
    punchQuery.addBindValue(memberId);
    if (!punchQuery.exec()) {
        m_db.rollback();
        return Result<bool>::err(ErrorCode::DbError, punchQuery.lastError().text());
    }
    if (!punchQuery.next()) {
        m_db.rollback();
        return Result<bool>::err(
            ErrorCode::NotFound,
            QStringLiteral("Incoming deduction references an unknown punch card"));
    }

    const int balanceBefore = punchQuery.value(0).toInt();
    if (balanceBefore < sessionsDeducted) {
        m_db.rollback();
        return Result<bool>::err(
            ErrorCode::InvalidState,
            QStringLiteral("Incoming deduction exceeds local punch-card balance"));
    }

    // Ensure check-in attempt exists for FK consistency.
    QSqlQuery attemptExists(m_db);
    attemptExists.prepare(QStringLiteral(
        "SELECT deduction_event_id FROM checkin_attempts WHERE id = ?"));
    attemptExists.addBindValue(attemptId);
    if (!attemptExists.exec()) {
        m_db.rollback();
        return Result<bool>::err(ErrorCode::DbError, attemptExists.lastError().text());
    }

    if (attemptExists.next()) {
        const QString existingDeductionId = attemptExists.value(0).toString();
        if (!existingDeductionId.isEmpty() && existingDeductionId != deductionId) {
            m_db.rollback();
            return Result<bool>::err(
                ErrorCode::InvalidState,
                QStringLiteral("Incoming deduction references an occupied check-in attempt id"));
        }
    } else {
        QSqlQuery insertAttempt(m_db);
        insertAttempt.prepare(QStringLiteral(
            "INSERT INTO checkin_attempts "
            "(id, member_id, session_id, operator_user_id, status, attempted_at, "
            " deduction_event_id, failure_reason) "
            "VALUES (?, ?, ?, ?, 'Success', ?, ?, NULL)"));
        insertAttempt.addBindValue(attemptId);
        insertAttempt.addBindValue(memberId);
        insertAttempt.addBindValue(sessionId);
        insertAttempt.addBindValue(actorUserId);
        insertAttempt.addBindValue(deductedAt.toString(Qt::ISODateWithMs));
        insertAttempt.addBindValue(deductionId);
        if (!insertAttempt.exec()) {
            m_db.rollback();
            return Result<bool>::err(ErrorCode::DbError, insertAttempt.lastError().text());
        }
    }

    QSqlQuery deductCard(m_db);
    deductCard.prepare(QStringLiteral(
        "UPDATE punch_cards "
        "SET current_balance = current_balance - ?, updated_at = ? "
        "WHERE id = ? AND current_balance >= ?"));
    deductCard.addBindValue(sessionsDeducted);
    deductCard.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    deductCard.addBindValue(punchCardId);
    deductCard.addBindValue(sessionsDeducted);
    if (!deductCard.exec()) {
        m_db.rollback();
        return Result<bool>::err(ErrorCode::DbError, deductCard.lastError().text());
    }
    if (deductCard.numRowsAffected() == 0) {
        m_db.rollback();
        return Result<bool>::err(
            ErrorCode::InvalidState,
            QStringLiteral("Incoming deduction could not be applied to punch-card balance"));
    }

    const int balanceAfter = balanceBefore - sessionsDeducted;

    QSqlQuery insertDeduction(m_db);
    insertDeduction.prepare(QStringLiteral(
        "INSERT INTO deduction_events "
        "(id, member_id, punch_card_id, checkin_attempt_id, sessions_deducted, "
        " balance_before, balance_after, deducted_at, reversed_by_correction_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    insertDeduction.addBindValue(deductionId);
    insertDeduction.addBindValue(memberId);
    insertDeduction.addBindValue(punchCardId);
    insertDeduction.addBindValue(attemptId);
    insertDeduction.addBindValue(sessionsDeducted);
    insertDeduction.addBindValue(balanceBefore);
    insertDeduction.addBindValue(balanceAfter);
    insertDeduction.addBindValue(deductedAt.toString(Qt::ISODateWithMs));
    insertDeduction.addBindValue(reversedByCorrectionId.isEmpty() ? QVariant() : reversedByCorrectionId);
    if (!insertDeduction.exec()) {
        m_db.rollback();
        return Result<bool>::err(ErrorCode::DbError, insertDeduction.lastError().text());
    }

    QSqlQuery bindAttempt(m_db);
    bindAttempt.prepare(QStringLiteral(
        "UPDATE checkin_attempts SET deduction_event_id = ? "
        "WHERE id = ? AND (deduction_event_id IS NULL OR deduction_event_id = '')"));
    bindAttempt.addBindValue(deductionId);
    bindAttempt.addBindValue(attemptId);
    if (!bindAttempt.exec()) {
        m_db.rollback();
        return Result<bool>::err(ErrorCode::DbError, bindAttempt.lastError().text());
    }

    if (!m_db.commit()) {
        m_db.rollback();
        return Result<bool>::err(
            ErrorCode::DbError,
            QStringLiteral("Failed to commit incoming deduction transaction"));
    }

    return Result<bool>::ok(true);
}

Result<std::optional<QJsonObject>> CheckInRepository::findLocalDeductionConflict(
    const QString& memberId,
    const QString& sessionId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT de.id, de.member_id, ci.session_id, de.punch_card_id, "
        "       de.checkin_attempt_id, de.sessions_deducted, de.balance_before, "
        "       de.balance_after, de.deducted_at, de.reversed_by_correction_id "
        "FROM deduction_events de "
        "JOIN checkin_attempts ci ON ci.id = de.checkin_attempt_id "
        "WHERE de.member_id = ? AND ci.session_id = ? "
        "ORDER BY de.deducted_at DESC LIMIT 1"));
    q.addBindValue(memberId);
    q.addBindValue(sessionId);

    if (!q.exec())
        return Result<std::optional<QJsonObject>>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<std::optional<QJsonObject>>::ok(std::nullopt);

    QJsonObject record;
    record[QStringLiteral("id")] = q.value(0).toString();
    record[QStringLiteral("member_id")] = q.value(1).toString();
    record[QStringLiteral("session_id")] = q.value(2).toString();
    record[QStringLiteral("punch_card_id")] = q.value(3).toString();
    record[QStringLiteral("checkin_attempt_id")] = q.value(4).toString();
    record[QStringLiteral("sessions_deducted")] = q.value(5).toInt();
    record[QStringLiteral("balance_before")] = q.value(6).toInt();
    record[QStringLiteral("balance_after")] = q.value(7).toInt();
    record[QStringLiteral("deducted_at")] = q.value(8).toString();
    record[QStringLiteral("reversed_by_correction_id")] = q.value(9).toString();
    return Result<std::optional<QJsonObject>>::ok(record);
}

Result<void> CheckInRepository::setDeductionReversed(const QString& deductionId,
                                                       const QString& correctionId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE deduction_events SET reversed_by_correction_id = ? WHERE id = ?"));
    q.addBindValue(correctionId);
    q.addBindValue(deductionId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<void>::err(ErrorCode::NotFound);

    return Result<void>::ok();
}

// ── Correction requests ──────────────────────────────────────────────────────

Result<CorrectionRequest> CheckInRepository::insertCorrectionRequest(const CorrectionRequest& r)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO correction_requests "
        "(id, deduction_event_id, requested_by_user_id, rationale, status, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(r.id);
    q.addBindValue(r.deductionEventId);
    q.addBindValue(r.requestedByUserId);
    q.addBindValue(r.rationale);
    q.addBindValue(correctionStatusToString(r.status));
    q.addBindValue(r.createdAt.toString(Qt::ISODateWithMs));

    if (!q.exec())
        return Result<CorrectionRequest>::err(ErrorCode::DbError, q.lastError().text());

    return Result<CorrectionRequest>::ok(r);
}

Result<CorrectionRequest> CheckInRepository::getCorrectionRequest(const QString& id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, deduction_event_id, requested_by_user_id, rationale, status, created_at "
        "FROM correction_requests WHERE id = ?"));
    q.addBindValue(id);

    if (!q.exec())
        return Result<CorrectionRequest>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<CorrectionRequest>::err(ErrorCode::NotFound);

    return Result<CorrectionRequest>::ok(rowToCorrectionRequest(q));
}

Result<QList<CorrectionRequest>>
CheckInRepository::listCorrectionRequests(CorrectionStatus status)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, deduction_event_id, requested_by_user_id, rationale, status, created_at "
        "FROM correction_requests WHERE status = ? "
        "ORDER BY created_at DESC"));
    q.addBindValue(correctionStatusToString(status));

    if (!q.exec())
        return Result<QList<CorrectionRequest>>::err(ErrorCode::DbError, q.lastError().text());

    QList<CorrectionRequest> results;
    while (q.next())
        results.append(rowToCorrectionRequest(q));

    return Result<QList<CorrectionRequest>>::ok(std::move(results));
}

Result<QList<QJsonObject>> CheckInRepository::listCorrectionDelta(const QDateTime& since)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT cr.id, cr.deduction_event_id, cr.requested_by_user_id, cr.rationale, "
        "       cr.status, cr.created_at, ca.approved_by_user_id, ca.step_up_window_id, "
        "       ca.approved_at, ca.before_payload_json, ca.after_payload_json "
        "FROM correction_requests cr "
        "LEFT JOIN correction_approvals ca ON ca.correction_request_id = cr.id "
        "WHERE cr.created_at >= ? OR ca.approved_at >= ? "
        "ORDER BY cr.created_at ASC"));
    q.addBindValue(since.toString(Qt::ISODateWithMs));
    q.addBindValue(since.toString(Qt::ISODateWithMs));

    if (!q.exec())
        return Result<QList<QJsonObject>>::err(ErrorCode::DbError, q.lastError().text());

    QList<QJsonObject> records;
    while (q.next()) {
        QJsonObject record;
        record[QStringLiteral("id")] = q.value(0).toString();
        record[QStringLiteral("deduction_event_id")] = q.value(1).toString();
        record[QStringLiteral("requested_by_user_id")] = q.value(2).toString();
        record[QStringLiteral("rationale")] = q.value(3).toString();
        record[QStringLiteral("status")] = q.value(4).toString();
        record[QStringLiteral("created_at")] = q.value(5).toString();

        if (!q.value(6).isNull()) {
            QJsonObject approval;
            approval[QStringLiteral("approved_by_user_id")] = q.value(6).toString();
            approval[QStringLiteral("step_up_window_id")] = q.value(7).toString();
            approval[QStringLiteral("approved_at")] = q.value(8).toString();
            approval[QStringLiteral("before_payload_json")] = q.value(9).toString();
            approval[QStringLiteral("after_payload_json")] = q.value(10).toString();
            record[QStringLiteral("approval")] = approval;
        }

        records.append(std::move(record));
    }

    return Result<QList<QJsonObject>>::ok(std::move(records));
}

Result<bool> CheckInRepository::applyIncomingCorrection(const QJsonObject& record,
                                                        const QString& actorUserId)
{
    const QString correctionId = record[QStringLiteral("id")].toString().trimmed();
    const QString deductionEventId = record[QStringLiteral("deduction_event_id")].toString().trimmed();
    QString requestedByUserId = record[QStringLiteral("requested_by_user_id")].toString().trimmed();
    const QString rationale = record[QStringLiteral("rationale")].toString().trimmed();
    const QString statusText = record[QStringLiteral("status")].toString();

    if (correctionId.isEmpty() || deductionEventId.isEmpty()) {
        return Result<bool>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Incoming correction is missing required fields"));
    }

    // Idempotency: correction request already exists.
    {
        QSqlQuery exists(m_db);
        exists.prepare(QStringLiteral("SELECT 1 FROM correction_requests WHERE id = ?"));
        exists.addBindValue(correctionId);
        if (!exists.exec())
            return Result<bool>::err(ErrorCode::DbError, exists.lastError().text());
        if (exists.next())
            return Result<bool>::ok(false);
    }

    auto userExists = [this](const QString& userId) -> Result<bool> {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT 1 FROM users WHERE id = ?"));
        q.addBindValue(userId);
        if (!q.exec())
            return Result<bool>::err(ErrorCode::DbError, q.lastError().text());
        return Result<bool>::ok(q.next());
    };

    auto actorExists = userExists(actorUserId);
    if (actorExists.isErr())
        return actorExists;
    if (!actorExists.value()) {
        return Result<bool>::err(
            ErrorCode::NotFound,
            QStringLiteral("Sync actor user was not found for incoming correction"));
    }

    if (requestedByUserId.isEmpty()) {
        requestedByUserId = actorUserId;
    } else {
        auto requestedExists = userExists(requestedByUserId);
        if (requestedExists.isErr())
            return requestedExists;
        if (!requestedExists.value())
            requestedByUserId = actorUserId;
    }

    QDateTime createdAt = QDateTime::fromString(
        record[QStringLiteral("created_at")].toString(), Qt::ISODateWithMs);
    if (!createdAt.isValid()) {
        createdAt = QDateTime::fromString(
            record[QStringLiteral("created_at")].toString(), Qt::ISODate);
    }
    if (!createdAt.isValid())
        createdAt = QDateTime::currentDateTimeUtc();

    const CorrectionStatus status = correctionStatusFromString(statusText);

    QSqlQuery deductionQuery(m_db);
    deductionQuery.prepare(QStringLiteral(
        "SELECT punch_card_id, sessions_deducted, reversed_by_correction_id "
        "FROM deduction_events WHERE id = ?"));
    deductionQuery.addBindValue(deductionEventId);
    if (!deductionQuery.exec())
        return Result<bool>::err(ErrorCode::DbError, deductionQuery.lastError().text());
    if (!deductionQuery.next()) {
        return Result<bool>::err(
            ErrorCode::NotFound,
            QStringLiteral("Incoming correction references an unknown deduction"));
    }

    const QString punchCardId = deductionQuery.value(0).toString();
    const int sessionsDeducted = deductionQuery.value(1).toInt();
    const QString existingReversalId = deductionQuery.value(2).toString();
    if (status == CorrectionStatus::Applied
        && !existingReversalId.isEmpty()
        && existingReversalId != correctionId) {
        return Result<bool>::err(
            ErrorCode::InvalidState,
            QStringLiteral("Incoming correction conflicts with an existing reversal"));
    }

    if (!m_db.transaction()) {
        return Result<bool>::err(
            ErrorCode::DbError,
            QStringLiteral("Failed to begin incoming correction transaction"));
    }

    QSqlQuery insertCorrection(m_db);
    insertCorrection.prepare(QStringLiteral(
        "INSERT INTO correction_requests "
        "(id, deduction_event_id, requested_by_user_id, rationale, status, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?)"));
    insertCorrection.addBindValue(correctionId);
    insertCorrection.addBindValue(deductionEventId);
    insertCorrection.addBindValue(requestedByUserId);
    insertCorrection.addBindValue(rationale.isEmpty() ? QStringLiteral("Imported from sync package")
                                                      : rationale);
    insertCorrection.addBindValue(correctionStatusToString(status));
    insertCorrection.addBindValue(createdAt.toString(Qt::ISODateWithMs));
    if (!insertCorrection.exec()) {
        m_db.rollback();
        return Result<bool>::err(ErrorCode::DbError, insertCorrection.lastError().text());
    }

    if (status == CorrectionStatus::Applied && existingReversalId.isEmpty()) {
        QSqlQuery restoreCard(m_db);
        restoreCard.prepare(QStringLiteral(
            "UPDATE punch_cards "
            "SET current_balance = CASE "
            "  WHEN current_balance + ? > initial_balance THEN initial_balance "
            "  ELSE current_balance + ? END, "
            "updated_at = ? "
            "WHERE id = ?"));
        restoreCard.addBindValue(sessionsDeducted);
        restoreCard.addBindValue(sessionsDeducted);
        restoreCard.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        restoreCard.addBindValue(punchCardId);
        if (!restoreCard.exec()) {
            m_db.rollback();
            return Result<bool>::err(ErrorCode::DbError, restoreCard.lastError().text());
        }
        if (restoreCard.numRowsAffected() == 0) {
            m_db.rollback();
            return Result<bool>::err(
                ErrorCode::InvalidState,
                QStringLiteral("Incoming correction could not restore punch-card balance"));
        }

        QSqlQuery markReversed(m_db);
        markReversed.prepare(QStringLiteral(
            "UPDATE deduction_events SET reversed_by_correction_id = ? "
            "WHERE id = ? AND (reversed_by_correction_id IS NULL OR reversed_by_correction_id = '')"));
        markReversed.addBindValue(correctionId);
        markReversed.addBindValue(deductionEventId);
        if (!markReversed.exec()) {
            m_db.rollback();
            return Result<bool>::err(ErrorCode::DbError, markReversed.lastError().text());
        }
        if (markReversed.numRowsAffected() == 0) {
            m_db.rollback();
            return Result<bool>::err(
                ErrorCode::InvalidState,
                QStringLiteral("Incoming correction could not bind deduction reversal"));
        }
    }

    if (!m_db.commit()) {
        m_db.rollback();
        return Result<bool>::err(
            ErrorCode::DbError,
            QStringLiteral("Failed to commit incoming correction transaction"));
    }

    return Result<bool>::ok(true);
}

Result<void> CheckInRepository::updateCorrectionStatus(const QString& id,
                                                         CorrectionStatus status)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE correction_requests SET status = ? WHERE id = ?"));
    q.addBindValue(correctionStatusToString(status));
    q.addBindValue(id);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<void>::err(ErrorCode::NotFound);

    return Result<void>::ok();
}

Result<QString> CheckInRepository::createCompensatingCorrection(const QString& deductionEventId,
                                                                const QString& actorUserId,
                                                                const QString& rationale)
{
    if (deductionEventId.trimmed().isEmpty()) {
        return Result<QString>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Deduction id is required for compensating correction"));
    }

    if (rationale.trimmed().isEmpty()) {
        return Result<QString>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Compensating correction rationale is required"));
    }

    QSqlQuery actorQuery(m_db);
    actorQuery.prepare(QStringLiteral("SELECT 1 FROM users WHERE id = ?"));
    actorQuery.addBindValue(actorUserId);
    if (!actorQuery.exec())
        return Result<QString>::err(ErrorCode::DbError, actorQuery.lastError().text());
    if (!actorQuery.next()) {
        return Result<QString>::err(
            ErrorCode::NotFound,
            QStringLiteral("Compensating correction actor user was not found"));
    }

    QSqlQuery deductionQuery(m_db);
    deductionQuery.prepare(QStringLiteral(
        "SELECT punch_card_id, sessions_deducted, reversed_by_correction_id "
        "FROM deduction_events WHERE id = ?"));
    deductionQuery.addBindValue(deductionEventId);
    if (!deductionQuery.exec())
        return Result<QString>::err(ErrorCode::DbError, deductionQuery.lastError().text());
    if (!deductionQuery.next())
        return Result<QString>::err(ErrorCode::NotFound, QStringLiteral("Deduction not found"));

    const QString punchCardId = deductionQuery.value(0).toString();
    const int sessionsDeducted = deductionQuery.value(1).toInt();
    const QString existingCorrectionId = deductionQuery.value(2).toString();
    if (!existingCorrectionId.isEmpty())
        return Result<QString>::ok(existingCorrectionId);

    if (!m_db.transaction()) {
        return Result<QString>::err(
            ErrorCode::DbError,
            QStringLiteral("Failed to begin compensating correction transaction"));
    }

    const QString correctionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString nowIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    QSqlQuery insertCorrection(m_db);
    insertCorrection.prepare(QStringLiteral(
        "INSERT INTO correction_requests "
        "(id, deduction_event_id, requested_by_user_id, rationale, status, created_at) "
        "VALUES (?, ?, ?, ?, 'Applied', ?)"));
    insertCorrection.addBindValue(correctionId);
    insertCorrection.addBindValue(deductionEventId);
    insertCorrection.addBindValue(actorUserId);
    insertCorrection.addBindValue(rationale.trimmed());
    insertCorrection.addBindValue(nowIso);
    if (!insertCorrection.exec()) {
        m_db.rollback();
        return Result<QString>::err(ErrorCode::DbError, insertCorrection.lastError().text());
    }

    QSqlQuery restoreCard(m_db);
    restoreCard.prepare(QStringLiteral(
        "UPDATE punch_cards "
        "SET current_balance = CASE "
        "  WHEN current_balance + ? > initial_balance THEN initial_balance "
        "  ELSE current_balance + ? END, "
        "updated_at = ? "
        "WHERE id = ?"));
    restoreCard.addBindValue(sessionsDeducted);
    restoreCard.addBindValue(sessionsDeducted);
    restoreCard.addBindValue(nowIso);
    restoreCard.addBindValue(punchCardId);
    if (!restoreCard.exec()) {
        m_db.rollback();
        return Result<QString>::err(ErrorCode::DbError, restoreCard.lastError().text());
    }
    if (restoreCard.numRowsAffected() == 0) {
        m_db.rollback();
        return Result<QString>::err(
            ErrorCode::InvalidState,
            QStringLiteral("Compensating correction could not restore punch-card balance"));
    }

    QSqlQuery markReversed(m_db);
    markReversed.prepare(QStringLiteral(
        "UPDATE deduction_events SET reversed_by_correction_id = ? "
        "WHERE id = ? AND (reversed_by_correction_id IS NULL OR reversed_by_correction_id = '')"));
    markReversed.addBindValue(correctionId);
    markReversed.addBindValue(deductionEventId);
    if (!markReversed.exec()) {
        m_db.rollback();
        return Result<QString>::err(ErrorCode::DbError, markReversed.lastError().text());
    }
    if (markReversed.numRowsAffected() == 0) {
        m_db.rollback();
        return Result<QString>::err(
            ErrorCode::InvalidState,
            QStringLiteral("Compensating correction could not bind deduction reversal"));
    }

    if (!m_db.commit()) {
        m_db.rollback();
        return Result<QString>::err(
            ErrorCode::DbError,
            QStringLiteral("Failed to commit compensating correction transaction"));
    }

    return Result<QString>::ok(correctionId);
}

// ── Correction approvals ─────────────────────────────────────────────────────

Result<CorrectionApproval> CheckInRepository::insertCorrectionApproval(const CorrectionApproval& a)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO correction_approvals "
        "(correction_request_id, approved_by_user_id, step_up_window_id, "
        " rationale, approved_at, before_payload_json, after_payload_json) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(a.correctionRequestId);
    q.addBindValue(a.approvedByUserId);
    q.addBindValue(a.stepUpWindowId);
    q.addBindValue(a.rationale);
    q.addBindValue(a.approvedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(a.beforePayloadJson);
    q.addBindValue(a.afterPayloadJson);

    if (!q.exec())
        return Result<CorrectionApproval>::err(ErrorCode::DbError, q.lastError().text());

    return Result<CorrectionApproval>::ok(a);
}

Result<CorrectionApproval> CheckInRepository::getCorrectionApproval(const QString& requestId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT correction_request_id, approved_by_user_id, step_up_window_id, "
        "       rationale, approved_at, before_payload_json, after_payload_json "
        "FROM correction_approvals WHERE correction_request_id = ?"));
    q.addBindValue(requestId);

    if (!q.exec())
        return Result<CorrectionApproval>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<CorrectionApproval>::err(ErrorCode::NotFound);

    CorrectionApproval a;
    a.correctionRequestId = q.value(0).toString();
    a.approvedByUserId    = q.value(1).toString();
    a.stepUpWindowId      = q.value(2).toString();
    a.rationale           = q.value(3).toString();
    a.approvedAt          = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
    a.beforePayloadJson   = q.value(5).toString();
    a.afterPayloadJson    = q.value(6).toString();
    return Result<CorrectionApproval>::ok(std::move(a));
}
