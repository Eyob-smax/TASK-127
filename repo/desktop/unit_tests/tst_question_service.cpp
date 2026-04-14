// tst_question_service.cpp — ProctorOps
// Unit tests for QuestionService: question CRUD, validation, KP tree,
// mappings, tags, and combined query builder.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#include "services/QuestionService.h"
#include "repositories/QuestionRepository.h"
#include "repositories/KnowledgePointRepository.h"
#include "repositories/AuditRepository.h"
#include "repositories/UserRepository.h"
#include "crypto/AesGcmCipher.h"
#include "crypto/HashChain.h"
#include "utils/Validation.h"
#include "services/AuthService.h"
#include "models/CommonTypes.h"

class TstQuestionService : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ── Question CRUD ───────────────────────────────────────────────────
    void test_createQuestion_success();
    void test_createQuestion_invalidDifficulty();
    void test_createQuestion_invalidDiscrimination();
    void test_createQuestion_emptyBody();
    void test_createQuestion_bodyTooLong();
    void test_createQuestion_tooFewOptions();
    void test_createQuestion_tooManyOptions();
    void test_createQuestion_optionTooLong();
    void test_createQuestion_indexOutOfRange();
    void test_updateQuestion_success();
    void test_deleteQuestion_softDelete();

    // ── Query builder ───────────────────────────────────────────────────
    void test_queryQuestions_difficultyRange();
    void test_queryQuestions_kpSubtree();
    void test_queryQuestions_tagFilter();
    void test_queryQuestions_textSearch();
    void test_queryQuestions_pagination();
    void test_queryQuestions_combinedTagDiscrimination();

    // ── External ID ─────────────────────────────────────────────────────
    void test_externalIdExists_duplicate();

    // ── KP tree ─────────────────────────────────────────────────────────
    void test_createKP_rootNode();
    void test_createKP_childNode();
    void test_updateKP_pathPropagation();
    void test_deleteKP_softDelete();

    // ── Mappings ────────────────────────────────────────────────────────
    void test_mapQuestionToKP();
    void test_unmapQuestionFromKP();

    // ── Tags ────────────────────────────────────────────────────────────
    void test_createTag_success();
    void test_createTag_duplicate();
    void test_applyTag_success();
    void test_removeTag_success();

private:
    void applySchema();
    void createTestUser();
    Question makeValidQuestion();

    QSqlDatabase m_db;
    int m_dbIndex = 0;
    static const QString s_userId;
};

const QString TstQuestionService::s_userId = QStringLiteral("user-001");

void TstQuestionService::initTestCase() {}
void TstQuestionService::cleanupTestCase() {}

void TstQuestionService::init()
{
    QString connName = QStringLiteral("tst_qs_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON;"));
    applySchema();
    createTestUser();
}

