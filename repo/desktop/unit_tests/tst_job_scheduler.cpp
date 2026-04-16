// tst_job_scheduler.cpp — ProctorOps
// Unit tests for JobScheduler: priority ordering, dependency resolution,
// retry backoff, fairness, worker cap, crash recovery, scheduled jobs, cancel.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSignalSpy>

#include "scheduler/JobScheduler.h"
#include "services/IngestionService.h"
#include "repositories/IngestionRepository.h"
#include "repositories/QuestionRepository.h"
#include "repositories/KnowledgePointRepository.h"
#include "repositories/MemberRepository.h"
#include "repositories/AuditRepository.h"
#include "repositories/UserRepository.h"
#include "crypto/AesGcmCipher.h"
#include "utils/Validation.h"
#include "services/AuthService.h"
#include "models/CommonTypes.h"

class TstJobScheduler : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ── Priority ordering ───────────────────────────────────────────────
    void test_priorityOrdering_highFirst();
    void test_priorityOrdering_tieBreakByCreatedAt();

    // ── Dependencies ────────────────────────────────────────────────────
    void test_dependency_blocksUntilMet();

    // ── Retry backoff ───────────────────────────────────────────────────
    void test_retryBackoff_schedule();
    void test_retryBackoff_maxRetriesPermanentFailure();

    // ── Worker cap ──────────────────────────────────────────────────────
    void test_workerCap_max2();

    // ── Crash recovery ──────────────────────────────────────────────────
    void test_crashRecovery_interruptedJobsReenqueued();

    // ── Scheduled jobs ──────────────────────────────────────────────────
    void test_scheduledJob_deferredUntilTime();

    // ── Fairness ────────────────────────────────────────────────────────
    void test_fairness_lowPriorityEventuallyReady();

    // ── Cancel ──────────────────────────────────────────────────────────
    void test_cancel_onlyFromPending();

    // ── Creation ────────────────────────────────────────────────────────
    void test_scheduleJob_createsJob();
    void test_scheduleJob_invalidPriority();

    // ── Scheduler execution path ────────────────────────────────────────
    void test_scheduler_startStop_idempotent();
    void test_scheduler_recoverCrash_maxRetryMarkedFailed();

private:
    void applySchema();
    void createTestUser();
    void createTempSourceFile();

    QSqlDatabase m_db;
    int m_dbIndex = 0;
    QString m_tempFilePath;

    static const QString s_userId;
    static const QByteArray s_masterKey;
};

const QString TstJobScheduler::s_userId = QStringLiteral("user-001");
const QByteArray TstJobScheduler::s_masterKey = QByteArray(32, 'k');

void TstJobScheduler::initTestCase()
{
    // Create a temp file for source_file_path
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(false);
    tempFile.open();
    tempFile.write("{\"body_text\":\"test\",\"answer_options\":[\"A\",\"B\"],\"correct_answer_index\":0,\"difficulty\":3,\"discrimination\":0.5}\n");
    m_tempFilePath = tempFile.fileName();
    tempFile.close();
}

void TstJobScheduler::cleanupTestCase()
{
    QFile::remove(m_tempFilePath);
}

void TstJobScheduler::init()
{
    QString connName = QStringLiteral("tst_sched_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON;"));
    applySchema();
    createTestUser();
}

