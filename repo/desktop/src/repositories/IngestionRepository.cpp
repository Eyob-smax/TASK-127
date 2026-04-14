// IngestionRepository.cpp — ProctorOps
// Concrete SQLite implementation for ingestion job queue, dependencies,
// checkpoints, and worker claims.

#include "IngestionRepository.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>

IngestionRepository::IngestionRepository(QSqlDatabase& db)
    : m_db(db)
{
}

// ── Enum string conversions ──────────────────────────────────────────────────

QString IngestionRepository::jobTypeToString(JobType t)
{
    switch (t) {
    case JobType::QuestionImport: return QStringLiteral("QuestionImport");
    case JobType::RosterImport:   return QStringLiteral("RosterImport");
    }
    return QStringLiteral("QuestionImport");
}

JobType IngestionRepository::jobTypeFromString(const QString& s)
{
    if (s == QStringLiteral("RosterImport")) return JobType::RosterImport;
    return JobType::QuestionImport;
}

QString IngestionRepository::jobStatusToString(JobStatus s)
{
    switch (s) {
    case JobStatus::Pending:     return QStringLiteral("Pending");
    case JobStatus::Claimed:     return QStringLiteral("Claimed");
    case JobStatus::Validating:  return QStringLiteral("Validating");
    case JobStatus::Importing:   return QStringLiteral("Importing");
    case JobStatus::Indexing:    return QStringLiteral("Indexing");
    case JobStatus::Completed:   return QStringLiteral("Completed");
    case JobStatus::Failed:      return QStringLiteral("Failed");
    case JobStatus::Interrupted: return QStringLiteral("Interrupted");
    case JobStatus::Cancelled:   return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Pending");
}

JobStatus IngestionRepository::jobStatusFromString(const QString& s)
{
    if (s == QStringLiteral("Claimed"))     return JobStatus::Claimed;
    if (s == QStringLiteral("Validating"))  return JobStatus::Validating;
    if (s == QStringLiteral("Importing"))   return JobStatus::Importing;
    if (s == QStringLiteral("Indexing"))    return JobStatus::Indexing;
    if (s == QStringLiteral("Completed"))   return JobStatus::Completed;
    if (s == QStringLiteral("Failed"))      return JobStatus::Failed;
    if (s == QStringLiteral("Interrupted")) return JobStatus::Interrupted;
    if (s == QStringLiteral("Cancelled"))   return JobStatus::Cancelled;
    return JobStatus::Pending;
}

QString IngestionRepository::jobPhaseToString(JobPhase p)
{
    switch (p) {
    case JobPhase::Validate: return QStringLiteral("Validate");
    case JobPhase::Import:   return QStringLiteral("Import");
    case JobPhase::Index:    return QStringLiteral("Index");
    }
    return QStringLiteral("Validate");
}

JobPhase IngestionRepository::jobPhaseFromString(const QString& s)
{
    if (s == QStringLiteral("Import")) return JobPhase::Import;
    if (s == QStringLiteral("Index"))  return JobPhase::Index;
    return JobPhase::Validate;
}

// ── Row mapping ──────────────────────────────────────────────────────────────

IngestionJob IngestionRepository::rowToJob(const QSqlQuery& q)
{
    IngestionJob j;
    j.id              = q.value(0).toString();
    j.type            = jobTypeFromString(q.value(1).toString());
    j.status          = jobStatusFromString(q.value(2).toString());
    j.priority        = q.value(3).toInt();
    j.sourceFilePath  = q.value(4).toString();
    j.scheduledAt     = q.value(5).isNull() ? QDateTime()
                          : QDateTime::fromString(q.value(5).toString(), Qt::ISODateWithMs);
    j.createdAt       = QDateTime::fromString(q.value(6).toString(), Qt::ISODateWithMs);
    j.startedAt       = q.value(7).isNull() ? QDateTime()
                          : QDateTime::fromString(q.value(7).toString(), Qt::ISODateWithMs);
    j.completedAt     = q.value(8).isNull() ? QDateTime()
                          : QDateTime::fromString(q.value(8).toString(), Qt::ISODateWithMs);
    j.failedAt        = q.value(9).isNull() ? QDateTime()
                          : QDateTime::fromString(q.value(9).toString(), Qt::ISODateWithMs);
    j.retryCount      = q.value(10).toInt();
    j.lastError       = q.value(11).toString();
    j.currentPhase    = jobPhaseFromString(q.value(12).toString());
    j.createdByUserId = q.value(13).toString();
    return j;
}

static const QString kJobSelectCols = QStringLiteral(
    "id, type, status, priority, source_file_path, scheduled_at, "
    "created_at, started_at, completed_at, failed_at, "
    "retry_count, last_error, current_phase, created_by_user_id");

// ── Jobs ─────────────────────────────────────────────────────────────────────

Result<IngestionJob> IngestionRepository::insertJob(const IngestionJob& job)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO ingestion_jobs "
        "(id, type, status, priority, source_file_path, scheduled_at, "
        " created_at, started_at, completed_at, failed_at, "
        " retry_count, last_error, current_phase, created_by_user_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(job.id);
    q.addBindValue(jobTypeToString(job.type));
    q.addBindValue(jobStatusToString(job.status));
    q.addBindValue(job.priority);
    q.addBindValue(job.sourceFilePath);
    q.addBindValue(job.scheduledAt.isNull() ? QVariant()
                     : job.scheduledAt.toString(Qt::ISODateWithMs));
    q.addBindValue(job.createdAt.toString(Qt::ISODateWithMs));
    q.addBindValue(job.startedAt.isNull() ? QVariant()
                     : job.startedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(job.completedAt.isNull() ? QVariant()
                     : job.completedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(job.failedAt.isNull() ? QVariant()
                     : job.failedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(job.retryCount);
    q.addBindValue(job.lastError.isEmpty() ? QVariant() : job.lastError);
    q.addBindValue(jobPhaseToString(job.currentPhase));
    q.addBindValue(job.createdByUserId);

    if (!q.exec())
        return Result<IngestionJob>::err(ErrorCode::DbError, q.lastError().text());

    return Result<IngestionJob>::ok(job);
}

Result<IngestionJob> IngestionRepository::getJob(const QString& jobId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ") + kJobSelectCols
              + QStringLiteral(" FROM ingestion_jobs WHERE id = ?"));
    q.addBindValue(jobId);

    if (!q.exec())
        return Result<IngestionJob>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<IngestionJob>::err(ErrorCode::NotFound);

    return Result<IngestionJob>::ok(rowToJob(q));
}

Result<IngestionJob> IngestionRepository::updateJobStatus(const QString& jobId,
                                                            JobStatus status,
                                                            const QString& lastError)
{
    QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    // Set phase-specific timestamp fields and retry count
    QString extraSet;
    if (status == JobStatus::Claimed)
        extraSet = QStringLiteral(", started_at = '") + now + QStringLiteral("'");
    else if (status == JobStatus::Completed)
        extraSet = QStringLiteral(", completed_at = '") + now + QStringLiteral("'");
    else if (status == JobStatus::Failed)
        extraSet = QStringLiteral(", failed_at = '") + now + QStringLiteral("', retry_count = retry_count + 1");

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE ingestion_jobs SET status = ?, last_error = ?")
              + extraSet
              + QStringLiteral(" WHERE id = ?"));
    q.addBindValue(jobStatusToString(status));
    q.addBindValue(lastError.isEmpty() ? QVariant() : lastError);
    q.addBindValue(jobId);

    if (!q.exec())
        return Result<IngestionJob>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<IngestionJob>::err(ErrorCode::NotFound);

    return getJob(jobId);
}

Result<QList<IngestionJob>> IngestionRepository::listJobsByStatus(JobStatus status)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ") + kJobSelectCols
              + QStringLiteral(" FROM ingestion_jobs WHERE status = ? "
                                "ORDER BY priority DESC, created_at ASC"));
    q.addBindValue(jobStatusToString(status));

    if (!q.exec())
        return Result<QList<IngestionJob>>::err(ErrorCode::DbError, q.lastError().text());

    QList<IngestionJob> results;
    while (q.next())
        results.append(rowToJob(q));

    return Result<QList<IngestionJob>>::ok(std::move(results));
}

