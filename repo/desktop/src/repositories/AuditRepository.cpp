// AuditRepository.cpp — ProctorOps
// Concrete SQLite implementation for the tamper-evident audit chain.

#include "AuditRepository.h"
#include "models/CommonTypes.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>

namespace {

AuditEventType eventTypeFromString(const QString& s)
{
    // Reverse lookup — iterate through all known types
    // This is acceptable because audit queries are not hot-path operations.
    static const QMap<QString, AuditEventType> map = {
        {QStringLiteral("LOGIN"), AuditEventType::Login},
        {QStringLiteral("LOGIN_FAILED"), AuditEventType::LoginFailed},
        {QStringLiteral("LOGIN_LOCKED"), AuditEventType::LoginLocked},
        {QStringLiteral("CAPTCHA_CHALLENGE"), AuditEventType::CaptchaChallenge},
        {QStringLiteral("CAPTCHA_SOLVED"), AuditEventType::CaptchaSolved},
        {QStringLiteral("CAPTCHA_FAILED"), AuditEventType::CaptchaFailed},
        {QStringLiteral("LOGOUT"), AuditEventType::Logout},
        {QStringLiteral("CONSOLE_LOCKED"), AuditEventType::ConsoleLocked},
        {QStringLiteral("CONSOLE_UNLOCKED"), AuditEventType::ConsoleUnlocked},
        {QStringLiteral("STEP_UP_INITIATED"), AuditEventType::StepUpInitiated},
        {QStringLiteral("STEP_UP_PASSED"), AuditEventType::StepUpPassed},
        {QStringLiteral("STEP_UP_FAILED"), AuditEventType::StepUpFailed},
        {QStringLiteral("USER_CREATED"), AuditEventType::UserCreated},
        {QStringLiteral("USER_UPDATED"), AuditEventType::UserUpdated},
        {QStringLiteral("USER_DEACTIVATED"), AuditEventType::UserDeactivated},
        {QStringLiteral("ROLE_CHANGED"), AuditEventType::RoleChanged},
        {QStringLiteral("USER_UNLOCKED"), AuditEventType::UserUnlocked},
        {QStringLiteral("PASSWORD_RESET"), AuditEventType::PasswordReset},
        {QStringLiteral("CHECKIN_ATTEMPTED"), AuditEventType::CheckInAttempted},
        {QStringLiteral("CHECKIN_SUCCESS"), AuditEventType::CheckInSuccess},
        {QStringLiteral("CHECKIN_DUPLICATE_BLOCKED"), AuditEventType::CheckInDuplicateBlocked},
        {QStringLiteral("CHECKIN_FROZEN_BLOCKED"), AuditEventType::CheckInFrozenBlocked},
        {QStringLiteral("CHECKIN_EXPIRED_BLOCKED"), AuditEventType::CheckInExpiredBlocked},
        {QStringLiteral("CHECKIN_TERM_CARD_INVALID"), AuditEventType::CheckInTermCardInvalid},
        {QStringLiteral("CHECKIN_PUNCH_CARD_EXHAUSTED"), AuditEventType::CheckInPunchCardExhausted},
        {QStringLiteral("DEDUCTION_CREATED"), AuditEventType::DeductionCreated},
        {QStringLiteral("DEDUCTION_REVERSED"), AuditEventType::DeductionReversed},
        {QStringLiteral("CORRECTION_REQUESTED"), AuditEventType::CorrectionRequested},
        {QStringLiteral("CORRECTION_APPROVED"), AuditEventType::CorrectionApproved},
        {QStringLiteral("CORRECTION_REJECTED"), AuditEventType::CorrectionRejected},
        {QStringLiteral("CORRECTION_APPLIED"), AuditEventType::CorrectionApplied},
        {QStringLiteral("QUESTION_CREATED"), AuditEventType::QuestionCreated},
        {QStringLiteral("QUESTION_UPDATED"), AuditEventType::QuestionUpdated},
        {QStringLiteral("QUESTION_DELETED"), AuditEventType::QuestionDeleted},
        {QStringLiteral("KNOWLEDGE_POINT_CREATED"), AuditEventType::KnowledgePointCreated},
        {QStringLiteral("KNOWLEDGE_POINT_UPDATED"), AuditEventType::KnowledgePointUpdated},
        {QStringLiteral("KNOWLEDGE_POINT_DELETED"), AuditEventType::KnowledgePointDeleted},
        {QStringLiteral("KNOWLEDGE_POINT_MAPPED"), AuditEventType::KnowledgePointMapped},
        {QStringLiteral("KNOWLEDGE_POINT_UNMAPPED"), AuditEventType::KnowledgePointUnmapped},
        {QStringLiteral("TAG_CREATED"), AuditEventType::TagCreated},
        {QStringLiteral("TAG_APPLIED"), AuditEventType::TagApplied},
        {QStringLiteral("TAG_REMOVED"), AuditEventType::TagRemoved},
        {QStringLiteral("JOB_CREATED"), AuditEventType::JobCreated},
        {QStringLiteral("JOB_STARTED"), AuditEventType::JobStarted},
        {QStringLiteral("JOB_COMPLETED"), AuditEventType::JobCompleted},
        {QStringLiteral("JOB_FAILED"), AuditEventType::JobFailed},
        {QStringLiteral("JOB_CANCELLED"), AuditEventType::JobCancelled},
        {QStringLiteral("JOB_INTERRUPTED"), AuditEventType::JobInterrupted},
        {QStringLiteral("SYNC_EXPORT"), AuditEventType::SyncExport},
        {QStringLiteral("SYNC_IMPORT"), AuditEventType::SyncImport},
        {QStringLiteral("SYNC_CONFLICT_RESOLVED"), AuditEventType::SyncConflictResolved},
        {QStringLiteral("UPDATE_IMPORTED"), AuditEventType::UpdateImported},
        {QStringLiteral("UPDATE_STAGED"), AuditEventType::UpdateStaged},
        {QStringLiteral("UPDATE_APPLIED"), AuditEventType::UpdateApplied},
        {QStringLiteral("UPDATE_ROLLED_BACK"), AuditEventType::UpdateRolledBack},
        {QStringLiteral("KEY_IMPORTED"), AuditEventType::KeyImported},
        {QStringLiteral("KEY_REVOKED"), AuditEventType::KeyRevoked},
        {QStringLiteral("KEY_ROTATED"), AuditEventType::KeyRotated},
        {QStringLiteral("EXPORT_REQUESTED"), AuditEventType::ExportRequested},
        {QStringLiteral("EXPORT_COMPLETED"), AuditEventType::ExportCompleted},
        {QStringLiteral("DELETION_REQUESTED"), AuditEventType::DeletionRequested},
        {QStringLiteral("DELETION_APPROVED"), AuditEventType::DeletionApproved},
        {QStringLiteral("DELETION_COMPLETED"), AuditEventType::DeletionCompleted},
        {QStringLiteral("CHAIN_VERIFIED"), AuditEventType::ChainVerified},
        {QStringLiteral("AUDIT_EXPORT"), AuditEventType::AuditExport},
    };
    return map.value(s, AuditEventType::Login);
}

} // anonymous namespace

