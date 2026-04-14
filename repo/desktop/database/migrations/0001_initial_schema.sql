-- Migration: 0001_initial_schema.sql
-- Applied by: Migration runner at startup (see src/utils/Migration.h)
-- Scope: schema_migrations bootstrap table only.
--
-- The full domain schema is defined in migrations 0002–0011.
-- This migration establishes the migration tracking infrastructure.
--
-- Applied inside a single SQLite transaction by the Migration runner.
-- On failure, the transaction is rolled back and the app refuses to start.

-- Migration tracking table
CREATE TABLE IF NOT EXISTS schema_migrations (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    migration   TEXT    NOT NULL UNIQUE,    -- e.g. '0001_initial_schema'
    applied_at  TEXT    NOT NULL            -- ISO-8601 UTC timestamp
);

-- Application startup/shutdown tracking (supports crash recovery)
-- See docs/design.md §14 for crash recovery logic.
CREATE TABLE IF NOT EXISTS app_lifecycle (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    started_at          TEXT    NOT NULL,   -- ISO-8601 UTC
    clean_shutdown_at   TEXT,               -- NULL if crash-detected on next start
    app_version         TEXT    NOT NULL
);
