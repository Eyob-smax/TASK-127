#pragma once
// Ingestion.h — ProctorOps
// Domain models for the ingestion scheduler: jobs, dependencies, checkpoints,
// worker claims, and retry state.

#include <QString>
#include <QStringList>
#include <QDateTime>

// ── JobType ───────────────────────────────────────────────────────────────────
enum class JobType {
    QuestionImport, // imports question records from a JSON Lines file
    RosterImport,   // imports member roster from a CSV file
};

// ── JobStatus ─────────────────────────────────────────────────────────────────
// State machine: PENDING → CLAIMED → VALIDATING → IMPORTING → INDEXING → COMPLETED
//                                                            ↘ FAILED (retryable)
//                                               ↘ INTERRUPTED (crash; resume from checkpoint)
//                         ↗ CANCELLED (only from PENDING)
enum class JobStatus {
    Pending,      // waiting for a worker; dependencies may not yet be met
    Claimed,      // a worker has taken ownership (see WorkerClaim)
    Validating,   // VALIDATE phase running
    Importing,    // IMPORT phase running
    Indexing,     // INDEX phase running
    Completed,    // all three phases succeeded
    Failed,       // a phase failed; retryCount < SchedulerMaxRetries enables retry
    Interrupted,  // app crashed during processing; resume from last checkpoint
    Cancelled,    // operator-cancelled while Pending
};

// ── JobPhase ──────────────────────────────────────────────────────────────────
// Each phase has its own checkpoint. A job must complete one phase before starting the next.
enum class JobPhase {
    Validate,  // parse file, check schema, validate all rows
    Import,    // write validated rows to domain tables
    Index,     // build any additional indexes or materialized data
};

// ── IngestionJob ──────────────────────────────────────────────────────────────
struct IngestionJob {
    QString      id;               // UUID
    JobType      type;
    JobStatus    status;
    int          priority;         // 1–10; higher values run first among Pending jobs
    QString      sourceFilePath;   // absolute path to the source file
    QDateTime    scheduledAt;      // null = run as soon as a worker is available
    QDateTime    createdAt;
    QDateTime    startedAt;        // null until first worker claim
    QDateTime    completedAt;      // null until Completed
    QDateTime    failedAt;         // null unless Failed
    int          retryCount;       // 0 on first attempt; incremented per retry
    QString      lastError;        // human-readable last failure message
    JobPhase     currentPhase;
    QString      createdByUserId;
    // Invariant: retryCount <= Validation::SchedulerMaxRetries
    // Invariant: priority in [1, 10]
};

// ── JobDependency ─────────────────────────────────────────────────────────────
// A job may not start until all its prerequisite jobs reach Completed.
struct JobDependency {
    QString  jobId;
    QString  dependsOnJobId;
};

// ── JobCheckpoint ────────────────────────────────────────────────────────────
// Persisted after each committed batch within a phase so the job can resume
// after a crash without reprocessing already-committed rows.
struct JobCheckpoint {
    QString   jobId;
    JobPhase  phase;
    qint64    offsetBytes;         // byte offset in the source file (for resume)
    int       recordsProcessed;    // rows successfully committed so far
    QDateTime savedAt;
};

// ── WorkerClaim ──────────────────────────────────────────────────────────────
// Tracks which worker thread owns a job to prevent double-processing.
// Released when the job transitions to Completed, Failed, or Interrupted.
struct WorkerClaim {
    QString   jobId;
    QString   workerId;   // UUID generated per WorkerPool thread; unique per run
    QDateTime claimedAt;
};