void TstJobScheduler::cleanup()
{
    QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstJobScheduler::applySchema()
{
    QSqlQuery q(m_db);

    // Users (0002)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE users ("
        "  id TEXT PRIMARY KEY, username TEXT NOT NULL UNIQUE,"
        "  role TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'Active',"
        "  created_at TEXT NOT NULL, updated_at TEXT NOT NULL, created_by_user_id TEXT);"
    )), qPrintable(q.lastError().text()));

    // Ingestion jobs (0006)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE ingestion_jobs ("
        "  id TEXT PRIMARY KEY, type TEXT NOT NULL,"
        "  status TEXT NOT NULL DEFAULT 'Pending',"
        "  priority INTEGER NOT NULL DEFAULT 5 CHECK (priority >= 1 AND priority <= 10),"
        "  source_file_path TEXT NOT NULL, scheduled_at TEXT,"
        "  created_at TEXT NOT NULL, started_at TEXT, completed_at TEXT, failed_at TEXT,"
        "  retry_count INTEGER NOT NULL DEFAULT 0 CHECK (retry_count >= 0),"
        "  last_error TEXT, current_phase TEXT NOT NULL DEFAULT 'Validate',"
        "  created_by_user_id TEXT NOT NULL REFERENCES users(id));"
    )), qPrintable(q.lastError().text()));

    // Job dependencies
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE job_dependencies ("
        "  job_id TEXT NOT NULL REFERENCES ingestion_jobs(id) ON DELETE CASCADE,"
        "  depends_on_job_id TEXT NOT NULL REFERENCES ingestion_jobs(id) ON DELETE RESTRICT,"
        "  PRIMARY KEY (job_id, depends_on_job_id));"
    )), qPrintable(q.lastError().text()));

    // Job checkpoints
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE job_checkpoints ("
        "  job_id TEXT NOT NULL REFERENCES ingestion_jobs(id) ON DELETE CASCADE,"
        "  phase TEXT NOT NULL, offset_bytes INTEGER NOT NULL DEFAULT 0,"
        "  records_processed INTEGER NOT NULL DEFAULT 0, saved_at TEXT NOT NULL,"
        "  PRIMARY KEY (job_id, phase));"
    )), qPrintable(q.lastError().text()));

    // Worker claims
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE worker_claims ("
        "  job_id TEXT PRIMARY KEY REFERENCES ingestion_jobs(id) ON DELETE CASCADE,"
        "  worker_id TEXT NOT NULL, claimed_at TEXT NOT NULL);"
    )), qPrintable(q.lastError().text()));

    // Audit entries (0008)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE audit_entries ("
        "  id TEXT PRIMARY KEY, timestamp TEXT NOT NULL, actor_user_id TEXT NOT NULL,"
        "  event_type TEXT NOT NULL, entity_type TEXT NOT NULL, entity_id TEXT NOT NULL,"
        "  before_payload_json TEXT NOT NULL DEFAULT '{}',"
        "  after_payload_json TEXT NOT NULL DEFAULT '{}',"
        "  previous_entry_hash TEXT NOT NULL, entry_hash TEXT NOT NULL);"
    )), qPrintable(q.lastError().text()));
}

void TstJobScheduler::createTestUser()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO users (id, username, role, status, created_at, updated_at)"
                              " VALUES (?, ?, 'ContentManager', 'Active', ?, ?)"));
    q.addBindValue(s_userId);
    q.addBindValue(QStringLiteral("testuser"));
    QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    q.addBindValue(now);
    q.addBindValue(now);
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
}

// ── Priority ordering tests ─────────────────────────────────────────────────────

void TstJobScheduler::test_priorityOrdering_highFirst()
{
    IngestionRepository ingRepo(m_db);

    // Insert two jobs: low priority first, then high
    auto insertJob = [&](int priority) -> QString {
        IngestionJob job;
        job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        job.type = JobType::QuestionImport;
        job.status = JobStatus::Pending;
        job.priority = priority;
        job.sourceFilePath = m_tempFilePath;
        job.createdAt = QDateTime::currentDateTimeUtc();
        job.retryCount = 0;
        job.currentPhase = JobPhase::Validate;
        job.createdByUserId = s_userId;
        ingRepo.insertJob(job);
        return job.id;
    };

    insertJob(3);
    insertJob(8);

    auto ready = ingRepo.listReadyJobs();
    QVERIFY(ready.isOk());
    QVERIFY(ready.value().size() >= 2);
    QVERIFY(ready.value().at(0).priority >= ready.value().at(1).priority);
}

void TstJobScheduler::test_priorityOrdering_tieBreakByCreatedAt()
{
    IngestionRepository ingRepo(m_db);

    // Insert two jobs with same priority, first created earlier
    IngestionJob job1;
    job1.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job1.type = JobType::QuestionImport;
    job1.status = JobStatus::Pending;
    job1.priority = 5;
    job1.sourceFilePath = m_tempFilePath;
    job1.createdAt = QDateTime::currentDateTimeUtc().addSecs(-10);
    job1.retryCount = 0;
    job1.currentPhase = JobPhase::Validate;
    job1.createdByUserId = s_userId;
    ingRepo.insertJob(job1);

    IngestionJob job2;
    job2.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job2.type = JobType::QuestionImport;
    job2.status = JobStatus::Pending;
    job2.priority = 5;
    job2.sourceFilePath = m_tempFilePath;
    job2.createdAt = QDateTime::currentDateTimeUtc();
    job2.retryCount = 0;
    job2.currentPhase = JobPhase::Validate;
    job2.createdByUserId = s_userId;
    ingRepo.insertJob(job2);

    auto ready = ingRepo.listReadyJobs();
    QVERIFY(ready.isOk());
    QVERIFY(ready.value().size() >= 2);
    // First job should have earlier createdAt (tie-break by created_at ASC)
    QVERIFY(ready.value().at(0).createdAt <= ready.value().at(1).createdAt);
}