void TstQuestionService::cleanup()
{
    QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstQuestionService::applySchema()
{
    QSqlQuery q(m_db);

    // Users table (minimal from 0002)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE users ("
        "  id TEXT PRIMARY KEY,"
        "  username TEXT NOT NULL UNIQUE,"
        "  role TEXT NOT NULL,"
        "  status TEXT NOT NULL DEFAULT 'Active',"
        "  created_at TEXT NOT NULL,"
        "  updated_at TEXT NOT NULL,"
        "  created_by_user_id TEXT"
        ");"
    )), qPrintable(q.lastError().text()));

    // Knowledge points (0005)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE knowledge_points ("
        "  id TEXT PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  parent_id TEXT REFERENCES knowledge_points(id) ON DELETE RESTRICT,"
        "  position INTEGER NOT NULL DEFAULT 0,"
        "  path TEXT NOT NULL,"
        "  created_at TEXT NOT NULL,"
        "  deleted INTEGER NOT NULL DEFAULT 0"
        ");"
    )), qPrintable(q.lastError().text()));

    // Tags
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE tags ("
        "  id TEXT PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  created_at TEXT NOT NULL,"
        "  CONSTRAINT uq_tag_name UNIQUE (name)"
        ");"
    )), qPrintable(q.lastError().text()));

    // Questions
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE questions ("
        "  id TEXT PRIMARY KEY,"
        "  body_text TEXT NOT NULL,"
        "  answer_options_json TEXT NOT NULL,"
        "  correct_answer_index INTEGER NOT NULL CHECK (correct_answer_index >= 0),"
        "  difficulty INTEGER NOT NULL CHECK (difficulty >= 1 AND difficulty <= 5),"
        "  discrimination REAL NOT NULL CHECK (discrimination >= 0.00 AND discrimination <= 1.00),"
        "  status TEXT NOT NULL DEFAULT 'Active',"
        "  external_id TEXT,"
        "  created_at TEXT NOT NULL,"
        "  updated_at TEXT NOT NULL,"
        "  created_by_user_id TEXT NOT NULL REFERENCES users(id),"
        "  updated_by_user_id TEXT NOT NULL REFERENCES users(id)"
        ");"
    )), qPrintable(q.lastError().text()));

    // Question-KP mappings
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE question_kp_mappings ("
        "  question_id TEXT NOT NULL REFERENCES questions(id) ON DELETE CASCADE,"
        "  knowledge_point_id TEXT NOT NULL REFERENCES knowledge_points(id) ON DELETE RESTRICT,"
        "  mapped_at TEXT NOT NULL,"
        "  mapped_by_user_id TEXT NOT NULL REFERENCES users(id),"
        "  PRIMARY KEY (question_id, knowledge_point_id)"
        ");"
    )), qPrintable(q.lastError().text()));

    // Question-Tag mappings
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE question_tag_mappings ("
        "  question_id TEXT NOT NULL REFERENCES questions(id) ON DELETE CASCADE,"
        "  tag_id TEXT NOT NULL REFERENCES tags(id) ON DELETE RESTRICT,"
        "  applied_at TEXT NOT NULL,"
        "  applied_by_user_id TEXT NOT NULL REFERENCES users(id),"
        "  PRIMARY KEY (question_id, tag_id)"
        ");"
    )), qPrintable(q.lastError().text()));

    // Audit entries (0008)
    QVERIFY2(q.exec(QStringLiteral(
        "CREATE TABLE audit_entries ("
        "  id TEXT PRIMARY KEY,"
        "  timestamp TEXT NOT NULL,"
        "  actor_user_id TEXT NOT NULL,"
        "  event_type TEXT NOT NULL,"
        "  entity_type TEXT NOT NULL,"
        "  entity_id TEXT NOT NULL,"
        "  before_payload_json TEXT NOT NULL DEFAULT '{}',"
        "  after_payload_json TEXT NOT NULL DEFAULT '{}',"
        "  previous_entry_hash TEXT NOT NULL,"
        "  entry_hash TEXT NOT NULL"
        ");"
    )), qPrintable(q.lastError().text()));
}

void TstQuestionService::createTestUser()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO users (id, username, role, status, created_at, updated_at)"
                              " VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(s_userId);
    q.addBindValue(QStringLiteral("testadmin"));
    q.addBindValue(QStringLiteral("SecurityAdministrator"));
    q.addBindValue(QStringLiteral("Active"));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
}

Question TstQuestionService::makeValidQuestion()
{
    Question q;
    q.bodyText = QStringLiteral("What is the primary purpose of grounding?");
    q.answerOptions = {QStringLiteral("Safety"), QStringLiteral("Aesthetics"),
                       QStringLiteral("Cost reduction"), QStringLiteral("Speed")};
    q.correctAnswerIndex = 0;
    q.difficulty = 3;
    q.discrimination = 0.75;
    return q;
}

// ── Question CRUD tests ─────────────────────────────────────────────────────────

void TstQuestionService::test_createQuestion_success()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto result = svc.createQuestion(makeValidQuestion(), s_userId);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");

    const auto& created = result.value();
    QVERIFY(!created.id.isEmpty());
    QCOMPARE(created.status, QuestionStatus::Draft);
    QCOMPARE(created.difficulty, 3);
    QCOMPARE(created.discrimination, 0.75);
    QCOMPARE(created.createdByUserId, s_userId);
}

