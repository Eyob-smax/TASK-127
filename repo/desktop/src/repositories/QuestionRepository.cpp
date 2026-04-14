// QuestionRepository.cpp — ProctorOps
// Concrete SQLite implementation for questions, KP mappings, tag mappings, tags,
// and the combined query builder.

#include "QuestionRepository.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>

QuestionRepository::QuestionRepository(QSqlDatabase& db)
    : m_db(db)
{
}

// ── Status string conversions ────────────────────────────────────────────────

QString QuestionRepository::questionStatusToString(QuestionStatus s)
{
    switch (s) {
    case QuestionStatus::Draft:    return QStringLiteral("Draft");
    case QuestionStatus::Active:   return QStringLiteral("Active");
    case QuestionStatus::Archived: return QStringLiteral("Archived");
    case QuestionStatus::Deleted:  return QStringLiteral("Deleted");
    }
    return QStringLiteral("Draft");
}

QuestionStatus QuestionRepository::questionStatusFromString(const QString& s)
{
    if (s == QStringLiteral("Active"))   return QuestionStatus::Active;
    if (s == QStringLiteral("Archived")) return QuestionStatus::Archived;
    if (s == QStringLiteral("Deleted"))  return QuestionStatus::Deleted;
    return QuestionStatus::Draft;
}

// ── Helper: row → Question ───────────────────────────────────────────────────

Question QuestionRepository::rowToQuestion(const QSqlQuery& q)
{
    Question out;
    out.id                = q.value(0).toString();
    out.bodyText          = q.value(1).toString();

    // Parse JSON array of answer options
    QJsonDocument doc = QJsonDocument::fromJson(q.value(2).toString().toUtf8());
    if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        for (const auto& v : arr)
            out.answerOptions.append(v.toString());
    }

    out.correctAnswerIndex = q.value(3).toInt();
    out.difficulty         = q.value(4).toInt();
    out.discrimination     = q.value(5).toDouble();
    out.status             = questionStatusFromString(q.value(6).toString());
    out.externalId         = q.value(7).toString();
    out.createdAt          = QDateTime::fromString(q.value(8).toString(), Qt::ISODateWithMs);
    out.updatedAt          = QDateTime::fromString(q.value(9).toString(), Qt::ISODateWithMs);
    out.createdByUserId    = q.value(10).toString();
    out.updatedByUserId    = q.value(11).toString();
    return out;
}

// ── Questions ──────────────────────────────────────────────────────────────

Result<Question> QuestionRepository::insertQuestion(const Question& question)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO questions "
        "(id, body_text, answer_options_json, correct_answer_index, "
        " difficulty, discrimination, status, external_id, "
        " created_at, updated_at, created_by_user_id, updated_by_user_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

    q.addBindValue(question.id);
    q.addBindValue(question.bodyText);

    // Serialize answer options to JSON array
    QJsonArray optArr;
    for (const auto& opt : question.answerOptions)
        optArr.append(opt);
    q.addBindValue(QString::fromUtf8(QJsonDocument(optArr).toJson(QJsonDocument::Compact)));

    q.addBindValue(question.correctAnswerIndex);
    q.addBindValue(question.difficulty);
    q.addBindValue(question.discrimination);
    q.addBindValue(questionStatusToString(question.status));
    q.addBindValue(question.externalId.isEmpty() ? QVariant() : question.externalId);
    q.addBindValue(question.createdAt.toString(Qt::ISODateWithMs));
    q.addBindValue(question.updatedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(question.createdByUserId);
    q.addBindValue(question.updatedByUserId);

    if (!q.exec())
        return Result<Question>::err(ErrorCode::DbError, q.lastError().text());

    return Result<Question>::ok(question);
}

