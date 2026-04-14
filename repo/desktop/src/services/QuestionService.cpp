// QuestionService.cpp — ProctorOps
// Content governance business logic: question CRUD with validation, audit,
// knowledge-point tree management, mappings, and tags.

#include "QuestionService.h"
#include "services/AuthService.h"
#include "repositories/IQuestionRepository.h"
#include "utils/Validation.h"
#include "utils/Logger.h"

#include <QUuid>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>

QuestionService::QuestionService(IQuestionRepository& questionRepo,
                                   IKnowledgePointRepository& kpRepo,
                                   AuditService& auditService,
                                   AuthService& authService)
    : m_questionRepo(questionRepo)
    , m_kpRepo(kpRepo)
    , m_auditService(auditService)
    , m_authService(authService)
{
}

// ── Validation ───────────────────────────────────────────────────────────────

Result<void> QuestionService::validateQuestion(const Question& q)
{
    if (q.bodyText.isEmpty())
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("Body text is required"));

    if (q.bodyText.length() > Validation::QuestionBodyMaxChars)
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("Body text exceeds maximum length"));

    if (q.answerOptions.size() < Validation::AnswerOptionMinCount)
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("At least 2 answer options required"));

    if (q.answerOptions.size() > Validation::AnswerOptionMaxCount)
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("At most 6 answer options allowed"));

    for (const auto& opt : q.answerOptions) {
        if (opt.length() > Validation::AnswerOptionMaxChars)
            return Result<void>::err(ErrorCode::ValidationFailed,
                                      QStringLiteral("Answer option exceeds maximum length"));
    }

    if (q.correctAnswerIndex < 0 || q.correctAnswerIndex >= q.answerOptions.size())
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("Correct answer index out of range"));

    if (!Validation::isDifficultyValid(q.difficulty))
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("Difficulty must be between 1 and 5"));

    if (!Validation::isDiscriminationValid(q.discrimination))
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("Discrimination must be between 0.00 and 1.00"));

    return Result<void>::ok();
}

// ── Questions ────────────────────────────────────────────────────────────────

Result<Question> QuestionService::createQuestion(const Question& q,
                                                    const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::ContentManager);
    if (authResult.isErr())
        return Result<Question>::err(authResult.errorCode(), authResult.errorMessage());

    auto valid = validateQuestion(q);
    if (valid.isErr())
        return Result<Question>::err(valid.errorCode(), valid.errorMessage());

    Question out = q;
    out.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    out.status = QuestionStatus::Draft;
    out.createdAt = QDateTime::currentDateTimeUtc();
    out.updatedAt = out.createdAt;
    out.createdByUserId = actorUserId;
    out.updatedByUserId = actorUserId;

    auto result = m_questionRepo.insertQuestion(out);
    if (result.isErr())
        return result;

    QJsonObject payload;
    payload[QStringLiteral("question_id")] = out.id;
    payload[QStringLiteral("difficulty")] = out.difficulty;
    payload[QStringLiteral("discrimination")] = out.discrimination;
    payload[QStringLiteral("status")] = QStringLiteral("Draft");
    m_auditService.recordEvent(actorUserId, AuditEventType::QuestionCreated,
                                QStringLiteral("Question"), out.id,
                                {}, payload);

    Logger::instance().info(QStringLiteral("QuestionService"),
                             QStringLiteral("Question created"),
                             {{QStringLiteral("question_id"), out.id}});

    return result;
}

Result<Question> QuestionService::updateQuestion(const Question& q,
                                                    const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::ContentManager);
    if (authResult.isErr())
        return Result<Question>::err(authResult.errorCode(), authResult.errorMessage());

    auto valid = validateQuestion(q);
    if (valid.isErr())
        return Result<Question>::err(valid.errorCode(), valid.errorMessage());

    // Fetch old version for audit diff
    auto oldResult = m_questionRepo.findQuestionById(q.id);
    if (oldResult.isErr())
        return oldResult;

    Question out = q;
    out.updatedAt = QDateTime::currentDateTimeUtc();
    out.updatedByUserId = actorUserId;

    auto result = m_questionRepo.updateQuestion(out);
    if (result.isErr())
        return result;

    const auto& old = oldResult.value();
    QJsonObject before;
    before[QStringLiteral("difficulty")] = old.difficulty;
    before[QStringLiteral("discrimination")] = old.discrimination;

    QJsonObject after;
    after[QStringLiteral("difficulty")] = out.difficulty;
    after[QStringLiteral("discrimination")] = out.discrimination;

    m_auditService.recordEvent(actorUserId, AuditEventType::QuestionUpdated,
                                QStringLiteral("Question"), out.id,
                                before, after);

    return result;
}

