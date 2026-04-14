#pragma once
// IIngestionRepository.h — ProctorOps
// Pure interface for ingestion job queue, dependency tracking, checkpoints,
// and worker claims. Used by JobScheduler and WorkerPool.

#include "models/Ingestion.h"
#include "utils/Result.h"
#include <QString>
#include <QList>
#include <optional>

class IIngestionRepository {
public:
    virtual ~IIngestionRepository() = default;

    // ── Jobs ───────────────────────────────────────────────────────────────
    virtual Result<IngestionJob>         insertJob(const IngestionJob& job)                   = 0;
    virtual Result<IngestionJob>         getJob(const QString& jobId)                         = 0;
    virtual Result<IngestionJob>         updateJobStatus(const QString& jobId,
                                                          JobStatus status,
                                                          const QString& lastError = {})      = 0;
    virtual Result<QList<IngestionJob>>  listJobsByStatus(JobStatus status)                   = 0;
    // Returns pending jobs whose dependencies are all Completed, ordered by priority desc.
    virtual Result<QList<IngestionJob>>  listReadyJobs()                                      = 0;
    virtual Result<void>                 cancelJob(const QString& jobId)                      = 0;
    // Crash recovery: transitions all CLAIMED / VALIDATING / IMPORTING / INDEXING
    // jobs to INTERRUPTED without incrementing retryCount.
    virtual Result<void>                 markInterrupted(const QList<QString>& jobIds)        = 0;
    // Returns all job IDs currently in an in-progress state (for crash recovery).
    virtual Result<QList<QString>>       findInProgressJobIds()                               = 0;

    // ── Dependencies ───────────────────────────────────────────────────────
    virtual Result<void>                 insertDependency(const JobDependency& dep)           = 0;
    virtual Result<QList<JobDependency>> getDependencies(const QString& jobId)                = 0;
    // Returns true if all dependsOnJobIds for the given jobId are Completed.
    virtual Result<bool>                 areDependenciesMet(const QString& jobId)            = 0;

    // ── Checkpoints ────────────────────────────────────────────────────────
    virtual Result<void>                         saveCheckpoint(const JobCheckpoint& cp)      = 0;
    virtual Result<std::optional<JobCheckpoint>> loadCheckpoint(const QString& jobId,
                                                                  JobPhase phase)             = 0;
    virtual Result<void>                         clearCheckpoints(const QString& jobId)       = 0;

    // ── Worker claims ──────────────────────────────────────────────────────
    // Atomic claim: inserts WorkerClaim only if no claim exists for jobId.
    virtual Result<void>                         claimJob(const WorkerClaim& claim)           = 0;
    virtual Result<void>                         releaseJob(const QString& jobId)             = 0;
    virtual Result<std::optional<WorkerClaim>>   getActiveClaim(const QString& jobId)        = 0;
    // Release all stale claims (used during crash recovery).
    virtual Result<void>                         releaseAllClaims()                           = 0;
};
