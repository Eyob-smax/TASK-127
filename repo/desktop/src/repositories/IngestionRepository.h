#pragma once
// IngestionRepository.h — ProctorOps
// Concrete SQLite implementation of IIngestionRepository.
// Manages ingestion jobs, dependencies, checkpoints, and worker claims.

#include "IIngestionRepository.h"
#include <QSqlDatabase>

class IngestionRepository : public IIngestionRepository {
public:
    explicit IngestionRepository(QSqlDatabase& db);

    // ── Jobs ───────────────────────────────────────────────────────────────
    Result<IngestionJob>         insertJob(const IngestionJob& job) override;
    Result<IngestionJob>         getJob(const QString& jobId) override;
    Result<IngestionJob>         updateJobStatus(const QString& jobId,
                                                   JobStatus status,
                                                   const QString& lastError = {}) override;
    Result<QList<IngestionJob>>  listJobsByStatus(JobStatus status) override;
    Result<QList<IngestionJob>>  listReadyJobs() override;
    Result<void>                 cancelJob(const QString& jobId) override;
    Result<void>                 markInterrupted(const QList<QString>& jobIds) override;
    Result<QList<QString>>       findInProgressJobIds() override;

    // ── Dependencies ───────────────────────────────────────────────────────
    Result<void>                 insertDependency(const JobDependency& dep) override;
    Result<QList<JobDependency>> getDependencies(const QString& jobId) override;
    Result<bool>                 areDependenciesMet(const QString& jobId) override;

    // ── Checkpoints ────────────────────────────────────────────────────────
    Result<void>                         saveCheckpoint(const JobCheckpoint& cp) override;
    Result<std::optional<JobCheckpoint>> loadCheckpoint(const QString& jobId,
                                                          JobPhase phase) override;
    Result<void>                         clearCheckpoints(const QString& jobId) override;

    // ── Worker claims ──────────────────────────────────────────────────────
    Result<void>                         claimJob(const WorkerClaim& claim) override;
    Result<void>                         releaseJob(const QString& jobId) override;
    Result<std::optional<WorkerClaim>>   getActiveClaim(const QString& jobId) override;
    Result<void>                         releaseAllClaims() override;

private:
    QSqlDatabase& m_db;

    static IngestionJob rowToJob(const QSqlQuery& q);

    static QString jobTypeToString(JobType t);
    static JobType jobTypeFromString(const QString& s);
    static QString jobStatusToString(JobStatus s);
    static JobStatus jobStatusFromString(const QString& s);
    static QString jobPhaseToString(JobPhase p);
    static JobPhase jobPhaseFromString(const QString& s);
};
