// tst_ingestion_service.cpp — ProctorOps
// Unit tests for IngestionService: question import (JSONL), roster import (CSV),
// validation phases, checkpoint resume, and error handling.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryFile>
#include <QTextStream>

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

class TstIngestionService : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ── Question import (JSONL) ─────────────────────────────────────────
    void test_questionImport_validJsonl();
    void test_questionImport_invalidDifficulty();
    void test_questionImport_duplicateExternalId();
    void test_questionImport_missingRequiredField();
    void test_questionImport_kpPathResolution();
    void test_questionImport_tagResolution();

    // ── Roster import (CSV) ─────────────────────────────────────────────
    void test_rosterImport_validCsv();
    void test_rosterImport_piiEncrypted();
    void test_rosterImport_termCardCreated();
    void test_rosterImport_punchCardCreated();
    void test_rosterImport_invalidMobile();
    void test_rosterImport_invalidDateRange();

    // ── Job lifecycle ───────────────────────────────────────────────────
    void test_createJob_success();
    void test_createJob_invalidPriority();
    void test_cancelJob_pending();
    void test_cancelJob_notPending();

private:
    void applySchema();
    void createTestUser();
    QString writeTempFile(const QString& content, const QString& suffix);

    QSqlDatabase m_db;
    int m_dbIndex = 0;
    QStringList m_tempFiles;

    static const QString s_userId;
    static const QByteArray s_masterKey;
};

const QString TstIngestionService::s_userId = QStringLiteral("user-001");
const QByteArray TstIngestionService::s_masterKey = QByteArray(32, 'k');

void TstIngestionService::initTestCase() {}

void TstIngestionService::cleanupTestCase()
{
    for (const auto& f : m_tempFiles)
        QFile::remove(f);
}

void TstIngestionService::init()
{
    QString connName = QStringLiteral("tst_ing_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON;"));
    applySchema();
    createTestUser();
}