Result<Question> QuestionRepository::updateQuestion(const Question& question)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE questions SET "
        "body_text = ?, answer_options_json = ?, correct_answer_index = ?, "
        "difficulty = ?, discrimination = ?, status = ?, external_id = ?, "
        "updated_at = ?, updated_by_user_id = ? "
        "WHERE id = ?"));

    q.addBindValue(question.bodyText);

    QJsonArray optArr;
    for (const auto& opt : question.answerOptions)
        optArr.append(opt);
    q.addBindValue(QString::fromUtf8(QJsonDocument(optArr).toJson(QJsonDocument::Compact)));

    q.addBindValue(question.correctAnswerIndex);
    q.addBindValue(question.difficulty);
    q.addBindValue(question.discrimination);
    q.addBindValue(questionStatusToString(question.status));
    q.addBindValue(question.externalId.isEmpty() ? QVariant() : question.externalId);
    q.addBindValue(question.updatedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(question.updatedByUserId);
    q.addBindValue(question.id);

    if (!q.exec())
        return Result<Question>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<Question>::err(ErrorCode::NotFound);

    return Result<Question>::ok(question);
}

Result<void> QuestionRepository::softDeleteQuestion(const QString& questionId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE questions SET status = 'Deleted', updated_at = ? WHERE id = ?"));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    q.addBindValue(questionId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<void>::err(ErrorCode::NotFound);

    return Result<void>::ok();
}

Result<Question> QuestionRepository::findQuestionById(const QString& questionId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, body_text, answer_options_json, correct_answer_index, "
        "       difficulty, discrimination, status, external_id, "
        "       created_at, updated_at, created_by_user_id, updated_by_user_id "
        "FROM questions WHERE id = ?"));
    q.addBindValue(questionId);

    if (!q.exec())
        return Result<Question>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<Question>::err(ErrorCode::NotFound);

    return Result<Question>::ok(rowToQuestion(q));
}