AuditRepository::AuditRepository(QSqlDatabase& db)
    : m_db(db)
{
}

AuditEntry AuditRepository::entryFromQuery(QSqlQuery& q)
{
    AuditEntry e;
    e.id                 = q.value(0).toString();
    e.timestamp          = QDateTime::fromString(q.value(1).toString(), Qt::ISODateWithMs);
    e.actorUserId        = q.value(2).toString();
    e.eventType          = eventTypeFromString(q.value(3).toString());
    e.entityType         = q.value(4).toString();
    e.entityId           = q.value(5).toString();
    e.beforePayloadJson  = q.value(6).toString();
    e.afterPayloadJson   = q.value(7).toString();
    e.previousEntryHash  = q.value(8).toString();
    e.entryHash          = q.value(9).toString();
    return e;
}

// ── Append-only writes ─────────────────────────────────────────────────────

Result<AuditEntry> AuditRepository::insertEntry(const AuditEntry& entry)
{
    // Both insert and chain-head update must be atomic
    m_db.transaction();

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO audit_entries "
        "(id, timestamp, actor_user_id, event_type, entity_type, entity_id, "
        " before_payload_json, after_payload_json, previous_entry_hash, entry_hash) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(entry.id);
    q.addBindValue(entry.timestamp.toString(Qt::ISODateWithMs));
    q.addBindValue(entry.actorUserId);
    q.addBindValue(auditEventTypeToString(entry.eventType));
    q.addBindValue(entry.entityType);
    q.addBindValue(entry.entityId);
    q.addBindValue(entry.beforePayloadJson);
    q.addBindValue(entry.afterPayloadJson);
    q.addBindValue(entry.previousEntryHash);
    q.addBindValue(entry.entryHash);

    if (!q.exec()) {
        m_db.rollback();
        return Result<AuditEntry>::err(ErrorCode::DbError, q.lastError().text());
    }

    // Update chain head
    QSqlQuery headQ(m_db);
    headQ.prepare(QStringLiteral(
        "UPDATE audit_chain_head SET last_entry_id = ?, last_entry_hash = ? WHERE id = 1"));
    headQ.addBindValue(entry.id);
    headQ.addBindValue(entry.entryHash);

    if (!headQ.exec()) {
        m_db.rollback();
        return Result<AuditEntry>::err(ErrorCode::DbError, headQ.lastError().text());
    }

    m_db.commit();
    return Result<AuditEntry>::ok(entry);
}

