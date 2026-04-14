#pragma once
// IQuestionRepository.h — ProctorOps
// Pure interface for question CRUD, tag management, and the combined query builder.

#include "models/Question.h"
#include "utils/Result.h"
#include <QString>
#include <QList>

class IQuestionRepository {
public:
    virtual ~IQuestionRepository() = default;

    // ── Questions ──────────────────────────────────────────────────────────
    virtual Result<Question>         insertQuestion(const Question& q)                      = 0;
    virtual Result<Question>         updateQuestion(const Question& q)                      = 0;
    virtual Result<void>             softDeleteQuestion(const QString& questionId)          = 0;
    virtual Result<Question>         findQuestionById(const QString& questionId)            = 0;
    virtual Result<QList<Question>>  queryQuestions(const QuestionFilter& filter)          = 0;
    // Duplicate detection for import: find by externalId if non-empty.
    virtual Result<bool>             externalIdExists(const QString& externalId)            = 0;

    // ── KP mappings ────────────────────────────────────────────────────────
    virtual Result<void>                     insertKPMapping(const QuestionKPMapping& m)    = 0;
    virtual Result<void>                     deleteKPMapping(const QString& questionId,
                                                              const QString& kpId)          = 0;
    virtual Result<QList<QuestionKPMapping>> getKPMappings(const QString& questionId)       = 0;

    // ── Tag mappings ───────────────────────────────────────────────────────
    virtual Result<void>                      insertTagMapping(const QuestionTagMapping& m) = 0;
    virtual Result<void>                      deleteTagMapping(const QString& questionId,
                                                                const QString& tagId)       = 0;
    virtual Result<QList<QuestionTagMapping>> getTagMappings(const QString& questionId)     = 0;

    // ── Tags ───────────────────────────────────────────────────────────────
    virtual Result<Tag>         insertTag(const Tag& tag)                                   = 0;
    virtual Result<Tag>         findTagByName(const QString& name)                          = 0;
    virtual Result<QList<Tag>>  listTags()                                                  = 0;
};

class IKnowledgePointRepository {
public:
    virtual ~IKnowledgePointRepository() = default;

    virtual Result<KnowledgePoint>         insertKP(const KnowledgePoint& kp)               = 0;
    virtual Result<KnowledgePoint>         updateKP(const KnowledgePoint& kp)               = 0;
    virtual Result<void>                   softDeleteKP(const QString& kpId)                 = 0;
    virtual Result<KnowledgePoint>         findKPById(const QString& kpId)                   = 0;
    // Returns the full tree ordered by materialized path, then position.
    virtual Result<QList<KnowledgePoint>>  getTree()                                        = 0;
    // Returns all descendants of a KP (for subtree query builder expansion).
    virtual Result<QList<KnowledgePoint>>  getDescendants(const QString& kpId)              = 0;
};