Result<QList<IngestionJob>> IngestionRepository::listReadyJobs()
{
    // Ready: Pending status + all dependencies Completed (or no dependencies)
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT ") + kJobSelectCols + QStringLiteral(
        " FROM ingestion_jobs j "
        "WHERE j.status = 'Pending' "
        "  AND NOT EXISTS ("
        "    SELECT 1 FROM job_dependencies d "
        "    JOIN ingestion_jobs dep ON dep.id = d.depends_on_job_id "
        "    WHERE d.job_id = j.id AND dep.status != 'Completed'"
        "  ) "
        "ORDER BY j.priority DESC, j.created_at ASC"));

    if (!q.exec())
        return Result<QList<IngestionJob>>::err(ErrorCode::DbError, q.lastError().text());

    QList<IngestionJob> results;
    while (q.next())
        results.append(rowToJob(q));

    return Result<QList<IngestionJob>>::ok(std::move(results));
}

Result<void> IngestionRepository::cancelJob(const QString& jobId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE ingestion_jobs SET status = 'Cancelled' "
        "WHERE id = ? AND status = 'Pending'"));
    q.addBindValue(jobId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("Job is not in Pending status"));

    return Result<void>::ok();
}

Result<void> IngestionRepository::markInterrupted(const QList<QString>& jobIds)
{
    if (jobIds.isEmpty())
        return Result<void>::ok();

    QStringList placeholders;
    for (int i = 0; i < jobIds.size(); ++i)
        placeholders << QStringLiteral("?");

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE ingestion_jobs SET status = 'Interrupted' "
        "WHERE id IN (") + placeholders.join(QStringLiteral(", "))
              + QStringLiteral(") AND status IN ('Claimed', 'Validating', 'Importing', 'Indexing')"));

    for (const auto& id : jobIds)
        q.addBindValue(id);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<QList<QString>> IngestionRepository::findInProgressJobIds()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id FROM ingestion_jobs "
        "WHERE status IN ('Claimed', 'Validating', 'Importing', 'Indexing')"));

    if (!q.exec())
        return Result<QList<QString>>::err(ErrorCode::DbError, q.lastError().text());

    QList<QString> ids;
    while (q.next())
        ids.append(q.value(0).toString());

    return Result<QList<QString>>::ok(std::move(ids));
}