Result<QString> AuditRepository::getChainHeadHash()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT last_entry_hash FROM audit_chain_head WHERE id = 1")))
        return Result<QString>::err(ErrorCode::DbError, q.lastError().text());

    if (!q.next())
        return Result<QString>::ok(QString()); // empty chain

    return Result<QString>::ok(q.value(0).toString());
}

// ── Queries ────────────────────────────────────────────────────────────────

Result<AuditEntry> AuditRepository::findEntryById(const QString& entryId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, timestamp, actor_user_id, event_type, entity_type, entity_id, "
        "       before_payload_json, after_payload_json, previous_entry_hash, entry_hash "
        "FROM audit_entries WHERE id = ?"));
    q.addBindValue(entryId);

    if (!q.exec())
        return Result<AuditEntry>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<AuditEntry>::err(ErrorCode::NotFound);

    return Result<AuditEntry>::ok(entryFromQuery(q));
}

Result<QList<AuditEntry>> AuditRepository::queryEntries(const AuditFilter& filter)
{
    QString sql = QStringLiteral(
        "SELECT id, timestamp, actor_user_id, event_type, entity_type, entity_id, "
        "       before_payload_json, after_payload_json, previous_entry_hash, entry_hash "
        "FROM audit_entries WHERE 1=1");

    QVariantList binds;

    if (!filter.actorUserId.isEmpty()) {
        sql += QStringLiteral(" AND actor_user_id = ?");
        binds.append(filter.actorUserId);
    }
    if (filter.eventType.has_value()) {
        sql += QStringLiteral(" AND event_type = ?");
        binds.append(auditEventTypeToString(filter.eventType.value()));
    }
    if (!filter.entityType.isEmpty()) {
        sql += QStringLiteral(" AND entity_type = ?");
        binds.append(filter.entityType);
    }
    if (!filter.entityId.isEmpty()) {
        sql += QStringLiteral(" AND entity_id = ?");
        binds.append(filter.entityId);
    }
    if (filter.fromTimestamp.has_value()) {
        sql += QStringLiteral(" AND timestamp >= ?");
        binds.append(filter.fromTimestamp.value().toString(Qt::ISODateWithMs));
    }
    if (filter.toTimestamp.has_value()) {
        sql += QStringLiteral(" AND timestamp <= ?");
        binds.append(filter.toTimestamp.value().toString(Qt::ISODateWithMs));
    }

    sql += QStringLiteral(" ORDER BY timestamp ASC LIMIT ? OFFSET ?");
    binds.append(filter.limit);
    binds.append(filter.offset);

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const QVariant& v : binds)
        q.addBindValue(v);

    if (!q.exec())
        return Result<QList<AuditEntry>>::err(ErrorCode::DbError, q.lastError().text());

    QList<AuditEntry> entries;
    while (q.next())
        entries.append(entryFromQuery(q));

    return Result<QList<AuditEntry>>::ok(std::move(entries));
}