void TstQuestionService::test_createQuestion_invalidDifficulty()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto q = makeValidQuestion();
    q.difficulty = 0;
    auto result = svc.createQuestion(q, s_userId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstQuestionService::test_createQuestion_invalidDiscrimination()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto q = makeValidQuestion();
    q.discrimination = 1.5;
    auto result = svc.createQuestion(q, s_userId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstQuestionService::test_createQuestion_emptyBody()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto q = makeValidQuestion();
    q.bodyText.clear();
    auto result = svc.createQuestion(q, s_userId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstQuestionService::test_createQuestion_bodyTooLong()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto q = makeValidQuestion();
    q.bodyText = QString(Validation::QuestionBodyMaxChars + 1, QLatin1Char('x'));
    auto result = svc.createQuestion(q, s_userId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstQuestionService::test_createQuestion_tooFewOptions()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto q = makeValidQuestion();
    q.answerOptions = {QStringLiteral("Only one")};
    q.correctAnswerIndex = 0;
    auto result = svc.createQuestion(q, s_userId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstQuestionService::test_createQuestion_tooManyOptions()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto q = makeValidQuestion();
    q.answerOptions = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
                       QStringLiteral("D"), QStringLiteral("E"), QStringLiteral("F"),
                       QStringLiteral("G")};
    auto result = svc.createQuestion(q, s_userId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstQuestionService::test_createQuestion_optionTooLong()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto q = makeValidQuestion();
    q.answerOptions[0] = QString(Validation::AnswerOptionMaxChars + 1, QLatin1Char('x'));
    auto result = svc.createQuestion(q, s_userId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstQuestionService::test_createQuestion_indexOutOfRange()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto q = makeValidQuestion();
    q.correctAnswerIndex = 99;
    auto result = svc.createQuestion(q, s_userId);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstQuestionService::test_updateQuestion_success()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto created = svc.createQuestion(makeValidQuestion(), s_userId);
    QVERIFY(created.isOk());

    auto q = created.value();
    q.difficulty = 5;
    q.discrimination = 0.90;
    auto updated = svc.updateQuestion(q, s_userId);
    QVERIFY2(updated.isOk(), updated.isErr() ? qPrintable(updated.errorMessage()) : "");
    QCOMPARE(updated.value().difficulty, 5);
    QCOMPARE(updated.value().discrimination, 0.90);
}

void TstQuestionService::test_deleteQuestion_softDelete()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto created = svc.createQuestion(makeValidQuestion(), s_userId);
    QVERIFY(created.isOk());

    auto result = svc.deleteQuestion(created.value().id, s_userId);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
}

// ── Query builder tests ─────────────────────────────────────────────────────────

void TstQuestionService::test_queryQuestions_difficultyRange()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    // Create questions with different difficulties
    for (int d = 1; d <= 5; ++d) {
        auto q = makeValidQuestion();
        q.difficulty = d;
        q.status = QuestionStatus::Active;
        auto r = svc.createQuestion(q, s_userId);
        QVERIFY(r.isOk());
        // Manually set to Active for query
        QSqlQuery sq(m_db);
        sq.prepare(QStringLiteral("UPDATE questions SET status = 'Active' WHERE id = ?"));
        sq.addBindValue(r.value().id);
        sq.exec();
    }

    QuestionFilter filter;
    filter.difficultyMin = 3;
    filter.difficultyMax = 5;
    filter.statusFilter = QuestionStatus::Active;
    auto result = svc.queryQuestions(filter);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QCOMPARE(result.value().size(), 3);
}

void TstQuestionService::test_queryQuestions_kpSubtree()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    // Create KP tree: Safety → Electrical
    KnowledgePoint root;
    root.name = QStringLiteral("Safety");
    root.position = 0;
    auto rootResult = svc.createKnowledgePoint(root, s_userId);
    QVERIFY(rootResult.isOk());

    KnowledgePoint child;
    child.name = QStringLiteral("Electrical");
    child.parentId = rootResult.value().id;
    child.position = 0;
    auto childResult = svc.createKnowledgePoint(child, s_userId);
    QVERIFY(childResult.isOk());

    // Create question and map to child KP
    auto q = makeValidQuestion();
    auto qResult = svc.createQuestion(q, s_userId);
    QVERIFY(qResult.isOk());

    QSqlQuery sq(m_db);
    sq.prepare(QStringLiteral("UPDATE questions SET status = 'Active' WHERE id = ?"));
    sq.addBindValue(qResult.value().id);
    sq.exec();

    auto mapResult = svc.mapQuestionToKP(qResult.value().id, childResult.value().id, s_userId);
    QVERIFY2(mapResult.isOk(), mapResult.isErr() ? qPrintable(mapResult.errorMessage()) : "");

    // Query by root KP should find the question via subtree
    QuestionFilter filter;
    filter.knowledgePointId = rootResult.value().id;
    filter.statusFilter = QuestionStatus::Active;
    auto result = svc.queryQuestions(filter);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QVERIFY(result.value().size() >= 1);
}

void TstQuestionService::test_queryQuestions_tagFilter()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto tagResult = svc.createTag(QStringLiteral("Safety"), s_userId);
    QVERIFY(tagResult.isOk());

    auto qResult = svc.createQuestion(makeValidQuestion(), s_userId);
    QVERIFY(qResult.isOk());

    QSqlQuery sq(m_db);
    sq.prepare(QStringLiteral("UPDATE questions SET status = 'Active' WHERE id = ?"));
    sq.addBindValue(qResult.value().id);
    sq.exec();

    auto applyResult = svc.applyTag(qResult.value().id, tagResult.value().id, s_userId);
    QVERIFY(applyResult.isOk());

    QuestionFilter filter;
    filter.tagIds = {tagResult.value().id};
    filter.statusFilter = QuestionStatus::Active;
    auto result = svc.queryQuestions(filter);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QCOMPARE(result.value().size(), 1);
}

void TstQuestionService::test_queryQuestions_textSearch()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto q = makeValidQuestion();
    q.bodyText = QStringLiteral("What is the resistance of copper wire?");
    auto created = svc.createQuestion(q, s_userId);
    QVERIFY(created.isOk());

    QSqlQuery sq(m_db);
    sq.prepare(QStringLiteral("UPDATE questions SET status = 'Active' WHERE id = ?"));
    sq.addBindValue(created.value().id);
    sq.exec();

    QuestionFilter filter;
    filter.textSearch = QStringLiteral("copper");
    filter.statusFilter = QuestionStatus::Active;
    auto result = svc.queryQuestions(filter);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QCOMPARE(result.value().size(), 1);
}

