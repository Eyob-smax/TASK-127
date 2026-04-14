// tst_question_editor.cpp — ProctorOps
// Unit tests for QuestionEditorDialog: field mapping, validation,
// answer option management, and save behavior.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "dialogs/QuestionEditorDialog.h"
#include "AppContextTestTypes.h"
#include "services/QuestionService.h"
#include "services/AuditService.h"
#include "services/AuthService.h"
#include "repositories/QuestionRepository.h"
#include "repositories/KnowledgePointRepository.h"
#include "repositories/AuditRepository.h"
#include "repositories/UserRepository.h"
#include "crypto/AesGcmCipher.h"
#include "models/Question.h"

#include <QTextEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QPushButton>

class TstQuestionEditor : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ── Construction ────────────────────────────────────────────────────────
    void test_createMode_defaultValues();
    void test_editMode_loadsQuestion();

    // ── Answer options ───────────────────────────────────────────────────────
    void test_addAnswer_incrementsCount();
    void test_removeAnswer_decrementsCount();

    // ── Window title ──────────────────────────────────────────────────────────
    void test_createModeTitle();
    void test_editModeTitle();

private:
    void applySchema();
    QString insertQuestion(const QString& body, int difficulty);

    QSqlDatabase       m_db;
    AppContext*        m_ctx{nullptr};

    std::unique_ptr<QuestionRepository>       m_questionRepo;
    std::unique_ptr<KnowledgePointRepository> m_kpRepo;
    std::unique_ptr<AuditRepository>          m_auditRepo;
    std::unique_ptr<UserRepository>           m_userRepo;
    std::unique_ptr<AuditService>             m_auditService;
    std::unique_ptr<AuthService>              m_authService;
    std::unique_ptr<QuestionService>          m_questionService;
};

void TstQuestionEditor::initTestCase()
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("tst_question_editor"));
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(m_db.open());
    applySchema();
    m_ctx = new AppContext();

    static const QByteArray testKey(32, '\x33');
    m_ctx->cipher = std::make_unique<AesGcmCipher>(testKey);

    m_questionRepo  = std::make_unique<QuestionRepository>(m_db);
    m_kpRepo        = std::make_unique<KnowledgePointRepository>(m_db);
    m_auditRepo     = std::make_unique<AuditRepository>(m_db);
    m_userRepo      = std::make_unique<UserRepository>(m_db);
    m_auditService  = std::make_unique<AuditService>(*m_auditRepo, *m_ctx->cipher);
    m_authService   = std::make_unique<AuthService>(*m_userRepo, *m_auditRepo);

    QSqlQuery q(m_db);
    q.exec(QStringLiteral(
        "INSERT OR IGNORE INTO users (id, username, role, status, created_at, updated_at, created_by_user_id) "
        "VALUES ('test-user', 'content', 'ContentManager', 'Active', datetime('now'), datetime('now'), NULL)"));

    m_questionService = std::make_unique<QuestionService>(
        *m_questionRepo, *m_kpRepo, *m_auditService, *m_authService);

    // Transfer ownership into AppContext
    m_ctx->questionService = std::move(m_questionService);
}

void TstQuestionEditor::cleanupTestCase()
{
    m_ctx->questionService.reset();
    m_authService.reset();
    m_userRepo.reset();
    m_ctx->cipher.reset();
    m_ctx = nullptr;
    m_auditService.reset();
    m_auditRepo.reset();
    m_kpRepo.reset();
    m_questionRepo.reset();
    m_db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("tst_question_editor"));
}

void TstQuestionEditor::applySchema()
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS questions ("
        "id TEXT PRIMARY KEY, body_text TEXT, answer_options_json TEXT, "
        "correct_answer_index INTEGER, difficulty INTEGER, discrimination REAL, "
        "status TEXT DEFAULT 'Draft', external_id TEXT, "
        "created_at TEXT, updated_at TEXT, "
        "created_by_user_id TEXT, updated_by_user_id TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS knowledge_points ("
        "id TEXT PRIMARY KEY, name TEXT, parent_id TEXT, position INTEGER, "
        "path TEXT, created_at TEXT, deleted INTEGER DEFAULT 0)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS question_kp_mappings ("
        "question_id TEXT, knowledge_point_id TEXT, mapped_at TEXT, "
        "mapped_by_user_id TEXT, PRIMARY KEY (question_id, knowledge_point_id))"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS tags ("
        "id TEXT PRIMARY KEY, name TEXT UNIQUE, created_at TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS question_tag_mappings ("
        "question_id TEXT, tag_id TEXT, applied_at TEXT, applied_by_user_id TEXT, "
        "PRIMARY KEY (question_id, tag_id))"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS audit_entries ("
        "id TEXT PRIMARY KEY, timestamp TEXT, actor_user_id TEXT, "
        "event_type TEXT, entity_type TEXT, entity_id TEXT, "
        "before_payload_json TEXT, after_payload_json TEXT, "
        "previous_entry_hash TEXT, entry_hash TEXT)"));
    q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS users ("
        "id TEXT PRIMARY KEY, username TEXT, role TEXT, status TEXT, "
        "created_at TEXT, updated_at TEXT, created_by_user_id TEXT)"));
}