// ── Retention ──────────────────────────────────────────────────────────────

Result<qint64> AuditRepository::countEntriesOlderThan(const QDateTime& threshold)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM audit_entries WHERE timestamp < ?"));
    q.addBindValue(threshold.toString(Qt::ISODateWithMs));

    if (!q.exec() || !q.next())
        return Result<qint64>::err(ErrorCode::DbError, q.lastError().text());

    return Result<qint64>::ok(q.value(0).toLongLong());
}

Result<void> AuditRepository::purgeEntriesOlderThan(const QDateTime& threshold)
{
    // Ensure the chain-anchor table exists so we can record the purge boundary.
    {
        QSqlQuery create(m_db);
        if (!create.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS audit_chain_anchors ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  anchor_hash     TEXT NOT NULL,"
                "  anchored_at     TEXT NOT NULL,"
                "  purge_threshold TEXT NOT NULL)")))
            return Result<void>::err(ErrorCode::DbError, create.lastError().text());
    }

    const QString thresholdIso = threshold.toString(Qt::ISODateWithMs);

    // Find the previousEntryHash of the oldest surviving entry — this anchors the
    // chain boundary so verifiers can skip deleted entries without flagging tampering.
    {
        QSqlQuery boundary(m_db);
        boundary.prepare(QStringLiteral(
            "SELECT previous_entry_hash FROM audit_entries "
            "WHERE timestamp >= ? ORDER BY timestamp ASC LIMIT 1"));
        boundary.addBindValue(thresholdIso);
        if (!boundary.exec())
            return Result<void>::err(ErrorCode::DbError, boundary.lastError().text());

        if (!boundary.next()) {
            // No entries survive: refuse to purge the entire chain.
            return Result<void>::err(ErrorCode::ValidationFailed,
                QStringLiteral("Purge would delete all audit entries — aborting to preserve chain"));
        }

        const QString anchorHash = boundary.value(0).toString();
        const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

        QSqlQuery insertAnchor(m_db);
        insertAnchor.prepare(QStringLiteral(
            "INSERT INTO audit_chain_anchors (anchor_hash, anchored_at, purge_threshold) "
            "VALUES (?, ?, ?)"));
        insertAnchor.addBindValue(anchorHash);
        insertAnchor.addBindValue(now);
        insertAnchor.addBindValue(thresholdIso);
        if (!insertAnchor.exec())
            return Result<void>::err(ErrorCode::DbError, insertAnchor.lastError().text());
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM audit_entries WHERE timestamp < ?"));
    q.addBindValue(thresholdIso);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

// ── Export requests ────────────────────────────────────────────────────────

Result<ExportRequest> AuditRepository::insertExportRequest(const ExportRequest& req)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO export_requests "
        "(id, member_id, requester_user_id, status, rationale, created_at, fulfilled_at, output_file_path) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(req.id);
    q.addBindValue(req.memberId);
    q.addBindValue(req.requesterUserId);
    q.addBindValue(req.status);
    q.addBindValue(req.rationale);
    q.addBindValue(req.createdAt.toString(Qt::ISODateWithMs));
    q.addBindValue(req.fulfilledAt.isNull() ? QVariant() : req.fulfilledAt.toString(Qt::ISODateWithMs));
    q.addBindValue(req.outputFilePath.isEmpty() ? QVariant() : req.outputFilePath);

    if (!q.exec())
        return Result<ExportRequest>::err(ErrorCode::DbError, q.lastError().text());

    return Result<ExportRequest>::ok(req);
}

