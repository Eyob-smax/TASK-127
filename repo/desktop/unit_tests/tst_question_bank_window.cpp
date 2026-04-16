// tst_question_bank_window.cpp — ProctorOps
// Unit tests for QuestionBankWindow structure and data loading.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTableView>
#include <QGroupBox>
#include <QLineEdit>

#include "windows/QuestionBankWindow.h"
#include "AppContextTestTypes.h"
#include "services/QuestionService.h"
#include "services/AuditService.h"
#include "services/AuthService.h"
#include "repositories/QuestionRepository.h"
#include "repositories/KnowledgePointRepository.h"
#include "repositories/AuditRepository.h"
#include "repositories/UserRepository.h"
#include "crypto/AesGcmCipher.h"

class TstQuestionBankWindow : public QObject {
    Q_OBJECT

private:
    QSqlDatabase m_db;
    AppContext* m_ctx{nullptr};

    std::unique_ptr<QuestionRepository> m_questionRepo;
    std::unique_ptr<KnowledgePointRepository> m_kpRepo;
    std::unique_ptr<AuditRepository> m_auditRepo;
    std::unique_ptr<UserRepository> m_userRepo;
    std::unique_ptr<AuditService> m_auditService;
    std::unique_ptr<AuthService> m_authService;

private slots:
    void initTestCase();
    void cleanupTestCase();

    void test_windowMetadata();
    void test_filterPanelExists();
    void test_initialLoadPopulatesTable();
    void test_activateFilter_setsFilterVisibleAndFocused();

private:
    void applySchema();
    void seedQuestions();
};

void TstQuestionBankWindow::initTestCase()
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("tst_question_bank_window"));
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(m_db.open());

    applySchema();

    m_ctx = new AppContext();
    m_ctx->session.userId = QStringLiteral("u-content");
    m_ctx->authenticated = true;

    static const QByteArray testKey(32, '\x44');
    m_ctx->cipher = std::make_unique<AesGcmCipher>(testKey);

    m_questionRepo = std::make_unique<QuestionRepository>(m_db);
    m_kpRepo = std::make_unique<KnowledgePointRepository>(m_db);
    m_auditRepo = std::make_unique<AuditRepository>(m_db);
    m_userRepo = std::make_unique<UserRepository>(m_db);
    m_auditService = std::make_unique<AuditService>(*m_auditRepo, *m_ctx->cipher);
    m_authService = std::make_unique<AuthService>(*m_userRepo, *m_auditRepo);

    m_ctx->questionService = std::make_unique<QuestionService>(
        *m_questionRepo, *m_kpRepo, *m_auditService, *m_authService);

    seedQuestions();
}

void TstQuestionBankWindow::cleanupTestCase()
{
    m_ctx->questionService.reset();
    m_authService.reset();
    m_auditService.reset();
    m_userRepo.reset();
    m_auditRepo.reset();
    m_kpRepo.reset();
    m_questionRepo.reset();
    m_ctx->cipher.reset();
    delete m_ctx;
    m_ctx = nullptr;

    m_db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("tst_question_bank_window"));
}

void TstQuestionBankWindow::applySchema()
{
    QSqlQuery q(m_db);
    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE users ("
        "id TEXT PRIMARY KEY, username TEXT NOT NULL UNIQUE, role TEXT NOT NULL,"
        "status TEXT NOT NULL DEFAULT 'Active', created_at TEXT NOT NULL,"
        "updated_at TEXT NOT NULL, created_by_user_id TEXT)")));
    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE knowledge_points ("
        "id TEXT PRIMARY KEY, name TEXT NOT NULL, parent_id TEXT, position INTEGER NOT NULL DEFAULT 0,"
        "path TEXT NOT NULL, created_at TEXT NOT NULL, deleted INTEGER NOT NULL DEFAULT 0)")));
    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE tags (id TEXT PRIMARY KEY, name TEXT NOT NULL UNIQUE, created_at TEXT NOT NULL)")));
    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE questions ("
        "id TEXT PRIMARY KEY, body_text TEXT NOT NULL, answer_options_json TEXT NOT NULL,"
        "correct_answer_index INTEGER NOT NULL, difficulty INTEGER NOT NULL, discrimination REAL NOT NULL,"
        "status TEXT NOT NULL, external_id TEXT, created_at TEXT NOT NULL, updated_at TEXT NOT NULL,"
        "created_by_user_id TEXT NOT NULL, updated_by_user_id TEXT NOT NULL)")));
    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE question_kp_mappings ("
        "question_id TEXT NOT NULL, knowledge_point_id TEXT NOT NULL, mapped_at TEXT NOT NULL,"
        "mapped_by_user_id TEXT NOT NULL, PRIMARY KEY (question_id, knowledge_point_id))")));
    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE question_tag_mappings ("
        "question_id TEXT NOT NULL, tag_id TEXT NOT NULL, applied_at TEXT NOT NULL,"
        "applied_by_user_id TEXT NOT NULL, PRIMARY KEY (question_id, tag_id))")));
    QVERIFY(q.exec(QStringLiteral(
        "CREATE TABLE audit_entries ("
        "id TEXT PRIMARY KEY, timestamp TEXT NOT NULL, actor_user_id TEXT NOT NULL, event_type TEXT NOT NULL,"
        "entity_type TEXT NOT NULL, entity_id TEXT NOT NULL, before_payload_json TEXT NOT NULL DEFAULT '{}',"
        "after_payload_json TEXT NOT NULL DEFAULT '{}', previous_entry_hash TEXT NOT NULL, entry_hash TEXT NOT NULL)")));

    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO users (id, username, role, status, created_at, updated_at, created_by_user_id)"
        "VALUES ('u-content', 'content', 'ContentManager', 'Active', datetime('now'), datetime('now'), NULL)")));
}

