#pragma once
// JobScheduler.h — ProctorOps
// Priority-aware job scheduling with dependency resolution, retry backoff,
// starvation-safe fairness rules, and crash recovery.

#include "repositories/IIngestionRepository.h"
#include "services/IngestionService.h"
#include "services/AuditService.h"
#include "utils/Result.h"
#include "models/Ingestion.h"

#include <QObject>
#include <QTimer>
#include <QThreadPool>
#include <QFutureWatcher>
#include <QMutex>
#include <QDateTime>
#include <QHash>
#include <QList>

class JobScheduler : public QObject {
    Q_OBJECT

public:
    JobScheduler(IIngestionRepository& ingestionRepo,
                 IngestionService& ingestionService,
                 AuditService& auditService,
                 QObject* parent = nullptr);

    ~JobScheduler() override;

    /// Start the scheduler: recover from crash, begin tick loop.
    void start();

    /// Gracefully stop: release claims, stop timer.
    void stop();

    /// Submit a new job through the scheduler (delegates to IngestionService::createJob).
    [[nodiscard]] Result<IngestionJob> scheduleJob(
        JobType type,
        const QString& sourceFilePath,
        int priority,
        const QString& actorUserId,
        const QDateTime& scheduledAt = {},
        const QStringList& dependsOnJobIds = {});

    /// Current number of jobs being executed by worker threads.
    [[nodiscard]] int activeWorkerCount() const;

private slots:
    /// Main scheduling iteration — called by QTimer.
    void tick();

private:
    IIngestionRepository& m_ingestionRepo;
    IngestionService&     m_ingestionService;
    AuditService&         m_auditService;

    QTimer*      m_timer      = nullptr;
    QThreadPool* m_threadPool = nullptr;
    bool         m_running    = false;

    // ── Worker tracking ───────────────────────────────────────────────────

    mutable QMutex m_mutex;
    QHash<QString, QFutureWatcher<void>*> m_activeJobs; // jobId → watcher

    // ── Fairness tracking ─────────────────────────────────────────────────

    struct CompletionRecord {
        int    priority;
        qint64 durationMs;
    };
    QList<CompletionRecord> m_recentCompletions; // rolling window of last 10
    static constexpr int FairnessWindowSize = 10;

    // ── Internal ──────────────────────────────────────────────────────────

    /// Recover jobs that were in-progress when the app last crashed.
    void recoverFromCrash();

    /// Dispatch a job to a worker thread.
    void dispatchJob(const IngestionJob& job);

    /// Called when a worker thread finishes a job successfully.
    void onJobCompleted(const QString& jobId);

    /// Called when a worker thread fails a job.
    void onJobFailed(const QString& jobId, const QString& error);

    /// Compute average completion time of higher-priority jobs in the rolling window.
    qint64 averageHigherPriorityCompletionMs(int priority) const;

    /// Timer intervals
    static constexpr int TickIntervalActiveMs = 1000;  // 1s when jobs pending
    static constexpr int TickIntervalIdleMs   = 5000;  // 5s when idle
};
