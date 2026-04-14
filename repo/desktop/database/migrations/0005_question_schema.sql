-- Migration: 0005_question_schema.sql
-- Domain: Questions, knowledge-point trees, many-to-many mappings, and tags.
-- Invariants enforced:
--   - difficulty in [1, 5] via CHECK
--   - discrimination in [0.00, 1.00] via CHECK
--   - correct_answer_index >= 0 via CHECK (upper bound enforced at service layer)
--   - tag names unique via UNIQUE index
--   - materialized path stored for efficient KP subtree queries
--   - many-to-many integrity enforced by composite PRIMARY KEYs

-- Knowledge points (chapter tree nodes)
CREATE TABLE knowledge_points (
    id          TEXT    PRIMARY KEY,
    name        TEXT    NOT NULL,
    parent_id   TEXT    REFERENCES knowledge_points(id) ON DELETE RESTRICT, -- NULL = root
    position    INTEGER NOT NULL DEFAULT 0,   -- ordering among siblings
    path        TEXT    NOT NULL,             -- materialized path e.g. "Safety/Electrical"
    created_at  TEXT    NOT NULL,
    deleted     INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX idx_kp_parent   ON knowledge_points (parent_id);
CREATE INDEX idx_kp_path     ON knowledge_points (path);
CREATE INDEX idx_kp_deleted  ON knowledge_points (deleted);

-- Tags (unique names; used as labels on questions)
CREATE TABLE tags (
    id          TEXT    PRIMARY KEY,
    name        TEXT    NOT NULL,
    created_at  TEXT    NOT NULL,
    CONSTRAINT uq_tag_name UNIQUE (name)
);

-- Questions
CREATE TABLE questions (
    id                    TEXT    PRIMARY KEY,
    body_text             TEXT    NOT NULL,           -- max 4000 chars (enforced at service)
    answer_options_json   TEXT    NOT NULL,           -- JSON array of strings (2-6 elements)
    correct_answer_index  INTEGER NOT NULL CHECK (correct_answer_index >= 0),
    difficulty            INTEGER NOT NULL CHECK (difficulty >= 1 AND difficulty <= 5),
    discrimination        REAL    NOT NULL CHECK (discrimination >= 0.00 AND discrimination <= 1.00),
    status                TEXT    NOT NULL DEFAULT 'Active', -- 'Draft'|'Active'|'Archived'|'Deleted'
    external_id           TEXT,                       -- optional import duplicate key
    created_at            TEXT    NOT NULL,
    updated_at            TEXT    NOT NULL,
    created_by_user_id    TEXT    NOT NULL REFERENCES users(id),
    updated_by_user_id    TEXT    NOT NULL REFERENCES users(id)
);

CREATE INDEX idx_questions_status       ON questions (status);
CREATE INDEX idx_questions_difficulty   ON questions (difficulty);
CREATE INDEX idx_questions_external_id  ON questions (external_id) WHERE external_id IS NOT NULL;

-- Question ↔ KnowledgePoint (many-to-many)
CREATE TABLE question_kp_mappings (
    question_id         TEXT NOT NULL REFERENCES questions(id) ON DELETE CASCADE,
    knowledge_point_id  TEXT NOT NULL REFERENCES knowledge_points(id) ON DELETE RESTRICT,
    mapped_at           TEXT NOT NULL,
    mapped_by_user_id   TEXT NOT NULL REFERENCES users(id),
    PRIMARY KEY (question_id, knowledge_point_id)
);

CREATE INDEX idx_qkp_kp ON question_kp_mappings (knowledge_point_id);

-- Question ↔ Tag (many-to-many)
CREATE TABLE question_tag_mappings (
    question_id        TEXT NOT NULL REFERENCES questions(id) ON DELETE CASCADE,
    tag_id             TEXT NOT NULL REFERENCES tags(id) ON DELETE RESTRICT,
    applied_at         TEXT NOT NULL,
    applied_by_user_id TEXT NOT NULL REFERENCES users(id),
    PRIMARY KEY (question_id, tag_id)
);

CREATE INDEX idx_qtag_tag ON question_tag_mappings (tag_id);