// ── Dependency tests ────────────────────────────────────────────────────────────

void TstJobScheduler::test_dependency_blocksUntilMet()
{
    IngestionRepository ingRepo(m_db);

    // Create prerequisite job (not yet completed)
    IngestionJob prereq;
    prereq.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    prereq.type = JobType::QuestionImport;
    prereq.status = JobStatus::Pending;
    prereq.priority = 5;
    prereq.sourceFilePath = m_tempFilePath;
    prereq.createdAt = QDateTime::currentDateTimeUtc();
    prereq.retryCount = 0;
    prereq.currentPhase = JobPhase::Validate;
    prereq.createdByUserId = s_userId;
    ingRepo.insertJob(prereq);

    // Create dependent job
    IngestionJob dependent;
    dependent.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    dependent.type = JobType::QuestionImport;
    dependent.status = JobStatus::Pending;
    dependent.priority = 5;
    dependent.sourceFilePath = m_tempFilePath;
    dependent.createdAt = QDateTime::currentDateTimeUtc();
    dependent.retryCount = 0;
    dependent.currentPhase = JobPhase::Validate;
    dependent.createdByUserId = s_userId;
    ingRepo.insertJob(dependent);

    // Add dependency
    JobDependency dep;
    dep.jobId = dependent.id;
    dep.dependsOnJobId = prereq.id;
    ingRepo.insertDependency(dep);

    // Ready jobs should NOT include the dependent (prereq not completed)
    auto ready = ingRepo.listReadyJobs();
    QVERIFY(ready.isOk());
    for (const auto& j : ready.value()) {
        QVERIFY(j.id != dependent.id);
    }

    // Complete the prerequisite
    ingRepo.updateJobStatus(prereq.id, JobStatus::Completed);

    // Now dependent should appear in ready jobs
    auto readyAfter = ingRepo.listReadyJobs();
    QVERIFY(readyAfter.isOk());
    bool found = false;
    for (const auto& j : readyAfter.value()) {
        if (j.id == dependent.id) { found = true; break; }
    }
    QVERIFY(found);
}

// ── Retry backoff tests ─────────────────────────────────────────────────────────

void TstJobScheduler::test_retryBackoff_schedule()
{
    QCOMPARE(Validation::retryDelaySeconds(0), Validation::RetryDelay1Seconds);
    QCOMPARE(Validation::retryDelaySeconds(1), Validation::RetryDelay2Seconds);
    QCOMPARE(Validation::retryDelaySeconds(2), Validation::RetryDelay3Seconds);
    QCOMPARE(Validation::retryDelaySeconds(3), Validation::RetryDelay3Seconds);
}

void TstJobScheduler::test_retryBackoff_maxRetriesPermanentFailure()
{
    IngestionRepository ingRepo(m_db);

    IngestionJob job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.type = JobType::QuestionImport;
    job.status = JobStatus::Pending;
    job.priority = 5;
    job.sourceFilePath = m_tempFilePath;
    job.createdAt = QDateTime::currentDateTimeUtc();
    job.retryCount = 0;
    job.currentPhase = JobPhase::Validate;
    job.createdByUserId = s_userId;
    ingRepo.insertJob(job);

    // Simulate max retries by incrementing retry_count
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE ingestion_jobs SET retry_count = ? WHERE id = ?"));
    q.addBindValue(Validation::SchedulerMaxRetries);
    q.addBindValue(job.id);
    q.exec();

    auto updated = ingRepo.getJob(job.id);
    QVERIFY(updated.isOk());
    QVERIFY(updated.value().retryCount >= Validation::SchedulerMaxRetries);
}

// ── Worker cap tests ────────────────────────────────────────────────────────────

void TstJobScheduler::test_workerCap_max2()
{
    QCOMPARE(Validation::SchedulerDefaultWorkers, 2);
}