void TstIngestionService::cleanup()
{
    QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstIngestionService::applySchema()
{
    QSqlQuery q(m_db);

    // Users
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE users ("
        "  id TEXT PRIMARY KEY, username TEXT NOT NULL UNIQUE,"
        "  role TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'Active',"
        "  created_at TEXT NOT NULL, updated_at TEXT NOT NULL, created_by_user_id TEXT);"
    )), qPrintable(q.lastError().text()));

    // Members
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE members ("
        "  id TEXT PRIMARY KEY, member_id TEXT NOT NULL UNIQUE, member_id_hash TEXT,"
        "  barcode_encrypted TEXT NOT NULL, mobile_encrypted TEXT NOT NULL,"
        "  name_encrypted TEXT NOT NULL, deleted INTEGER NOT NULL DEFAULT 0,"
        "  created_at TEXT NOT NULL, updated_at TEXT NOT NULL);"
    )), qPrintable(q.lastError().text()));

    // Term cards
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE term_cards ("
        "  id TEXT PRIMARY KEY, member_id TEXT NOT NULL REFERENCES members(id) ON DELETE CASCADE,"
        "  term_start TEXT NOT NULL, term_end TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1,"
        "  created_at TEXT NOT NULL, CONSTRAINT chk_term_dates CHECK (term_end > term_start));"
    )), qPrintable(q.lastError().text()));

    // Punch cards
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE punch_cards ("
        "  id TEXT PRIMARY KEY, member_id TEXT NOT NULL REFERENCES members(id) ON DELETE CASCADE,"
        "  product_code TEXT NOT NULL, initial_balance INTEGER NOT NULL CHECK (initial_balance >= 0),"
        "  current_balance INTEGER NOT NULL CHECK (current_balance >= 0),"
        "  created_at TEXT NOT NULL, updated_at TEXT NOT NULL);"
    )), qPrintable(q.lastError().text()));

    // Knowledge points
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE knowledge_points ("
        "  id TEXT PRIMARY KEY, name TEXT NOT NULL,"
        "  parent_id TEXT REFERENCES knowledge_points(id) ON DELETE RESTRICT,"
        "  position INTEGER NOT NULL DEFAULT 0, path TEXT NOT NULL,"
        "  created_at TEXT NOT NULL, deleted INTEGER NOT NULL DEFAULT 0);"
    )), qPrintable(q.lastError().text()));

    // Tags
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE tags ("
        "  id TEXT PRIMARY KEY, name TEXT NOT NULL, created_at TEXT NOT NULL,"
        "  CONSTRAINT uq_tag_name UNIQUE (name));"
    )), qPrintable(q.lastError().text()));

    // Questions
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE questions ("
        "  id TEXT PRIMARY KEY, body_text TEXT NOT NULL,"
        "  answer_options_json TEXT NOT NULL,"
        "  correct_answer_index INTEGER NOT NULL CHECK (correct_answer_index >= 0),"
        "  difficulty INTEGER NOT NULL CHECK (difficulty >= 1 AND difficulty <= 5),"
        "  discrimination REAL NOT NULL CHECK (discrimination >= 0.00 AND discrimination <= 1.00),"
        "  status TEXT NOT NULL DEFAULT 'Active', external_id TEXT,"
        "  created_at TEXT NOT NULL, updated_at TEXT NOT NULL,"
        "  created_by_user_id TEXT NOT NULL REFERENCES users(id),"
        "  updated_by_user_id TEXT NOT NULL REFERENCES users(id));"
    )), qPrintable(q.lastError().text()));

    // Question-KP mappings
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE question_kp_mappings ("
        "  question_id TEXT NOT NULL REFERENCES questions(id) ON DELETE CASCADE,"
        "  knowledge_point_id TEXT NOT NULL REFERENCES knowledge_points(id) ON DELETE RESTRICT,"
        "  mapped_at TEXT NOT NULL, mapped_by_user_id TEXT NOT NULL REFERENCES users(id),"
        "  PRIMARY KEY (question_id, knowledge_point_id));"
    )), qPrintable(q.lastError().text()));

    // Question-Tag mappings
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE question_tag_mappings ("
        "  question_id TEXT NOT NULL REFERENCES questions(id) ON DELETE CASCADE,"
        "  tag_id TEXT NOT NULL REFERENCES tags(id) ON DELETE RESTRICT,"
        "  applied_at TEXT NOT NULL, applied_by_user_id TEXT NOT NULL REFERENCES users(id),"
        "  PRIMARY KEY (question_id, tag_id));"
    )), qPrintable(q.lastError().text()));

    // Ingestion jobs
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
        "CREATE TABLE job_dependencies ("
        "  job_id TEXT NOT NULL REFERENCES ingestion_jobs(id) ON DELETE CASCADE,"
        "  depends_on_job_id TEXT NOT NULL REFERENCES ingestion_jobs(id) ON DELETE RESTRICT,"
        "  PRIMARY KEY (job_id, depends_on_job_id));"
    )), qPrintable(q.lastError().text()));

    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE job_checkpoints ("
        "  job_id TEXT NOT NULL REFERENCES ingestion_jobs(id) ON DELETE CASCADE,"
        "  phase TEXT NOT NULL, offset_bytes INTEGER NOT NULL DEFAULT 0,"
        "  records_processed INTEGER NOT NULL DEFAULT 0, saved_at TEXT NOT NULL,"
        "  PRIMARY KEY (job_id, phase));"
    )), qPrintable(q.lastError().text()));

    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE worker_claims ("
        "  job_id TEXT PRIMARY KEY REFERENCES ingestion_jobs(id) ON DELETE CASCADE,"
        "  worker_id TEXT NOT NULL, claimed_at TEXT NOT NULL);"
    )), qPrintable(q.lastError().text()));

    // Audit entries
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE audit_entries ("
        "  id TEXT PRIMARY KEY, timestamp TEXT NOT NULL, actor_user_id TEXT NOT NULL,"
        "  event_type TEXT NOT NULL, entity_type TEXT NOT NULL, entity_id TEXT NOT NULL,"
        "  before_payload_json TEXT NOT NULL DEFAULT '{}',"
        "  after_payload_json TEXT NOT NULL DEFAULT '{}',"
        "  previous_entry_hash TEXT NOT NULL, entry_hash TEXT NOT NULL);"
    )), qPrintable(q.lastError().text()));
}

void TstIngestionService::createTestUser()
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

QString TstIngestionService::writeTempFile(const QString& content, const QString& suffix)
{
    QTemporaryFile file;
    file.setAutoRemove(false);
    file.setFileTemplate(QDir::tempPath() + QStringLiteral("/proctorops_test_XXXXXX") + suffix);
    if (!file.open())
        return {};
    file.write(content.toUtf8());
    QString path = file.fileName();
    file.close();
    m_tempFiles.append(path);
    return path;
}