Result<QList<Question>> QuestionRepository::queryQuestions(const QuestionFilter& filter)
{
    // Build dynamic WHERE clause
    QString sql = QStringLiteral(
        "SELECT DISTINCT q.id, q.body_text, q.answer_options_json, q.correct_answer_index, "
        "       q.difficulty, q.discrimination, q.status, q.external_id, "
        "       q.created_at, q.updated_at, q.created_by_user_id, q.updated_by_user_id "
        "FROM questions q ");

    QStringList joins;
    QStringList conditions;
    QVariantList binds;

    // KP subtree filter: join question_kp_mappings + knowledge_points
    bool needsKpJoin = !filter.knowledgePointId.isEmpty() || !filter.knowledgePointIds.isEmpty();
    if (needsKpJoin) {
        joins << QStringLiteral(
            "JOIN question_kp_mappings qkm ON qkm.question_id = q.id "
            "JOIN knowledge_points kp ON kp.id = qkm.knowledge_point_id ");
    }

    // Tag filter: join question_tag_mappings
    bool needsTagJoin = !filter.tagIds.isEmpty();
    if (needsTagJoin) {
        joins << QStringLiteral(
            "JOIN question_tag_mappings qtm ON qtm.question_id = q.id ");
    }

    for (const auto& j : joins)
        sql += j;

    sql += QStringLiteral("WHERE 1=1 ");

    // Status filter
    conditions << QStringLiteral("q.status = ?");
    binds << questionStatusToString(filter.statusFilter);

    // KP subtree
    if (!filter.knowledgePointId.isEmpty() && filter.knowledgePointIds.isEmpty()) {
        conditions << QStringLiteral("(kp.path = ? OR kp.path LIKE ? || '/%')");
        // Need to look up the KP path first — use subquery approach instead
        conditions.removeLast();
        conditions << QStringLiteral(
            "(kp.id = ? OR kp.path LIKE (SELECT path FROM knowledge_points WHERE id = ?) || '/%')");
        binds << filter.knowledgePointId;
        binds << filter.knowledgePointId;
    } else if (!filter.knowledgePointId.isEmpty() || !filter.knowledgePointIds.isEmpty()) {
        QStringList kpConditions;
        if (!filter.knowledgePointId.isEmpty()) {
            kpConditions << QStringLiteral(
                "(kp.id = ? OR kp.path LIKE (SELECT path FROM knowledge_points WHERE id = ?) || '/%')");
            binds << filter.knowledgePointId;
            binds << filter.knowledgePointId;
        }
        for (const auto& kpId : filter.knowledgePointIds) {
            kpConditions << QStringLiteral("kp.id = ?");
            binds << kpId;
        }
        conditions << QStringLiteral("(") + kpConditions.join(QStringLiteral(" OR ")) + QStringLiteral(")");
    }

    // Tag filter (OR semantics)
    if (!filter.tagIds.isEmpty()) {
        QStringList placeholders;
        for (const auto& tagId : filter.tagIds) {
            placeholders << QStringLiteral("?");
            binds << tagId;
        }
        conditions << QStringLiteral("qtm.tag_id IN (")
                      + placeholders.join(QStringLiteral(", ")) + QStringLiteral(")");
    }

    // Difficulty range
    if (filter.difficultyMin.has_value()) {
        conditions << QStringLiteral("q.difficulty >= ?");
        binds << filter.difficultyMin.value();
    }
    if (filter.difficultyMax.has_value()) {
        conditions << QStringLiteral("q.difficulty <= ?");
        binds << filter.difficultyMax.value();
    }

    // Discrimination range
    if (filter.discriminationMin.has_value()) {
        conditions << QStringLiteral("q.discrimination >= ?");
        binds << filter.discriminationMin.value();
    }
    if (filter.discriminationMax.has_value()) {
        conditions << QStringLiteral("q.discrimination <= ?");
        binds << filter.discriminationMax.value();
    }

    // Text search
    if (!filter.textSearch.isEmpty()) {
        conditions << QStringLiteral("q.body_text LIKE '%' || ? || '%'");
        binds << filter.textSearch;
    }

    for (const auto& c : conditions)
        sql += QStringLiteral(" AND ") + c;

    sql += QStringLiteral(" ORDER BY q.updated_at DESC LIMIT ? OFFSET ?");
    binds << filter.limit;
    binds << filter.offset;

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const auto& v : binds)
        q.addBindValue(v);

    if (!q.exec())
        return Result<QList<Question>>::err(ErrorCode::DbError, q.lastError().text());

    QList<Question> results;
    while (q.next())
        results.append(rowToQuestion(q));

    return Result<QList<Question>>::ok(std::move(results));
}

Result<bool> QuestionRepository::externalIdExists(const QString& externalId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT 1 FROM questions WHERE external_id = ? AND status != 'Deleted' LIMIT 1"));
    q.addBindValue(externalId);

    if (!q.exec())
        return Result<bool>::err(ErrorCode::DbError, q.lastError().text());

    return Result<bool>::ok(q.next());
}

// ── KP mappings ──────────────────────────────────────────────────────────────

Result<void> QuestionRepository::insertKPMapping(const QuestionKPMapping& m)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO question_kp_mappings "
        "(question_id, knowledge_point_id, mapped_at, mapped_by_user_id) "
        "VALUES (?, ?, ?, ?)"));
    q.addBindValue(m.questionId);
    q.addBindValue(m.knowledgePointId);
    q.addBindValue(m.mappedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(m.mappedByUserId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<void> QuestionRepository::deleteKPMapping(const QString& questionId,
                                                   const QString& kpId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM question_kp_mappings "
        "WHERE question_id = ? AND knowledge_point_id = ?"));
    q.addBindValue(questionId);
    q.addBindValue(kpId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<QList<QuestionKPMapping>> QuestionRepository::getKPMappings(const QString& questionId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT question_id, knowledge_point_id, mapped_at, mapped_by_user_id "
        "FROM question_kp_mappings WHERE question_id = ?"));
    q.addBindValue(questionId);

    if (!q.exec())
        return Result<QList<QuestionKPMapping>>::err(ErrorCode::DbError, q.lastError().text());

    QList<QuestionKPMapping> results;
    while (q.next()) {
        QuestionKPMapping m;
        m.questionId       = q.value(0).toString();
        m.knowledgePointId = q.value(1).toString();
        m.mappedAt         = QDateTime::fromString(q.value(2).toString(), Qt::ISODateWithMs);
        m.mappedByUserId   = q.value(3).toString();
        results.append(std::move(m));
    }
    return Result<QList<QuestionKPMapping>>::ok(std::move(results));
}