void TstQuestionService::test_queryQuestions_pagination()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    for (int i = 0; i < 5; ++i) {
        auto q = makeValidQuestion();
        auto r = svc.createQuestion(q, s_userId);
        QVERIFY(r.isOk());
        QSqlQuery sq(m_db);
        sq.prepare(QStringLiteral("UPDATE questions SET status = 'Active' WHERE id = ?"));
        sq.addBindValue(r.value().id);
        sq.exec();
    }

    QuestionFilter filter;
    filter.statusFilter = QuestionStatus::Active;
    filter.limit = 2;
    filter.offset = 0;
    auto page1 = svc.queryQuestions(filter);
    QVERIFY(page1.isOk());
    QCOMPARE(page1.value().size(), 2);

    filter.offset = 2;
    auto page2 = svc.queryQuestions(filter);
    QVERIFY(page2.isOk());
    QCOMPARE(page2.value().size(), 2);
}

// ── External ID ─────────────────────────────────────────────────────────────────

void TstQuestionService::test_externalIdExists_duplicate()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto q = makeValidQuestion();
    q.externalId = QStringLiteral("EXT-001");
    auto created = svc.createQuestion(q, s_userId);
    QVERIFY(created.isOk());

    auto exists = svc.externalIdExists(QStringLiteral("EXT-001"));
    QVERIFY(exists.isOk());
    QVERIFY(exists.value());

    auto notExists = svc.externalIdExists(QStringLiteral("EXT-999"));
    QVERIFY(notExists.isOk());
    QVERIFY(!notExists.value());
}

// ── KP tree tests ───────────────────────────────────────────────────────────────

void TstQuestionService::test_createKP_rootNode()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    KnowledgePoint kp;
    kp.name = QStringLiteral("Safety");
    kp.position = 0;
    auto result = svc.createKnowledgePoint(kp, s_userId);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QCOMPARE(result.value().path, QStringLiteral("Safety"));
    QVERIFY(result.value().parentId.isEmpty());
}

void TstQuestionService::test_createKP_childNode()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    KnowledgePoint root;
    root.name = QStringLiteral("Safety");
    root.position = 0;
    auto rootResult = svc.createKnowledgePoint(root, s_userId);
    QVERIFY(rootResult.isOk());

    KnowledgePoint child;
    child.name = QStringLiteral("Electrical");
    child.parentId = rootResult.value().id;
    child.position = 0;
    auto childResult = svc.createKnowledgePoint(child, s_userId);
    QVERIFY2(childResult.isOk(), childResult.isErr() ? qPrintable(childResult.errorMessage()) : "");
    QCOMPARE(childResult.value().path, QStringLiteral("Safety/Electrical"));
}