Result<void> QuestionService::deleteQuestion(const QString& questionId,
                                               const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::ContentManager);
    if (authResult.isErr())
        return Result<void>::err(authResult.errorCode(), authResult.errorMessage());

    auto result = m_questionRepo.softDeleteQuestion(questionId);
    if (result.isErr())
        return result;

    QJsonObject payload;
    payload[QStringLiteral("question_id")] = questionId;
    m_auditService.recordEvent(actorUserId, AuditEventType::QuestionDeleted,
                                QStringLiteral("Question"), questionId,
                                payload, {});

    return result;
}

Result<Question> QuestionService::getQuestion(const QString& questionId)
{
    return m_questionRepo.findQuestionById(questionId);
}

Result<QList<Question>> QuestionService::queryQuestions(const QuestionFilter& filter)
{
    return m_questionRepo.queryQuestions(filter);
}

Result<bool> QuestionService::externalIdExists(const QString& externalId)
{
    return m_questionRepo.externalIdExists(externalId);
}

// ── Knowledge-point tree ─────────────────────────────────────────────────────

Result<KnowledgePoint> QuestionService::createKnowledgePoint(const KnowledgePoint& kp,
                                                                const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::ContentManager);
    if (authResult.isErr())
        return Result<KnowledgePoint>::err(authResult.errorCode(), authResult.errorMessage());

    if (kp.name.isEmpty())
        return Result<KnowledgePoint>::err(ErrorCode::ValidationFailed,
                                            QStringLiteral("Knowledge point name is required"));

    KnowledgePoint out = kp;
    out.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    out.createdAt = QDateTime::currentDateTimeUtc();
    out.deleted = false;

    auto result = m_kpRepo.insertKP(out);
    if (result.isErr())
        return result;

    QJsonObject payload;
    payload[QStringLiteral("kp_id")] = result.value().id;
    payload[QStringLiteral("name")] = result.value().name;
    payload[QStringLiteral("path")] = result.value().path;
    m_auditService.recordEvent(actorUserId, AuditEventType::KnowledgePointCreated,
                                QStringLiteral("KnowledgePoint"), result.value().id,
                                {}, payload);

    return result;
}

Result<KnowledgePoint> QuestionService::updateKnowledgePoint(const KnowledgePoint& kp,
                                                                const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::ContentManager);
    if (authResult.isErr())
        return Result<KnowledgePoint>::err(authResult.errorCode(), authResult.errorMessage());

    if (kp.name.isEmpty())
        return Result<KnowledgePoint>::err(ErrorCode::ValidationFailed,
                                            QStringLiteral("Knowledge point name is required"));

    auto oldResult = m_kpRepo.findKPById(kp.id);
    if (oldResult.isErr())
        return oldResult;

    auto result = m_kpRepo.updateKP(kp);
    if (result.isErr())
        return result;

    const auto& old = oldResult.value();
    QJsonObject before;
    before[QStringLiteral("name")] = old.name;
    before[QStringLiteral("path")] = old.path;
    before[QStringLiteral("parent_id")] = old.parentId;

    QJsonObject after;
    after[QStringLiteral("name")] = result.value().name;
    after[QStringLiteral("path")] = result.value().path;
    after[QStringLiteral("parent_id")] = result.value().parentId;

    m_auditService.recordEvent(actorUserId, AuditEventType::KnowledgePointUpdated,
                                QStringLiteral("KnowledgePoint"), kp.id,
                                before, after);

    return result;
}

Result<void> QuestionService::deleteKnowledgePoint(const QString& kpId,
                                                     const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::ContentManager);
    if (authResult.isErr())
        return Result<void>::err(authResult.errorCode(), authResult.errorMessage());

    auto result = m_kpRepo.softDeleteKP(kpId);
    if (result.isErr())
        return result;

    QJsonObject payload;
    payload[QStringLiteral("kp_id")] = kpId;
    m_auditService.recordEvent(actorUserId, AuditEventType::KnowledgePointDeleted,
                                QStringLiteral("KnowledgePoint"), kpId,
                                payload, {});

    return result;
}

Result<QList<KnowledgePoint>> QuestionService::getTree()
{
    return m_kpRepo.getTree();
}

Result<QList<KnowledgePoint>> QuestionService::getDescendants(const QString& kpId)
{
    return m_kpRepo.getDescendants(kpId);
}

// ── Mappings ─────────────────────────────────────────────────────────────────

Result<void> QuestionService::mapQuestionToKP(const QString& questionId,
                                                const QString& kpId,
                                                const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::ContentManager);
    if (authResult.isErr())
        return Result<void>::err(authResult.errorCode(), authResult.errorMessage());

    // Validate both exist
    auto qResult = m_questionRepo.findQuestionById(questionId);
    if (qResult.isErr())
        return Result<void>::err(qResult.errorCode(), qResult.errorMessage());

    auto kpResult = m_kpRepo.findKPById(kpId);
    if (kpResult.isErr())
        return Result<void>::err(kpResult.errorCode(), kpResult.errorMessage());

    QuestionKPMapping mapping;
    mapping.questionId       = questionId;
    mapping.knowledgePointId = kpId;
    mapping.mappedAt         = QDateTime::currentDateTimeUtc();
    mapping.mappedByUserId   = actorUserId;

    auto result = m_questionRepo.insertKPMapping(mapping);
    if (result.isErr())
        return result;

    QJsonObject payload;
    payload[QStringLiteral("question_id")] = questionId;
    payload[QStringLiteral("kp_id")] = kpId;
    m_auditService.recordEvent(actorUserId, AuditEventType::KnowledgePointMapped,
                                QStringLiteral("QuestionKPMapping"), questionId,
                                {}, payload);

    return result;
}