void TstQuestionBankWindow::seedQuestions()
{
    Question q1;
    q1.bodyText = QStringLiteral("Question A body");
    q1.answerOptions = {QStringLiteral("A"), QStringLiteral("B")};
    q1.correctAnswerIndex = 0;
    q1.difficulty = 2;
    q1.discrimination = 0.50;
    q1.status = QuestionStatus::Active;

    Question q2;
    q2.bodyText = QStringLiteral("Question B body");
    q2.answerOptions = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
    q2.correctAnswerIndex = 1;
    q2.difficulty = 4;
    q2.discrimination = 0.70;
    q2.status = QuestionStatus::Active;

    auto r1 = m_ctx->questionService->createQuestion(q1, QStringLiteral("u-content"));
    auto r2 = m_ctx->questionService->createQuestion(q2, QStringLiteral("u-content"));
    QVERIFY(r1.isOk());
    QVERIFY(r2.isOk());

    // createQuestion persists questions in Draft state; the window defaults to
    // filtering Active status, so promote fixtures to Active for initial-load checks.
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE questions SET status = 'Active' WHERE id = ?"));
    q.addBindValue(r1.value().id);
    QVERIFY(q.exec());

    q.prepare(QStringLiteral("UPDATE questions SET status = 'Active' WHERE id = ?"));
    q.addBindValue(r2.value().id);
    QVERIFY(q.exec());
}

void TstQuestionBankWindow::test_windowMetadata()
{
    QuestionBankWindow win(*m_ctx);
    QCOMPARE(win.objectName(), QStringLiteral("window.question_bank"));
    QCOMPARE(win.windowTitle(), QStringLiteral("Question Bank"));
}

void TstQuestionBankWindow::test_filterPanelExists()
{
    QuestionBankWindow win(*m_ctx);
    auto boxes = win.findChildren<QGroupBox*>();
    bool foundFilter = false;
    for (auto* box : boxes) {
        if (box->title() == QStringLiteral("Filter")) {
            foundFilter = true;
            break;
        }
    }
    QVERIFY(foundFilter);
}

void TstQuestionBankWindow::test_initialLoadPopulatesTable()
{
    QuestionBankWindow win(*m_ctx);
    win.show();
    QTest::qWait(50);

    auto* table = win.findChild<QTableView*>();
    QVERIFY(table != nullptr);
    QVERIFY(table->model() != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount() >= 2, 2000);
}

void TstQuestionBankWindow::test_activateFilter_setsFilterVisibleAndFocused()
{
    QuestionBankWindow win(*m_ctx);
    win.show();
    QTest::qWait(50);

    QGroupBox* filterBox = nullptr;
    const auto boxes = win.findChildren<QGroupBox*>();
    for (QGroupBox* box : boxes) {
        if (box->title() == QStringLiteral("Filter")) {
            filterBox = box;
            break;
        }
    }
    QVERIFY(filterBox != nullptr);
    filterBox->setVisible(false);

    win.activateFilter();
    QCoreApplication::processEvents();

    QVERIFY(!filterBox->isHidden());
    auto* line = win.findChild<QLineEdit*>();
    QVERIFY(line != nullptr);
}

QTEST_MAIN(TstQuestionBankWindow)
#include "tst_question_bank_window.moc"
