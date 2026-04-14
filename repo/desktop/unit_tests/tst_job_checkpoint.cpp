// tst_job_checkpoint.cpp — ProctorOps
// Unit tests for job checkpoint persistence: save, upsert, load, clear,
// and resume behavior.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#include "repositories/IngestionRepository.h"
#include "models/Ingestion.h"
#include "utils/Validation.h"

class TstJobCheckpoint : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void test_saveCheckpoint_new();
    void test_saveCheckpoint_upsert();
    void test_loadCheckpoint_exists();
    void test_loadCheckpoint_missing();
    void test_clearCheckpoints_all();
    void test_checkpoint_survivesPhaseTrans();
    void test_checkpoint_resumeSkipsProcessed();

private:
    void applySchema();
    QString createTestJob();

    QSqlDatabase m_db;
    int m_dbIndex = 0;
    static const QString s_userId;
};

const QString TstJobCheckpoint::s_userId = QStringLiteral("user-001");

void TstJobCheckpoint::initTestCase() {}
void TstJobCheckpoint::cleanupTestCase() {}

void TstJobCheckpoint::init()
{
    QString connName = QStringLiteral("tst_cp_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON;"));
    applySchema();
}

void TstJobCheckpoint::cleanup()
{
    QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstJobCheckpoint::applySchema()
{
    QSqlQuery q(m_db);

    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE users ("
        "  id TEXT PRIMARY KEY, username TEXT NOT NULL UNIQUE,"
        "  role TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'Active',"
        "  created_at TEXT NOT NULL, updated_at TEXT NOT NULL, created_by_user_id TEXT);"
    )), qPrintable(q.lastError().text()));

    QSqlQuery u(m_db);
    u.prepare(QStringLiteral("INSERT INTO users (id, username, role, status, created_at, updated_at)"
                              " VALUES (?, ?, 'Operator', 'Active', ?, ?)"));
    u.addBindValue(s_userId);
    u.addBindValue(QStringLiteral("testuser"));
    QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    u.addBindValue(now);
    u.addBindValue(now);
    u.exec();

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

    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE job_checkpoints ("
        "  job_id TEXT NOT NULL REFERENCES ingestion_jobs(id) ON DELETE CASCADE,"
        "  phase TEXT NOT NULL, offset_bytes INTEGER NOT NULL DEFAULT 0,"
        "  records_processed INTEGER NOT NULL DEFAULT 0, saved_at TEXT NOT NULL,"
        "  PRIMARY KEY (job_id, phase));"
    )), qPrintable(q.lastError().text()));
}

QString TstJobCheckpoint::createTestJob()
{
    IngestionRepository repo(m_db);
    IngestionJob job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.type = JobType::QuestionImport;
    job.status = JobStatus::Pending;
    job.priority = 5;
    job.sourceFilePath = QStringLiteral("/tmp/test.jsonl");
    job.createdAt = QDateTime::currentDateTimeUtc();
    job.retryCount = 0;
    job.currentPhase = JobPhase::Validate;
    job.createdByUserId = s_userId;
    repo.insertJob(job);
    return job.id;
}

// ── Tests ───────────────────────────────────────────────────────────────────────

void TstJobCheckpoint::test_saveCheckpoint_new()
{
    IngestionRepository repo(m_db);
    QString jobId = createTestJob();

    JobCheckpoint cp;
    cp.jobId = jobId;
    cp.phase = JobPhase::Validate;
    cp.offsetBytes = 1024;
    cp.recordsProcessed = 50;
    cp.savedAt = QDateTime::currentDateTimeUtc();

    auto result = repo.saveCheckpoint(cp);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");

    auto loaded = repo.loadCheckpoint(jobId, JobPhase::Validate);
    QVERIFY(loaded.isOk());
    QVERIFY(loaded.value().has_value());
    QCOMPARE(loaded.value()->offsetBytes, qint64(1024));
    QCOMPARE(loaded.value()->recordsProcessed, 50);
}

void TstJobCheckpoint::test_saveCheckpoint_upsert()
{
    IngestionRepository repo(m_db);
    QString jobId = createTestJob();

    // First save
    JobCheckpoint cp1;
    cp1.jobId = jobId;
    cp1.phase = JobPhase::Validate;
    cp1.offsetBytes = 500;
    cp1.recordsProcessed = 25;
    cp1.savedAt = QDateTime::currentDateTimeUtc();
    repo.saveCheckpoint(cp1);

    // Upsert with new values
    JobCheckpoint cp2;
    cp2.jobId = jobId;
    cp2.phase = JobPhase::Validate;
    cp2.offsetBytes = 2000;
    cp2.recordsProcessed = 100;
    cp2.savedAt = QDateTime::currentDateTimeUtc();
    auto result = repo.saveCheckpoint(cp2);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");

    auto loaded = repo.loadCheckpoint(jobId, JobPhase::Validate);
    QVERIFY(loaded.isOk());
    QVERIFY(loaded.value().has_value());
    QCOMPARE(loaded.value()->offsetBytes, qint64(2000));
    QCOMPARE(loaded.value()->recordsProcessed, 100);
}