// ── Question import tests ───────────────────────────────────────────────────────

void TstIngestionService::test_questionImport_validJsonl()
{
    QString content = QStringLiteral(
        "{\"body_text\":\"What is safety?\",\"answer_options\":[\"A\",\"B\"],\"correct_answer_index\":0,\"difficulty\":3,\"discrimination\":0.5}\n"
        "{\"body_text\":\"What is ground?\",\"answer_options\":[\"C\",\"D\"],\"correct_answer_index\":1,\"difficulty\":2,\"discrimination\":0.8}\n"
    );
    QString path = writeTempFile(content, QStringLiteral(".jsonl"));

    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService svc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);

    auto job = svc.createJob(JobType::QuestionImport, path, 5, s_userId);
    QVERIFY2(job.isOk(), job.isErr() ? qPrintable(job.errorMessage()) : "");

    auto exec = svc.executeJob(job.value().id, QStringLiteral("worker-1"));
    QVERIFY2(exec.isOk(), exec.isErr() ? qPrintable(exec.errorMessage()) : "");

    // Verify questions were imported
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM questions"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 2);
}

void TstIngestionService::test_questionImport_invalidDifficulty()
{
    QString content = QStringLiteral(
        "{\"body_text\":\"Test\",\"answer_options\":[\"A\",\"B\"],\"correct_answer_index\":0,\"difficulty\":99,\"discrimination\":0.5}\n"
    );
    QString path = writeTempFile(content, QStringLiteral(".jsonl"));

    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService svc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);

    auto job = svc.createJob(JobType::QuestionImport, path, 5, s_userId);
    QVERIFY(job.isOk());

    // Validation phase should fail (100% error rate)
    auto exec = svc.executeJob(job.value().id, QStringLiteral("worker-1"));
    QVERIFY(exec.isErr());
}

void TstIngestionService::test_questionImport_duplicateExternalId()
{
    // Pre-insert a question with external_id
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO questions (id, body_text, answer_options_json,"
                              " correct_answer_index, difficulty, discrimination, status,"
                              " external_id, created_at, updated_at, created_by_user_id, updated_by_user_id)"
                              " VALUES (?, ?, '[\"A\",\"B\"]', 0, 3, 0.5, 'Draft', ?, ?, ?, ?, ?)"));
    q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    q.addBindValue(QStringLiteral("Existing question"));
    q.addBindValue(QStringLiteral("EXT-001"));
    QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    q.addBindValue(now);
    q.addBindValue(now);
    q.addBindValue(s_userId);
    q.addBindValue(s_userId);
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));

    QString content = QStringLiteral(
        "{\"body_text\":\"Duplicate\",\"answer_options\":[\"A\",\"B\"],\"correct_answer_index\":0,\"difficulty\":3,\"discrimination\":0.5,\"external_id\":\"EXT-001\"}\n"
    );
    QString path = writeTempFile(content, QStringLiteral(".jsonl"));

    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService svc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);

    auto job = svc.createJob(JobType::QuestionImport, path, 5, s_userId);
    QVERIFY(job.isOk());

    // Should fail validation (duplicate external_id → 100% error rate)
    auto exec = svc.executeJob(job.value().id, QStringLiteral("worker-1"));
    QVERIFY(exec.isErr());
}

void TstIngestionService::test_questionImport_missingRequiredField()
{
    QString content = QStringLiteral(
        "{\"answer_options\":[\"A\",\"B\"],\"correct_answer_index\":0,\"difficulty\":3,\"discrimination\":0.5}\n"
    );
    QString path = writeTempFile(content, QStringLiteral(".jsonl"));

    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService svc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);

    auto job = svc.createJob(JobType::QuestionImport, path, 5, s_userId);
    QVERIFY(job.isOk());

    auto exec = svc.executeJob(job.value().id, QStringLiteral("worker-1"));
    QVERIFY(exec.isErr()); // Missing body_text
}