Result<ExportRequest> AuditRepository::getExportRequest(const QString& id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, member_id, requester_user_id, status, rationale, "
        "       created_at, fulfilled_at, output_file_path "
        "FROM export_requests WHERE id = ?"));
    q.addBindValue(id);

    if (!q.exec())
        return Result<ExportRequest>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<ExportRequest>::err(ErrorCode::NotFound);

    ExportRequest req;
    req.id               = q.value(0).toString();
    req.memberId         = q.value(1).toString();
    req.requesterUserId  = q.value(2).toString();
    req.status           = q.value(3).toString();
    req.rationale        = q.value(4).toString();
    req.createdAt        = QDateTime::fromString(q.value(5).toString(), Qt::ISODateWithMs);
    req.fulfilledAt      = q.value(6).isNull() ? QDateTime()
                             : QDateTime::fromString(q.value(6).toString(), Qt::ISODateWithMs);
    req.outputFilePath   = q.value(7).toString();

    return Result<ExportRequest>::ok(std::move(req));
}

Result<void> AuditRepository::updateExportRequest(const ExportRequest& req)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE export_requests SET status = ?, fulfilled_at = ?, output_file_path = ? WHERE id = ?"));
    q.addBindValue(req.status);
    q.addBindValue(req.fulfilledAt.isNull() ? QVariant() : req.fulfilledAt.toString(Qt::ISODateWithMs));
    q.addBindValue(req.outputFilePath.isEmpty() ? QVariant() : req.outputFilePath);
    q.addBindValue(req.id);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

// ── Deletion requests ──────────────────────────────────────────────────────

Result<DeletionRequest> AuditRepository::insertDeletionRequest(const DeletionRequest& req)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO deletion_requests "
        "(id, member_id, requester_user_id, approver_user_id, status, rationale, "
        " created_at, approved_at, completed_at, fields_anonymized) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(req.id);
    q.addBindValue(req.memberId);
    q.addBindValue(req.requesterUserId);
    q.addBindValue(req.approverUserId.isEmpty() ? QVariant() : req.approverUserId);
    q.addBindValue(req.status);
    q.addBindValue(req.rationale);
    q.addBindValue(req.createdAt.toString(Qt::ISODateWithMs));
    q.addBindValue(req.approvedAt.isNull() ? QVariant() : req.approvedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(req.completedAt.isNull() ? QVariant() : req.completedAt.toString(Qt::ISODateWithMs));

    QJsonArray arr;
    for (const QString& f : req.fieldsAnonymized) arr.append(f);
    q.addBindValue(req.fieldsAnonymized.isEmpty() ? QVariant()
                     : QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));

    if (!q.exec())
        return Result<DeletionRequest>::err(ErrorCode::DbError, q.lastError().text());

    return Result<DeletionRequest>::ok(req);
}

Result<DeletionRequest> AuditRepository::getDeletionRequest(const QString& id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, member_id, requester_user_id, approver_user_id, status, rationale, "
        "       created_at, approved_at, completed_at, fields_anonymized "
        "FROM deletion_requests WHERE id = ?"));
    q.addBindValue(id);

    if (!q.exec())
        return Result<DeletionRequest>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<DeletionRequest>::err(ErrorCode::NotFound);

    DeletionRequest req;
    req.id               = q.value(0).toString();
    req.memberId         = q.value(1).toString();
    req.requesterUserId  = q.value(2).toString();
    req.approverUserId   = q.value(3).toString();
    req.status           = q.value(4).toString();
    req.rationale        = q.value(5).toString();
    req.createdAt        = QDateTime::fromString(q.value(6).toString(), Qt::ISODateWithMs);
    req.approvedAt       = q.value(7).isNull() ? QDateTime()
                             : QDateTime::fromString(q.value(7).toString(), Qt::ISODateWithMs);
    req.completedAt      = q.value(8).isNull() ? QDateTime()
                             : QDateTime::fromString(q.value(8).toString(), Qt::ISODateWithMs);

    QString fieldsJson = q.value(9).toString();
    if (!fieldsJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(fieldsJson.toUtf8());
        for (const QJsonValue& v : doc.array())
            req.fieldsAnonymized.append(v.toString());
    }

    return Result<DeletionRequest>::ok(std::move(req));
}