// ── Tag mappings ─────────────────────────────────────────────────────────────

Result<void> QuestionRepository::insertTagMapping(const QuestionTagMapping& m)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO question_tag_mappings "
        "(question_id, tag_id, applied_at, applied_by_user_id) "
        "VALUES (?, ?, ?, ?)"));
    q.addBindValue(m.questionId);
    q.addBindValue(m.tagId);
    q.addBindValue(m.appliedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(m.appliedByUserId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<void> QuestionRepository::deleteTagMapping(const QString& questionId,
                                                    const QString& tagId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM question_tag_mappings "
        "WHERE question_id = ? AND tag_id = ?"));
    q.addBindValue(questionId);
    q.addBindValue(tagId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<QList<QuestionTagMapping>> QuestionRepository::getTagMappings(const QString& questionId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT question_id, tag_id, applied_at, applied_by_user_id "
        "FROM question_tag_mappings WHERE question_id = ?"));
    q.addBindValue(questionId);

    if (!q.exec())
        return Result<QList<QuestionTagMapping>>::err(ErrorCode::DbError, q.lastError().text());

    QList<QuestionTagMapping> results;
    while (q.next()) {
        QuestionTagMapping m;
        m.questionId     = q.value(0).toString();
        m.tagId          = q.value(1).toString();
        m.appliedAt      = QDateTime::fromString(q.value(2).toString(), Qt::ISODateWithMs);
        m.appliedByUserId = q.value(3).toString();
        results.append(std::move(m));
    }
    return Result<QList<QuestionTagMapping>>::ok(std::move(results));
}

// ── Tags ─────────────────────────────────────────────────────────────────────

Result<Tag> QuestionRepository::insertTag(const Tag& tag)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO tags (id, name, created_at) VALUES (?, ?, ?)"));
    q.addBindValue(tag.id);
    q.addBindValue(tag.name);
    q.addBindValue(tag.createdAt.toString(Qt::ISODateWithMs));

    if (!q.exec())
        return Result<Tag>::err(ErrorCode::DbError, q.lastError().text());

    return Result<Tag>::ok(tag);
}

Result<Tag> QuestionRepository::findTagByName(const QString& name)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, name, created_at FROM tags WHERE name = ?"));
    q.addBindValue(name);

    if (!q.exec())
        return Result<Tag>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<Tag>::err(ErrorCode::NotFound);

    Tag tag;
    tag.id        = q.value(0).toString();
    tag.name      = q.value(1).toString();
    tag.createdAt = QDateTime::fromString(q.value(2).toString(), Qt::ISODateWithMs);
    return Result<Tag>::ok(std::move(tag));
}

Result<QList<Tag>> QuestionRepository::listTags()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, name, created_at FROM tags ORDER BY name"));

    if (!q.exec())
        return Result<QList<Tag>>::err(ErrorCode::DbError, q.lastError().text());

    QList<Tag> results;
    while (q.next()) {
        Tag tag;
        tag.id        = q.value(0).toString();
        tag.name      = q.value(1).toString();
        tag.createdAt = QDateTime::fromString(q.value(2).toString(), Qt::ISODateWithMs);
        results.append(std::move(tag));
    }
    return Result<QList<Tag>>::ok(std::move(results));
}
