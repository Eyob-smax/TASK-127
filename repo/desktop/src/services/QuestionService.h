#pragma once
// QuestionService.h — ProctorOps
// Content governance business logic: question CRUD with validation and audit,
// knowledge-point tree management, many-to-many mappings, tags, and query builder.

#include "repositories/IQuestionRepository.h"
#include "services/AuditService.h"
#include "utils/Result.h"
#include "models/Question.h"
#include "models/CommonTypes.h"

class IKnowledgePointRepository;
class AuthService;

class QuestionService {
public:
    QuestionService(IQuestionRepository& questionRepo,
                    IKnowledgePointRepository& kpRepo,
                    AuditService& auditService,
                    AuthService& authService);

    // ── Questions ──────────────────────────────────────────────────────────
    [[nodiscard]] Result<Question> createQuestion(const Question& q,
                                                    const QString& actorUserId);
    [[nodiscard]] Result<Question> updateQuestion(const Question& q,
                                                    const QString& actorUserId);
    [[nodiscard]] Result<void>     deleteQuestion(const QString& questionId,
                                                    const QString& actorUserId);
    [[nodiscard]] Result<Question> getQuestion(const QString& questionId);
    [[nodiscard]] Result<QList<Question>> queryQuestions(const QuestionFilter& filter);
    [[nodiscard]] Result<bool>     externalIdExists(const QString& externalId);

    // ── Knowledge-point tree ───────────────────────────────────────────────
    [[nodiscard]] Result<KnowledgePoint> createKnowledgePoint(const KnowledgePoint& kp,
                                                                const QString& actorUserId);
    [[nodiscard]] Result<KnowledgePoint> updateKnowledgePoint(const KnowledgePoint& kp,
                                                                const QString& actorUserId);
    [[nodiscard]] Result<void>           deleteKnowledgePoint(const QString& kpId,
                                                                const QString& actorUserId);
    [[nodiscard]] Result<QList<KnowledgePoint>> getTree();
    [[nodiscard]] Result<QList<KnowledgePoint>> getDescendants(const QString& kpId);

    // ── Mappings ───────────────────────────────────────────────────────────
    [[nodiscard]] Result<void> mapQuestionToKP(const QString& questionId,
                                                const QString& kpId,
                                                const QString& actorUserId);
    [[nodiscard]] Result<void> unmapQuestionFromKP(const QString& questionId,
                                                    const QString& kpId,
                                                    const QString& actorUserId);
    [[nodiscard]] Result<QList<QuestionKPMapping>> getQuestionKPMappings(
        const QString& questionId);

    // ── Tags ───────────────────────────────────────────────────────────────
    [[nodiscard]] Result<Tag>         createTag(const QString& name,
                                                 const QString& actorUserId);
    [[nodiscard]] Result<void>        applyTag(const QString& questionId,
                                                const QString& tagId,
                                                const QString& actorUserId);
    [[nodiscard]] Result<void>        removeTag(const QString& questionId,
                                                 const QString& tagId,
                                                 const QString& actorUserId);
    [[nodiscard]] Result<QList<Tag>>  listTags();
    [[nodiscard]] Result<QList<QuestionTagMapping>> getQuestionTagMappings(
        const QString& questionId);

private:
    IQuestionRepository&       m_questionRepo;
    IKnowledgePointRepository& m_kpRepo;
    AuditService&              m_auditService;
    AuthService&               m_authService;

    [[nodiscard]] Result<void> validateQuestion(const Question& q);
};