void TstIngestionService::test_questionImport_kpPathResolution()
{
    QString content = QStringLiteral(
        "{\"body_text\":\"KP test\",\"answer_options\":[\"A\",\"B\"],\"correct_answer_index\":0,\"difficulty\":3,\"discrimination\":0.5,\"knowledge_point_path\":\"Safety/Electrical\"}\n"
    );
    QString path = writeTempFile(content, QStringLiteral(".jsonl"));

    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService svc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);

    auto job = svc.createJob(JobType::QuestionImport, path, 5, s_userId);
    QVERIFY(job.isOk());

    auto exec = svc.executeJob(job.value().id, QStringLiteral("worker-1"));
    QVERIFY2(exec.isOk(), exec.isErr() ? qPrintable(exec.errorMessage()) : "");

    // Verify KP tree was created
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM knowledge_points"));
    QVERIFY(q.next());
    QVERIFY(q.value(0).toInt() >= 2); // Safety + Electrical

    // Verify mapping exists
    q.exec(QStringLiteral("SELECT COUNT(*) FROM question_kp_mappings"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
}

void TstIngestionService::test_questionImport_tagResolution()
{
    QString content = QStringLiteral(
        "{\"body_text\":\"Tag test\",\"answer_options\":[\"A\",\"B\"],\"correct_answer_index\":0,\"difficulty\":3,\"discrimination\":0.5,\"tags\":[\"Safety\",\"Electrical\"]}\n"
    );
    QString path = writeTempFile(content, QStringLiteral(".jsonl"));

    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService svc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);

    auto job = svc.createJob(JobType::QuestionImport, path, 5, s_userId);
    QVERIFY(job.isOk());

    auto exec = svc.executeJob(job.value().id, QStringLiteral("worker-1"));
    QVERIFY2(exec.isOk(), exec.isErr() ? qPrintable(exec.errorMessage()) : "");

    // Verify tags were created
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM tags"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 2);

    // Verify tag mappings
    q.exec(QStringLiteral("SELECT COUNT(*) FROM question_tag_mappings"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 2);
}

// ── Roster import tests ─────────────────────────────────────────────────────────

void TstIngestionService::test_rosterImport_validCsv()
{
    QString content = QStringLiteral(
        "member_id,name,barcode,mobile,term_start,term_end\n"
        "M001,John Doe,BC001,5551234567,2025-01-01,2025-12-31\n"
        "M002,Jane Smith,BC002,5559876543,2025-01-01,2025-12-31\n"
    );
    QString path = writeTempFile(content, QStringLiteral(".csv"));

    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService svc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);

    auto job = svc.createJob(JobType::RosterImport, path, 5, s_userId);
    QVERIFY2(job.isOk(), job.isErr() ? qPrintable(job.errorMessage()) : "");

    auto exec = svc.executeJob(job.value().id, QStringLiteral("worker-1"));
    QVERIFY2(exec.isOk(), exec.isErr() ? qPrintable(exec.errorMessage()) : "");

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM members"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 2);
}

void TstIngestionService::test_rosterImport_piiEncrypted()
{
    QString content = QStringLiteral(
        "member_id,name,barcode,mobile,term_start,term_end\n"
        "M100,Secret Name,BCENC,5551112222,2025-01-01,2025-12-31\n"
    );
    QString path = writeTempFile(content, QStringLiteral(".csv"));

    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService svc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);

    auto job = svc.createJob(JobType::RosterImport, path, 5, s_userId);
    svc.executeJob(job.value().id, QStringLiteral("worker-1"));

    // Verify PII is encrypted (not plaintext in DB)
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT name_encrypted FROM members WHERE member_id = 'M100'"));
    QVERIFY(q.next());
    QString stored = q.value(0).toString();
    QVERIFY(stored != QStringLiteral("Secret Name"));
    QVERIFY(!stored.isEmpty());
}

void TstIngestionService::test_rosterImport_termCardCreated()
{
    QString content = QStringLiteral(
        "member_id,name,barcode,mobile,term_start,term_end\n"
        "M200,Term Test,BCTC,5552223333,2025-01-01,2025-12-31\n"
    );
    QString path = writeTempFile(content, QStringLiteral(".csv"));

    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService svc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);

    auto job = svc.createJob(JobType::RosterImport, path, 5, s_userId);
    svc.executeJob(job.value().id, QStringLiteral("worker-1"));

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM term_cards"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);
}