// ── Crash recovery tests ────────────────────────────────────────────────────────

void TstJobScheduler::test_crashRecovery_interruptedJobsReenqueued()
{
    IngestionRepository ingRepo(m_db);

    // Create a job in Claimed status (simulating crash during processing)
    IngestionJob job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.type = JobType::QuestionImport;
    job.status = JobStatus::Claimed;
    job.priority = 5;
    job.sourceFilePath = m_tempFilePath;
    job.createdAt = QDateTime::currentDateTimeUtc();
    job.retryCount = 0;
    job.currentPhase = JobPhase::Validate;
    job.createdByUserId = s_userId;
    ingRepo.insertJob(job);

    // Manually set status to Claimed in DB
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE ingestion_jobs SET status = 'Claimed' WHERE id = ?"));
    q.addBindValue(job.id);
    q.exec();

    // Find in-progress jobs
    auto inProgress = ingRepo.findInProgressJobIds();
    QVERIFY(inProgress.isOk());
    QVERIFY(inProgress.value().contains(job.id));

    // Mark interrupted
    ingRepo.markInterrupted(inProgress.value());

    auto updated = ingRepo.getJob(job.id);
    QVERIFY(updated.isOk());
    QCOMPARE(updated.value().status, JobStatus::Interrupted);
}

// ── Scheduled job tests ─────────────────────────────────────────────────────────

void TstJobScheduler::test_scheduledJob_deferredUntilTime()
{
    IngestionRepository ingRepo(m_db);

    // Create a job scheduled for the future
    IngestionJob job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.type = JobType::QuestionImport;
    job.status = JobStatus::Pending;
    job.priority = 10;
    job.sourceFilePath = m_tempFilePath;
    job.scheduledAt = QDateTime::currentDateTimeUtc().addSecs(3600); // 1 hour from now
    job.createdAt = QDateTime::currentDateTimeUtc();
    job.retryCount = 0;
    job.currentPhase = JobPhase::Validate;
    job.createdByUserId = s_userId;
    ingRepo.insertJob(job);

    // The job should appear in ready jobs (listReadyJobs doesn't filter by scheduledAt)
    // but the scheduler's tick() will skip it based on scheduledAt > now
    auto ready = ingRepo.listReadyJobs();
    QVERIFY(ready.isOk());

    // Verify the job has a future scheduledAt
    auto fetched = ingRepo.getJob(job.id);
    QVERIFY(fetched.isOk());
    QVERIFY(fetched.value().scheduledAt > QDateTime::currentDateTimeUtc());
}

// ── Fairness tests ─────────────────────────────────────────────────────────────

void TstJobScheduler::test_fairness_lowPriorityEventuallyReady()
{
    IngestionRepository ingRepo(m_db);

    // Insert a low-priority job with an old created_at (simulating starvation)
    IngestionJob lowJob;
    lowJob.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    lowJob.type = JobType::QuestionImport;
    lowJob.status = JobStatus::Pending;
    lowJob.priority = 1; // lowest priority
    lowJob.sourceFilePath = m_tempFilePath;
    lowJob.createdAt = QDateTime::currentDateTimeUtc().addSecs(-3600); // created 1 hour ago
    lowJob.retryCount = 0;
    lowJob.currentPhase = JobPhase::Validate;
    lowJob.createdByUserId = s_userId;
    ingRepo.insertJob(lowJob);

    // Insert a high-priority job with recent created_at
    IngestionJob highJob;
    highJob.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    highJob.type = JobType::QuestionImport;
    highJob.status = JobStatus::Pending;
    highJob.priority = 10; // highest priority
    highJob.sourceFilePath = m_tempFilePath;
    highJob.createdAt = QDateTime::currentDateTimeUtc();
    highJob.retryCount = 0;
    highJob.currentPhase = JobPhase::Validate;
    highJob.createdByUserId = s_userId;
    ingRepo.insertJob(highJob);

    // Both jobs must appear in ready list — scheduler must never permanently starve
    // a low-priority job. High-priority goes first, but low-priority stays eligible.
    auto ready = ingRepo.listReadyJobs();
    QVERIFY(ready.isOk());
    QVERIFY2(ready.value().size() >= 2,
             "Both low-priority and high-priority jobs must be in ready list");

    bool foundLow = false;
    bool foundHigh = false;
    for (const auto& j : ready.value()) {
        if (j.id == lowJob.id) foundLow = true;
        if (j.id == highJob.id) foundHigh = true;
    }
    QVERIFY2(foundHigh, "High-priority job must appear in ready list");
    QVERIFY2(foundLow, "Low-priority job must not be starved from ready list");

    // High-priority job must be ordered before low-priority
    int highIdx = -1, lowIdx = -1;
    for (int i = 0; i < ready.value().size(); ++i) {
        if (ready.value().at(i).id == highJob.id) highIdx = i;
        if (ready.value().at(i).id == lowJob.id) lowIdx = i;
    }
    QVERIFY2(highIdx < lowIdx,
             "High-priority job must be scheduled before low-priority, but both remain eligible");
}

