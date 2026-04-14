#pragma once
// QuestionRepository.h — ProctorOps
// Concrete SQLite implementation of IQuestionRepository.
// Manages questions, KP mappings, tag mappings, tags, and the combined query builder.

#include "IQuestionRepository.h"
#include <QSqlDatabase>

class QuestionRepository : public IQuestionRepository {
public:
    explicit QuestionRepository(QSqlDatabase& db);

    // ── Questions ──────────────────────────────────────────────────────────
    Result<Question>         insertQuestion(const Question& q) override;
    Result<Question>         updateQuestion(const Question& q) override;
    Result<void>             softDeleteQuestion(const QString& questionId) override;
    Result<Question>         findQuestionById(const QString& questionId) override;
    Result<QList<Question>>  queryQuestions(const QuestionFilter& filter) override;
    Result<bool>             externalIdExists(const QString& externalId) override;

    // ── KP mappings ────────────────────────────────────────────────────────
    Result<void>                     insertKPMapping(const QuestionKPMapping& m) override;
    Result<void>                     deleteKPMapping(const QString& questionId,
                                                      const QString& kpId) override;
    Result<QList<QuestionKPMapping>> getKPMappings(const QString& questionId) override;

    // ── Tag mappings ───────────────────────────────────────────────────────
    Result<void>                      insertTagMapping(const QuestionTagMapping& m) override;
    Result<void>                      deleteTagMapping(const QString& questionId,
                                                        const QString& tagId) override;
    Result<QList<QuestionTagMapping>> getTagMappings(const QString& questionId) override;

    // ── Tags ───────────────────────────────────────────────────────────────
    Result<Tag>         insertTag(const Tag& tag) override;
    Result<Tag>         findTagByName(const QString& name) override;
    Result<QList<Tag>>  listTags() override;

private:
    QSqlDatabase& m_db;

    static Question rowToQuestion(const QSqlQuery& q);
    static QString questionStatusToString(QuestionStatus s);
    static QuestionStatus questionStatusFromString(const QString& s);
};