void TstIngestionService::test_rosterImport_punchCardCreated()
{
    QString content = QStringLiteral(
        "member_id,name,barcode,mobile,term_start,term_end,product_code,punch_balance\n"
        "M300,Punch Test,BCPC,5553334444,2025-01-01,2025-12-31,STANDARD,20\n"
    );
    QString path = writeTempFile(content, QStringLiteral(".csv"));

    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService svc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);

    auto job = svc.createJob(JobType::RosterImport, path, 5, s_userId);
    svc.executeJob(job.value().id, QStringLiteral("worker-1"));

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT current_balance FROM punch_cards"));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 20);
}

void TstIngestionService::test_rosterImport_invalidMobile()
{
    QString content = QStringLiteral(
        "member_id,name,barcode,mobile,term_start,term_end\n"
        "M400,Bad Mobile,BC400,123,2025-01-01,2025-12-31\n"
    );
    QString path = writeTempFile(content, QStringLiteral(".csv"));

    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService svc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);

    auto job = svc.createJob(JobType::RosterImport, path, 5, s_userId);
    auto exec = svc.executeJob(job.value().id, QStringLiteral("worker-1"));
    // 100% error rate → validation fails
    QVERIFY(exec.isErr());
}

void TstIngestionService::test_rosterImport_invalidDateRange()
{
    QString content = QStringLiteral(
        "member_id,name,barcode,mobile,term_start,term_end\n"
        "M500,Bad Dates,BC500,5554445555,2025-12-31,2025-01-01\n"
    );
    QString path = writeTempFile(content, QStringLiteral(".csv"));

    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService svc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);

    auto job = svc.createJob(JobType::RosterImport, path, 5, s_userId);
    auto exec = svc.executeJob(job.value().id, QStringLiteral("worker-1"));
    QVERIFY(exec.isErr()); // end < start → 100% error rate
}

// ── Job lifecycle tests ─────────────────────────────────────────────────────────

void TstIngestionService::test_createJob_success()
{
    QString content = QStringLiteral("{\"body_text\":\"test\",\"answer_options\":[\"A\",\"B\"],\"correct_answer_index\":0,\"difficulty\":3,\"discrimination\":0.5}\n");
    QString path = writeTempFile(content, QStringLiteral(".jsonl"));

    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService svc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);

    auto result = svc.createJob(JobType::QuestionImport, path, 7, s_userId);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QCOMPARE(result.value().status, JobStatus::Pending);
    QCOMPARE(result.value().priority, 7);
}

void TstIngestionService::test_createJob_invalidPriority()
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
    IngestionService svc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);

    auto result = svc.createJob(JobType::QuestionImport, QStringLiteral("/nonexistent"), 15, s_userId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstIngestionService::test_cancelJob_pending()
{
    QString content = QStringLiteral("{\"body_text\":\"test\",\"answer_options\":[\"A\",\"B\"],\"correct_answer_index\":0,\"difficulty\":3,\"discrimination\":0.5}\n");
    QString path = writeTempFile(content, QStringLiteral(".jsonl"));

    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService svc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);

    auto job = svc.createJob(JobType::QuestionImport, path, 5, s_userId);
    QVERIFY(job.isOk());

    auto result = svc.cancelJob(job.value().id, s_userId);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
}

void TstIngestionService::test_cancelJob_notPending()
{
    QString content = QStringLiteral("{\"body_text\":\"test\",\"answer_options\":[\"A\",\"B\"],\"correct_answer_index\":0,\"difficulty\":3,\"discrimination\":0.5}\n");
    QString path = writeTempFile(content, QStringLiteral(".jsonl"));

    IngestionRepository ingRepo(m_db);
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    MemberRepository memberRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(s_masterKey);
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    IngestionService svc(ingRepo, qRepo, kpRepo, memberRepo, auditSvc, authSvc, cipher);

    auto job = svc.createJob(JobType::QuestionImport, path, 5, s_userId);
    QVERIFY(job.isOk());

    // Manually set status to Claimed
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE ingestion_jobs SET status = 'Claimed' WHERE id = ?"));
    q.addBindValue(job.value().id);
    q.exec();

    auto result = svc.cancelJob(job.value().id, s_userId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::InvalidState);
}

QTEST_MAIN(TstIngestionService)
#include "tst_ingestion_service.moc"