// ── Dependencies ─────────────────────────────────────────────────────────────

Result<void> IngestionRepository::insertDependency(const JobDependency& dep)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO job_dependencies (job_id, depends_on_job_id) VALUES (?, ?)"));
    q.addBindValue(dep.jobId);
    q.addBindValue(dep.dependsOnJobId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<QList<JobDependency>> IngestionRepository::getDependencies(const QString& jobId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT job_id, depends_on_job_id FROM job_dependencies WHERE job_id = ?"));
    q.addBindValue(jobId);

    if (!q.exec())
        return Result<QList<JobDependency>>::err(ErrorCode::DbError, q.lastError().text());

    QList<JobDependency> results;
    while (q.next()) {
        JobDependency d;
        d.jobId         = q.value(0).toString();
        d.dependsOnJobId = q.value(1).toString();
        results.append(std::move(d));
    }
    return Result<QList<JobDependency>>::ok(std::move(results));
}

Result<bool> IngestionRepository::areDependenciesMet(const QString& jobId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM job_dependencies d "
        "JOIN ingestion_jobs dep ON dep.id = d.depends_on_job_id "
        "WHERE d.job_id = ? AND dep.status != 'Completed'"));
    q.addBindValue(jobId);

    if (!q.exec())
        return Result<bool>::err(ErrorCode::DbError, q.lastError().text());

    q.next();
    return Result<bool>::ok(q.value(0).toInt() == 0);
}

// ── Checkpoints ──────────────────────────────────────────────────────────────

Result<void> IngestionRepository::saveCheckpoint(const JobCheckpoint& cp)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO job_checkpoints "
        "(job_id, phase, offset_bytes, records_processed, saved_at) "
        "VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(cp.jobId);
    q.addBindValue(jobPhaseToString(cp.phase));
    q.addBindValue(cp.offsetBytes);
    q.addBindValue(cp.recordsProcessed);
    q.addBindValue(cp.savedAt.toString(Qt::ISODateWithMs));

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<std::optional<JobCheckpoint>>
IngestionRepository::loadCheckpoint(const QString& jobId, JobPhase phase)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT job_id, phase, offset_bytes, records_processed, saved_at "
        "FROM job_checkpoints WHERE job_id = ? AND phase = ?"));
    q.addBindValue(jobId);
    q.addBindValue(jobPhaseToString(phase));

    if (!q.exec())
        return Result<std::optional<JobCheckpoint>>::err(
            ErrorCode::DbError, q.lastError().text());

    if (!q.next())
        return Result<std::optional<JobCheckpoint>>::ok(std::nullopt);

    JobCheckpoint cp;
    cp.jobId            = q.value(0).toString();
    cp.phase            = jobPhaseFromString(q.value(1).toString());
    cp.offsetBytes      = q.value(2).toLongLong();
    cp.recordsProcessed = q.value(3).toInt();
    cp.savedAt          = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);

    return Result<std::optional<JobCheckpoint>>::ok(std::move(cp));
}

Result<void> IngestionRepository::clearCheckpoints(const QString& jobId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM job_checkpoints WHERE job_id = ?"));
    q.addBindValue(jobId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

// ── Worker claims ────────────────────────────────────────────────────────────

Result<void> IngestionRepository::claimJob(const WorkerClaim& claim)
{
    // Atomic: only insert if no existing claim
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO worker_claims (job_id, worker_id, claimed_at) "
        "SELECT ?, ?, ? "
        "WHERE NOT EXISTS (SELECT 1 FROM worker_claims WHERE job_id = ?)"));
    q.addBindValue(claim.jobId);
    q.addBindValue(claim.workerId);
    q.addBindValue(claim.claimedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(claim.jobId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<void>::err(ErrorCode::AlreadyExists,
                                  QStringLiteral("Job already claimed"));

    return Result<void>::ok();
}

Result<void> IngestionRepository::releaseJob(const QString& jobId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM worker_claims WHERE job_id = ?"));
    q.addBindValue(jobId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<std::optional<WorkerClaim>>
IngestionRepository::getActiveClaim(const QString& jobId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT job_id, worker_id, claimed_at FROM worker_claims WHERE job_id = ?"));
    q.addBindValue(jobId);

    if (!q.exec())
        return Result<std::optional<WorkerClaim>>::err(
            ErrorCode::DbError, q.lastError().text());

    if (!q.next())
        return Result<std::optional<WorkerClaim>>::ok(std::nullopt);

    WorkerClaim wc;
    wc.jobId     = q.value(0).toString();
    wc.workerId  = q.value(1).toString();
    wc.claimedAt = QDateTime::fromString(q.value(2).toString(), Qt::ISODateWithMs);

    return Result<std::optional<WorkerClaim>>::ok(std::move(wc));
}

Result<void> IngestionRepository::releaseAllClaims()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM worker_claims"));

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}
