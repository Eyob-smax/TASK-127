-- Migration: 0012_workspace_schema.sql
-- Applied by: Migration runner at startup (see src/utils/Migration.h)
-- Scope: workspace state persistence for crash-recovery and window restoration,
--        and performance observation log for cold-start and memory instrumentation.

-- Workspace state singleton: records open windows, pending action markers,
-- and interrupted ingestion job IDs so next launch can restore operational state.
-- id is forced to 1 (CHECK constraint) — only one row ever exists.
CREATE TABLE IF NOT EXISTS workspace_state (
    id                  INTEGER PRIMARY KEY CHECK (id = 1),
    open_windows        TEXT    NOT NULL DEFAULT '[]',   -- JSON array of window identifiers
    pending_actions     TEXT    NOT NULL DEFAULT '[]',   -- JSON array of pending action markers
    interrupted_job_ids TEXT    NOT NULL DEFAULT '[]',   -- JSON array of ingestion job IDs
    updated_at          TEXT    NOT NULL                 -- ISO-8601 UTC
);

-- Performance observation log: cold-start timing and periodic memory samples.
-- Results are written here by PerformanceObserver for manual verification against
-- the < 3 s cold-start target and 7-day / 200 MB memory growth target.
-- Final numeric verification requires a manual check on a representative office PC.
CREATE TABLE IF NOT EXISTS performance_log (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    event_type  TEXT    NOT NULL,   -- 'cold_start' | 'memory_sample' | 'shutdown'
    value_ms    INTEGER,            -- elapsed ms (cold_start only; NULL otherwise)
    value_bytes INTEGER,            -- RSS bytes (memory_sample only; NULL otherwise)
    recorded_at TEXT    NOT NULL    -- ISO-8601 UTC
);