void TstQuestionService::test_updateKP_pathPropagation()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    // Create Safety → Electrical
    KnowledgePoint root;
    root.name = QStringLiteral("Safety");
    root.position = 0;
    auto rootResult = svc.createKnowledgePoint(root, s_userId);
    QVERIFY(rootResult.isOk());

    KnowledgePoint child;
    child.name = QStringLiteral("Electrical");
    child.parentId = rootResult.value().id;
    child.position = 0;
    auto childResult = svc.createKnowledgePoint(child, s_userId);
    QVERIFY(childResult.isOk());

    // Rename root: Safety → Security
    auto toRename = rootResult.value();
    toRename.name = QStringLiteral("Security");
    auto renamed = svc.updateKnowledgePoint(toRename, s_userId);
    QVERIFY2(renamed.isOk(), renamed.isErr() ? qPrintable(renamed.errorMessage()) : "");
    QCOMPARE(renamed.value().path, QStringLiteral("Security"));

    // Check child path propagated
    auto descendants = svc.getDescendants(rootResult.value().id);
    QVERIFY(descendants.isOk());
    QVERIFY(descendants.value().size() >= 1);
    QCOMPARE(descendants.value().at(0).path, QStringLiteral("Security/Electrical"));
}

void TstQuestionService::test_deleteKP_softDelete()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    KnowledgePoint kp;
    kp.name = QStringLiteral("Temporary");
    kp.position = 0;
    auto created = svc.createKnowledgePoint(kp, s_userId);
    QVERIFY(created.isOk());

    auto result = svc.deleteKnowledgePoint(created.value().id, s_userId);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
}

// ── Mapping tests ───────────────────────────────────────────────────────────────

void TstQuestionService::test_mapQuestionToKP()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    KnowledgePoint kp;
    kp.name = QStringLiteral("Chapter1");
    kp.position = 0;
    auto kpResult = svc.createKnowledgePoint(kp, s_userId);
    QVERIFY(kpResult.isOk());

    auto qResult = svc.createQuestion(makeValidQuestion(), s_userId);
    QVERIFY(qResult.isOk());

    auto mapResult = svc.mapQuestionToKP(qResult.value().id, kpResult.value().id, s_userId);
    QVERIFY2(mapResult.isOk(), mapResult.isErr() ? qPrintable(mapResult.errorMessage()) : "");

    auto mappings = svc.getQuestionKPMappings(qResult.value().id);
    QVERIFY(mappings.isOk());
    QCOMPARE(mappings.value().size(), 1);
}

void TstQuestionService::test_unmapQuestionFromKP()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    KnowledgePoint kp;
    kp.name = QStringLiteral("Chapter2");
    kp.position = 0;
    auto kpResult = svc.createKnowledgePoint(kp, s_userId);
    QVERIFY(kpResult.isOk());

    auto qResult = svc.createQuestion(makeValidQuestion(), s_userId);
    QVERIFY(qResult.isOk());

    auto mapResult = svc.mapQuestionToKP(qResult.value().id, kpResult.value().id, s_userId);
    QVERIFY2(mapResult.isOk(), mapResult.isErr() ? qPrintable(mapResult.errorMessage()) : "");
    auto unmapResult = svc.unmapQuestionFromKP(qResult.value().id, kpResult.value().id, s_userId);
    QVERIFY2(unmapResult.isOk(), unmapResult.isErr() ? qPrintable(unmapResult.errorMessage()) : "");

    auto mappings = svc.getQuestionKPMappings(qResult.value().id);
    QVERIFY(mappings.isOk());
    QCOMPARE(mappings.value().size(), 0);
}

// ── Tag tests ───────────────────────────────────────────────────────────────────

void TstQuestionService::test_createTag_success()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto result = svc.createTag(QStringLiteral("Safety"), s_userId);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QCOMPARE(result.value().name, QStringLiteral("Safety"));
}

void TstQuestionService::test_createTag_duplicate()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto firstCreate = svc.createTag(QStringLiteral("Safety"), s_userId);
    QVERIFY2(firstCreate.isOk(), firstCreate.isErr() ? qPrintable(firstCreate.errorMessage()) : "");
    auto duplicate = svc.createTag(QStringLiteral("Safety"), s_userId);
    QVERIFY(duplicate.isErr());
    QCOMPARE(duplicate.errorCode(), ErrorCode::AlreadyExists);
}