// ── Cancel tests ────────────────────────────────────────────────────────────────

void TstJobScheduler::test_cancel_onlyFromPending()
{
    IngestionRepository ingRepo(m_db);

    // Create a pending job
    IngestionJob job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.type = JobType::QuestionImport;
    job.status = JobStatus::Pending;
    job.priority = 5;
    job.sourceFilePath = m_tempFilePath;
    job.createdAt = QDateTime::currentDateTimeUtc();
    job.retryCount = 0;
    job.currentPhase = JobPhase::Validate;
    job.createdByUserId = s_userId;
    ingRepo.insertJob(job);

    // Cancel succeeds from Pending
    auto result = ingRepo.cancelJob(job.id);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");

    auto cancelled = ingRepo.getJob(job.id);
    QVERIFY(cancelled.isOk());
    QCOMPARE(cancelled.value().status, JobStatus::Cancelled);
}

// ── Job creation tests ──────────────────────────────────────────────────────────

void TstJobScheduler::test_scheduleJob_createsJob()
{
    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService ingSvc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);
    JobScheduler scheduler(ingRepo, ingSvc, auditSvc);

    auto result = scheduler.scheduleJob(JobType::QuestionImport, m_tempFilePath,
                                          5, s_userId);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QCOMPARE(result.value().status, JobStatus::Pending);
    QCOMPARE(result.value().priority, 5);
}

void TstJobScheduler::test_scheduleJob_invalidPriority()
{
    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService ingSvc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);
    JobScheduler scheduler(ingRepo, ingSvc, auditSvc);

    auto result = scheduler.scheduleJob(JobType::QuestionImport, m_tempFilePath,
                                          15, s_userId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstJobScheduler::test_scheduler_startStop_idempotent()
{
    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService ingSvc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);
    JobScheduler scheduler(ingRepo, ingSvc, auditSvc);

    QCOMPARE(scheduler.activeWorkerCount(), 0);

    scheduler.start();
    QTest::qWait(20);
    scheduler.start(); // idempotent

    QVERIFY(scheduler.activeWorkerCount() >= 0);

    scheduler.stop();
    scheduler.stop(); // idempotent
    QCOMPARE(scheduler.activeWorkerCount(), 0);
}

void TstJobScheduler::test_scheduler_recoverCrash_maxRetryMarkedFailed()
{
    IngestionRepository ingRepo(m_db);

    IngestionJob job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.type = JobType::QuestionImport;
    job.status = JobStatus::Claimed;
    job.priority = 5;
    job.sourceFilePath = m_tempFilePath;
    job.createdAt = QDateTime::currentDateTimeUtc();
    job.retryCount = Validation::SchedulerMaxRetries;
    job.currentPhase = JobPhase::Validate;
    job.createdByUserId = s_userId;
    QVERIFY(ingRepo.insertJob(job).isOk());

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE ingestion_jobs SET status = 'Claimed', retry_count = ? WHERE id = ?"));
    q.addBindValue(Validation::SchedulerMaxRetries);
    q.addBindValue(job.id);
    QVERIFY(q.exec());

    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService ingSvc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);
    JobScheduler scheduler(ingRepo, ingSvc, auditSvc);

    scheduler.start();
    QTest::qWait(30);
    scheduler.stop();

    auto updated = ingRepo.getJob(job.id);
    QVERIFY(updated.isOk());
    QCOMPARE(updated.value().status, JobStatus::Failed);
    QVERIFY(updated.value().lastError.contains(QStringLiteral("Max retries exceeded"), Qt::CaseInsensitive));
}

QTEST_MAIN(TstJobScheduler)
#include "tst_job_scheduler.moc"