Result<void> AuditRepository::updateDeletionRequest(const DeletionRequest& req)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE deletion_requests "
        "SET approver_user_id = ?, status = ?, approved_at = ?, completed_at = ?, fields_anonymized = ? "
        "WHERE id = ?"));
    q.addBindValue(req.approverUserId.isEmpty() ? QVariant() : req.approverUserId);
    q.addBindValue(req.status);
    q.addBindValue(req.approvedAt.isNull() ? QVariant() : req.approvedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(req.completedAt.isNull() ? QVariant() : req.completedAt.toString(Qt::ISODateWithMs));

    QJsonArray arr;
    for (const QString& f : req.fieldsAnonymized) arr.append(f);
    q.addBindValue(req.fieldsAnonymized.isEmpty() ? QVariant()
                     : QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    q.addBindValue(req.id);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<QList<ExportRequest>> AuditRepository::listExportRequests(const QString& statusFilter)
{
    QString sql = QStringLiteral(
        "SELECT id, member_id, requester_user_id, status, rationale, "
        "       created_at, fulfilled_at, output_file_path "
        "FROM export_requests");
    if (!statusFilter.isEmpty())
        sql += QStringLiteral(" WHERE status = ?");
    sql += QStringLiteral(" ORDER BY created_at DESC");

    QSqlQuery q(m_db);
    q.prepare(sql);
    if (!statusFilter.isEmpty())
        q.addBindValue(statusFilter);

    if (!q.exec())
        return Result<QList<ExportRequest>>::err(ErrorCode::DbError, q.lastError().text());

    QList<ExportRequest> result;
    while (q.next()) {
        ExportRequest row;
        row.id              = q.value(0).toString();
        row.memberId        = q.value(1).toString();
        row.requesterUserId = q.value(2).toString();
        row.status          = q.value(3).toString();
        row.rationale       = q.value(4).toString();
        row.createdAt       = QDateTime::fromString(q.value(5).toString(), Qt::ISODateWithMs);
        row.fulfilledAt     = q.value(6).isNull() ? QDateTime()
                              : QDateTime::fromString(q.value(6).toString(), Qt::ISODateWithMs);
        row.outputFilePath  = q.value(7).toString();
        result.append(std::move(row));
    }
    return Result<QList<ExportRequest>>::ok(std::move(result));
}

Result<QList<DeletionRequest>> AuditRepository::listDeletionRequests(const QString& statusFilter)
{
    QString sql = QStringLiteral(
        "SELECT id, member_id, requester_user_id, approver_user_id, status, rationale, "
        "       created_at, approved_at, completed_at, fields_anonymized "
        "FROM deletion_requests");
    if (!statusFilter.isEmpty())
        sql += QStringLiteral(" WHERE status = ?");
    sql += QStringLiteral(" ORDER BY created_at DESC");

    QSqlQuery q(m_db);
    q.prepare(sql);
    if (!statusFilter.isEmpty())
        q.addBindValue(statusFilter);

    if (!q.exec())
        return Result<QList<DeletionRequest>>::err(ErrorCode::DbError, q.lastError().text());

    QList<DeletionRequest> result;
    while (q.next()) {
        DeletionRequest row;
        row.id              = q.value(0).toString();
        row.memberId        = q.value(1).toString();
        row.requesterUserId = q.value(2).toString();
        row.approverUserId  = q.value(3).toString();
        row.status          = q.value(4).toString();
        row.rationale       = q.value(5).toString();
        row.createdAt       = QDateTime::fromString(q.value(6).toString(), Qt::ISODateWithMs);
        row.approvedAt      = q.value(7).isNull() ? QDateTime()
                              : QDateTime::fromString(q.value(7).toString(), Qt::ISODateWithMs);
        row.completedAt     = q.value(8).isNull() ? QDateTime()
                              : QDateTime::fromString(q.value(8).toString(), Qt::ISODateWithMs);
        QString fieldsJson  = q.value(9).toString();
        if (!fieldsJson.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(fieldsJson.toUtf8());
            for (const QJsonValue& v : doc.array())
                row.fieldsAnonymized.append(v.toString());
        }
        result.append(std::move(row));
    }
    return Result<QList<DeletionRequest>>::ok(std::move(result));
}