Result<void> QuestionService::unmapQuestionFromKP(const QString& questionId,
                                                    const QString& kpId,
                                                    const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::ContentManager);
    if (authResult.isErr())
        return Result<void>::err(authResult.errorCode(), authResult.errorMessage());

    auto result = m_questionRepo.deleteKPMapping(questionId, kpId);
    if (result.isErr())
        return result;

    QJsonObject payload;
    payload[QStringLiteral("question_id")] = questionId;
    payload[QStringLiteral("kp_id")] = kpId;
    m_auditService.recordEvent(actorUserId, AuditEventType::KnowledgePointUnmapped,
                                QStringLiteral("QuestionKPMapping"), questionId,
                                payload, {});

    return result;
}

Result<QList<QuestionKPMapping>> QuestionService::getQuestionKPMappings(
    const QString& questionId)
{
    return m_questionRepo.getKPMappings(questionId);
}

// ── Tags ─────────────────────────────────────────────────────────────────────

Result<Tag> QuestionService::createTag(const QString& name, const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::ContentManager);
    if (authResult.isErr())
        return Result<Tag>::err(authResult.errorCode(), authResult.errorMessage());

    if (name.isEmpty())
        return Result<Tag>::err(ErrorCode::ValidationFailed,
                                 QStringLiteral("Tag name is required"));

    if (name.length() > Validation::TagNameMaxChars)
        return Result<Tag>::err(ErrorCode::ValidationFailed,
                                 QStringLiteral("Tag name exceeds maximum length"));

    // Check uniqueness
    auto existing = m_questionRepo.findTagByName(name);
    if (existing.isOk())
        return Result<Tag>::err(ErrorCode::AlreadyExists,
                                 QStringLiteral("Tag already exists"));

    Tag tag;
    tag.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tag.name = name;
    tag.createdAt = QDateTime::currentDateTimeUtc();

    auto result = m_questionRepo.insertTag(tag);
    if (result.isErr())
        return result;

    QJsonObject payload;
    payload[QStringLiteral("tag_id")] = tag.id;
    payload[QStringLiteral("name")] = tag.name;
    m_auditService.recordEvent(actorUserId, AuditEventType::TagCreated,
                                QStringLiteral("Tag"), tag.id,
                                {}, payload);

    return result;
}

Result<void> QuestionService::applyTag(const QString& questionId,
                                         const QString& tagId,
                                         const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::ContentManager);
    if (authResult.isErr())
        return Result<void>::err(authResult.errorCode(), authResult.errorMessage());

    // Validate question exists
    auto qResult = m_questionRepo.findQuestionById(questionId);
    if (qResult.isErr())
        return Result<void>::err(qResult.errorCode(), qResult.errorMessage());

    QuestionTagMapping mapping;
    mapping.questionId     = questionId;
    mapping.tagId          = tagId;
    mapping.appliedAt      = QDateTime::currentDateTimeUtc();
    mapping.appliedByUserId = actorUserId;

    auto result = m_questionRepo.insertTagMapping(mapping);
    if (result.isErr())
        return result;

    QJsonObject payload;
    payload[QStringLiteral("question_id")] = questionId;
    payload[QStringLiteral("tag_id")] = tagId;
    m_auditService.recordEvent(actorUserId, AuditEventType::TagApplied,
                                QStringLiteral("QuestionTagMapping"), questionId,
                                {}, payload);

    return result;
}

Result<void> QuestionService::removeTag(const QString& questionId,
                                          const QString& tagId,
                                          const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::ContentManager);
    if (authResult.isErr())
        return Result<void>::err(authResult.errorCode(), authResult.errorMessage());

    auto result = m_questionRepo.deleteTagMapping(questionId, tagId);
    if (result.isErr())
        return result;

    QJsonObject payload;
    payload[QStringLiteral("question_id")] = questionId;
    payload[QStringLiteral("tag_id")] = tagId;
    m_auditService.recordEvent(actorUserId, AuditEventType::TagRemoved,
                                QStringLiteral("QuestionTagMapping"), questionId,
                                payload, {});

    return result;
}

Result<QList<Tag>> QuestionService::listTags()
{
    return m_questionRepo.listTags();
}

Result<QList<QuestionTagMapping>> QuestionService::getQuestionTagMappings(
    const QString& questionId)
{
    return m_questionRepo.getTagMappings(questionId);
}