void TstJobCheckpoint::test_loadCheckpoint_exists()
{
    IngestionRepository repo(m_db);
    QString jobId = createTestJob();

    JobCheckpoint cp;
    cp.jobId = jobId;
    cp.phase = JobPhase::Import;
    cp.offsetBytes = 4096;
    cp.recordsProcessed = 200;
    cp.savedAt = QDateTime::currentDateTimeUtc();
    repo.saveCheckpoint(cp);

    auto loaded = repo.loadCheckpoint(jobId, JobPhase::Import);
    QVERIFY(loaded.isOk());
    QVERIFY(loaded.value().has_value());
    QCOMPARE(loaded.value()->phase, JobPhase::Import);
    QCOMPARE(loaded.value()->offsetBytes, qint64(4096));
}

void TstJobCheckpoint::test_loadCheckpoint_missing()
{
    IngestionRepository repo(m_db);
    QString jobId = createTestJob();

    auto loaded = repo.loadCheckpoint(jobId, JobPhase::Index);
    QVERIFY(loaded.isOk());
    QVERIFY(!loaded.value().has_value());
}

void TstJobCheckpoint::test_clearCheckpoints_all()
{
    IngestionRepository repo(m_db);
    QString jobId = createTestJob();

    // Save checkpoints for multiple phases
    for (auto phase : {JobPhase::Validate, JobPhase::Import, JobPhase::Index}) {
        JobCheckpoint cp;
        cp.jobId = jobId;
        cp.phase = phase;
        cp.offsetBytes = 100;
        cp.recordsProcessed = 10;
        cp.savedAt = QDateTime::currentDateTimeUtc();
        repo.saveCheckpoint(cp);
    }

    auto clearResult = repo.clearCheckpoints(jobId);
    QVERIFY2(clearResult.isOk(), clearResult.isErr() ? qPrintable(clearResult.errorMessage()) : "");

    // All should be gone
    for (auto phase : {JobPhase::Validate, JobPhase::Import, JobPhase::Index}) {
        auto loaded = repo.loadCheckpoint(jobId, phase);
        QVERIFY(loaded.isOk());
        QVERIFY(!loaded.value().has_value());
    }
}

void TstJobCheckpoint::test_checkpoint_survivesPhaseTrans()
{
    IngestionRepository repo(m_db);
    QString jobId = createTestJob();

    // Save validate checkpoint
    JobCheckpoint validateCp;
    validateCp.jobId = jobId;
    validateCp.phase = JobPhase::Validate;
    validateCp.offsetBytes = 5000;
    validateCp.recordsProcessed = 250;
    validateCp.savedAt = QDateTime::currentDateTimeUtc();
    repo.saveCheckpoint(validateCp);

    // Save import checkpoint
    JobCheckpoint importCp;
    importCp.jobId = jobId;
    importCp.phase = JobPhase::Import;
    importCp.offsetBytes = 3000;
    importCp.recordsProcessed = 150;
    importCp.savedAt = QDateTime::currentDateTimeUtc();
    repo.saveCheckpoint(importCp);

    // Both should coexist
    auto v = repo.loadCheckpoint(jobId, JobPhase::Validate);
    auto i = repo.loadCheckpoint(jobId, JobPhase::Import);
    QVERIFY(v.isOk() && v.value().has_value());
    QVERIFY(i.isOk() && i.value().has_value());
    QCOMPARE(v.value()->recordsProcessed, 250);
    QCOMPARE(i.value()->recordsProcessed, 150);
}

void TstJobCheckpoint::test_checkpoint_resumeSkipsProcessed()
{
    IngestionRepository repo(m_db);
    QString jobId = createTestJob();

    // Save a checkpoint at record 100, offset 5000
    JobCheckpoint cp;
    cp.jobId = jobId;
    cp.phase = JobPhase::Validate;
    cp.offsetBytes = 5000;
    cp.recordsProcessed = 100;
    cp.savedAt = QDateTime::currentDateTimeUtc();
    repo.saveCheckpoint(cp);

    // Load and verify resume point
    auto loaded = repo.loadCheckpoint(jobId, JobPhase::Validate);
    QVERIFY(loaded.isOk());
    QVERIFY(loaded.value().has_value());

    // Resume means: seek to offsetBytes, start counting from recordsProcessed
    QCOMPARE(loaded.value()->offsetBytes, qint64(5000));
    QCOMPARE(loaded.value()->recordsProcessed, 100);
}

QTEST_MAIN(TstJobCheckpoint)
#include "tst_job_checkpoint.moc"
