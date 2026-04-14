-- Migration: 0006_ingestion_schema.sql
-- Domain: Ingestion job queue, dependency graph, checkpoints, and worker claims.
-- Invariants enforced:
--   - priority in [1, 10] via CHECK
--   - retry_count >= 0 via CHECK
--   - Only one active worker claim per job (UNIQUE on job_id in worker_claims)
--   - Circular dependency prevention is enforced at service layer (not DB)

-- Ingestion jobs
CREATE TABLE ingestion_jobs (
    id                 TEXT    PRIMARY KEY,
    type               TEXT    NOT NULL,        -- 'QuestionImport' | 'RosterImport'
    status             TEXT    NOT NULL DEFAULT 'Pending',
                                                -- 'Pending'|'Claimed'|'Validating'|'Importing'
                                                -- |'Indexing'|'Completed'|'Failed'
                                                -- |'Interrupted'|'Cancelled'
    priority           INTEGER NOT NULL DEFAULT 5 CHECK (priority >= 1 AND priority <= 10),
    source_file_path   TEXT    NOT NULL,
    scheduled_at       TEXT,                    -- NULL = run immediately when worker free
    created_at         TEXT    NOT NULL,
    started_at         TEXT,                    -- NULL until first claim
    completed_at       TEXT,
    failed_at          TEXT,
    retry_count        INTEGER NOT NULL DEFAULT 0 CHECK (retry_count >= 0),
    last_error         TEXT,
    current_phase      TEXT    NOT NULL DEFAULT 'Validate', -- 'Validate'|'Import'|'Index'
    created_by_user_id TEXT    NOT NULL REFERENCES users(id)
);

CREATE INDEX idx_jobs_status    ON ingestion_jobs (status);
CREATE INDEX idx_jobs_priority  ON ingestion_jobs (status, priority DESC, created_at);
CREATE INDEX idx_jobs_scheduled ON ingestion_jobs (scheduled_at) WHERE scheduled_at IS NOT NULL;

-- Job dependencies (DAG edges: job_id must not start until depends_on_job_id = Completed)
CREATE TABLE job_dependencies (
    job_id             TEXT NOT NULL REFERENCES ingestion_jobs(id) ON DELETE CASCADE,
    depends_on_job_id  TEXT NOT NULL REFERENCES ingestion_jobs(id) ON DELETE RESTRICT,
    PRIMARY KEY (job_id, depends_on_job_id)
);

CREATE INDEX idx_deps_on ON job_dependencies (depends_on_job_id);

-- Job checkpoints (one per job+phase combination; upserted after each committed batch)
CREATE TABLE job_checkpoints (
    job_id             TEXT NOT NULL REFERENCES ingestion_jobs(id) ON DELETE CASCADE,
    phase              TEXT NOT NULL,           -- 'Validate' | 'Import' | 'Index'
    offset_bytes       INTEGER NOT NULL DEFAULT 0,
    records_processed  INTEGER NOT NULL DEFAULT 0,
    saved_at           TEXT    NOT NULL,
    PRIMARY KEY (job_id, phase)
);

-- Worker claims (one active claim per job at any time)
CREATE TABLE worker_claims (
    job_id       TEXT PRIMARY KEY REFERENCES ingestion_jobs(id) ON DELETE CASCADE,
    worker_id    TEXT NOT NULL,                -- UUID per WorkerPool thread per run
    claimed_at   TEXT NOT NULL
    -- UNIQUE on job_id (PRIMARY KEY) enforces single-claim invariant
);