void TstQuestionService::test_applyTag_success()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto tag = svc.createTag(QStringLiteral("Electrical"), s_userId);
    QVERIFY(tag.isOk());

    auto q = svc.createQuestion(makeValidQuestion(), s_userId);
    QVERIFY(q.isOk());

    auto result = svc.applyTag(q.value().id, tag.value().id, s_userId);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");

    auto mappings = svc.getQuestionTagMappings(q.value().id);
    QVERIFY(mappings.isOk());
    QCOMPARE(mappings.value().size(), 1);
}

void TstQuestionService::test_removeTag_success()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    auto tag = svc.createTag(QStringLiteral("Temp"), s_userId);
    QVERIFY(tag.isOk());

    auto q = svc.createQuestion(makeValidQuestion(), s_userId);
    QVERIFY(q.isOk());

    auto applyTagResult = svc.applyTag(q.value().id, tag.value().id, s_userId);
    QVERIFY2(applyTagResult.isOk(), applyTagResult.isErr() ? qPrintable(applyTagResult.errorMessage()) : "");
    auto result = svc.removeTag(q.value().id, tag.value().id, s_userId);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");

    auto mappings = svc.getQuestionTagMappings(q.value().id);
    QVERIFY(mappings.isOk());
    QCOMPARE(mappings.value().size(), 0);
}

// ── Combined tag + discrimination filter ────────────────────────────────────────

void TstQuestionService::test_queryQuestions_combinedTagDiscrimination()
{
    QuestionRepository qRepo(m_db);
    KnowledgePointRepository kpRepo(m_db);
    AuditRepository auditRepo(m_db);
    AesGcmCipher cipher(QByteArray(32, 'k'));
    AuditService auditSvc(auditRepo, cipher);
    UserRepository userRepo(m_db);
    AuthService authSvc(userRepo, auditRepo);
    QuestionService svc(qRepo, kpRepo, auditSvc, authSvc);

    // Create a tag to filter by
    auto tagResult = svc.createTag(QStringLiteral("Safety"), s_userId);
    QVERIFY2(tagResult.isOk(), tagResult.isErr() ? qPrintable(tagResult.errorMessage()) : "");
    const QString tagId = tagResult.value().id;

    // Question A: discrimination=0.30, no tag  → must NOT match
    auto qA = makeValidQuestion();
    qA.discrimination = 0.30;
    auto rA = svc.createQuestion(qA, s_userId);
    QVERIFY(rA.isOk());

    // Question B: discrimination=0.70, tag applied  → must match
    auto qB = makeValidQuestion();
    qB.discrimination = 0.70;
    auto rB = svc.createQuestion(qB, s_userId);
    QVERIFY(rB.isOk());

    // Question C: discrimination=0.80, tag applied  → must match
    auto qC = makeValidQuestion();
    qC.discrimination = 0.80;
    auto rC = svc.createQuestion(qC, s_userId);
    QVERIFY(rC.isOk());

    // Promote all to Active so the default statusFilter finds them
    auto activate = [&](const QString& id) {
        QSqlQuery sq(m_db);
        sq.prepare(QStringLiteral("UPDATE questions SET status = 'Active' WHERE id = ?"));
        sq.addBindValue(id);
        sq.exec();
    };
    activate(rA.value().id);
    activate(rB.value().id);
    activate(rC.value().id);

    // Apply tag only to B and C
    QVERIFY(svc.applyTag(rB.value().id, tagId, s_userId).isOk());
    QVERIFY(svc.applyTag(rC.value().id, tagId, s_userId).isOk());

    // Filter: tag={tagId} AND discriminationMin=0.65
    // Expected: B (0.70) and C (0.80); A excluded (no tag), no exclusions from discrimination
    // since both B and C exceed 0.65.
    QuestionFilter filter;
    filter.statusFilter      = QuestionStatus::Active;
    filter.tagIds            = {tagId};
    filter.discriminationMin = 0.65;
    auto result = svc.queryQuestions(filter);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QCOMPARE(result.value().size(), 2);

    // Tighten discrimination to 0.75 — only C (0.80) should remain
    filter.discriminationMin = 0.75;
    auto result2 = svc.queryQuestions(filter);
    QVERIFY2(result2.isOk(), result2.isErr() ? qPrintable(result2.errorMessage()) : "");
    QCOMPARE(result2.value().size(), 1);
    QCOMPARE(result2.value().first().id, rC.value().id);
}

QTEST_MAIN(TstQuestionService)
#include "tst_question_service.moc"
