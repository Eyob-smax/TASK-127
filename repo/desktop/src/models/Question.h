#pragma once
// Question.h — ProctorOps
// Domain models for question governance, knowledge-point trees, tags,
// and the combined query builder filter.

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <optional>

// ── QuestionStatus ────────────────────────────────────────────────────────────
enum class QuestionStatus {
    Draft,    // not yet published; not visible in public query results
    Active,   // published and available for exam use
    Archived, // retired; searchable but not assigned to new exams
    Deleted,  // soft-deleted; excluded from all query results
};

// ── Question ─────────────────────────────────────────────────────────────────
struct Question {
    QString        id;
    QString        bodyText;              // max Validation::QuestionBodyMaxChars
    QStringList    answerOptions;         // 2–6 elements; stored as JSON in DB
    int            correctAnswerIndex;    // 0-based index into answerOptions
    int            difficulty;            // 1–5 inclusive
    double         discrimination;        // 0.00–1.00 inclusive
    QuestionStatus status;
    QString        externalId;            // optional; used for duplicate detection across imports
    QDateTime      createdAt;
    QDateTime      updatedAt;
    QString        createdByUserId;
    QString        updatedByUserId;
    // Invariant: correctAnswerIndex in [0, answerOptions.size()-1]
    // Invariant: difficulty in [1, 5]
    // Invariant: discrimination in [0.00, 1.00]
};

// ── KnowledgePoint ───────────────────────────────────────────────────────────
// Chapter tree node. Supports arbitrary depth via parent_id self-reference.
// Materialized path stored for efficient subtree queries.
struct KnowledgePoint {
    QString   id;
    QString   name;
    QString   parentId;      // empty string for root nodes
    int       position;      // ordering among siblings with the same parentId
    QString   path;          // materialized slash-delimited path, e.g. "Safety/Electrical"
    QDateTime createdAt;
    bool      deleted;
};

// ── Tag ───────────────────────────────────────────────────────────────────────
struct Tag {
    QString   id;
    QString   name;      // unique; max Validation::TagNameMaxChars
    QDateTime createdAt;
};

// ── QuestionKPMapping ────────────────────────────────────────────────────────
// Many-to-many: question ↔ knowledge point.
struct QuestionKPMapping {
    QString   questionId;
    QString   knowledgePointId;
    QDateTime mappedAt;
    QString   mappedByUserId;
};

// ── QuestionTagMapping ────────────────────────────────────────────────────────
// Many-to-many: question ↔ tag.
struct QuestionTagMapping {
    QString   questionId;
    QString   tagId;
    QDateTime appliedAt;
    QString   appliedByUserId;
};

// ── QuestionFilter ────────────────────────────────────────────────────────────
// Combined query builder parameters. All fields are optional (empty/nullopt = not filtered).
// Example: "Chapter 3 AND difficulty >= 4 AND tag='Safety'"
struct QuestionFilter {
    QString                knowledgePointId;   // filter by KP subtree (includes descendants)
    std::optional<int>     difficultyMin;      // 1–5
    std::optional<int>     difficultyMax;      // 1–5
    std::optional<double>  discriminationMin;  // 0.00–1.00
    std::optional<double>  discriminationMax;
    QStringList            tagIds;             // OR semantics within list; AND with other filters
    QStringList            knowledgePointIds;  // additional explicit KP ids to include
    QString                textSearch;         // full-text search on bodyText
    QuestionStatus         statusFilter = QuestionStatus::Active; // default: active only
    int                    limit  = 100;
    int                    offset = 0;
};