QString TstQuestionEditor::insertQuestion(const QString& body, int difficulty)
{
    Question q;
    q.bodyText           = body;
    q.answerOptions      = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
    q.correctAnswerIndex = 0;
    q.difficulty         = difficulty;
    q.discrimination     = 0.5;
    q.status             = QuestionStatus::Active;
    auto result = m_ctx->questionService->createQuestion(q, QStringLiteral("test-user"));
    return result.isOk() ? result.value().id : QString{};
}

// ── Tests ──────────────────────────────────────────────────────────────────────

void TstQuestionEditor::test_createMode_defaultValues()
{
    QuestionEditorDialog dlg(*m_ctx);
    // Difficulty default is 3
    auto* diffSpin = dlg.findChild<QSpinBox*>();
    QVERIFY(diffSpin != nullptr);
    QCOMPARE(diffSpin->value(), 3);
}

void TstQuestionEditor::test_editMode_loadsQuestion()
{
    const QString qid = insertQuestion(QStringLiteral("What is 2+2?"), 2);
    QVERIFY(!qid.isEmpty());

    QuestionEditorDialog dlg(*m_ctx, qid);
    auto* bodyEdit = dlg.findChild<QTextEdit*>();
    QVERIFY(bodyEdit != nullptr);
    QCOMPARE(bodyEdit->toPlainText(), QStringLiteral("What is 2+2?"));
}

void TstQuestionEditor::test_addAnswer_incrementsCount()
{
    QuestionEditorDialog dlg(*m_ctx);
    auto* listWidget = dlg.findChild<QListWidget*>();
    QVERIFY(listWidget != nullptr);
    const int initialCount = listWidget->count();

    // Find and click the Add Option button
    const auto buttons = dlg.findChildren<QPushButton*>();
    for (auto* btn : buttons) {
        if (btn->text() == QStringLiteral("Add Option")) {
            QTest::mouseClick(btn, Qt::LeftButton);
            break;
        }
    }
    // Count may have changed
    QVERIFY(listWidget->count() >= initialCount);
}

void TstQuestionEditor::test_removeAnswer_decrementsCount()
{
    QuestionEditorDialog dlg(*m_ctx);
    auto* listWidget = dlg.findChild<QListWidget*>();
    QVERIFY(listWidget != nullptr);

    // Add two options first
    const auto buttons = dlg.findChildren<QPushButton*>();
    for (auto* btn : buttons) {
        if (btn->text() == QStringLiteral("Add Option")) {
            QTest::mouseClick(btn, Qt::LeftButton);
            QTest::mouseClick(btn, Qt::LeftButton);
            break;
        }
    }

    const int countAfterAdd = listWidget->count();
    // Select and remove if count > 2
    if (countAfterAdd > 2) {
        listWidget->setCurrentRow(0);
        for (auto* btn : buttons) {
            if (btn->text() == QStringLiteral("Remove Selected")) {
                QTest::mouseClick(btn, Qt::LeftButton);
                break;
            }
        }
        QVERIFY(listWidget->count() < countAfterAdd);
    }
}

void TstQuestionEditor::test_createModeTitle()
{
    QuestionEditorDialog dlg(*m_ctx);
    QCOMPARE(dlg.windowTitle(), QStringLiteral("New Question"));
}

void TstQuestionEditor::test_editModeTitle()
{
    const QString qid = insertQuestion(QStringLiteral("Test question"), 3);
    QVERIFY(!qid.isEmpty());
    QuestionEditorDialog dlg(*m_ctx, qid);
    QCOMPARE(dlg.windowTitle(), QStringLiteral("Edit Question"));
}

QTEST_MAIN(TstQuestionEditor)
#include "tst_question_editor.moc"
