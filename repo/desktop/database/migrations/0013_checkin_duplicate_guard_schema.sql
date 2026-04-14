-- Migration: 0013_checkin_duplicate_guard_schema.sql
-- Domain: Deterministic duplicate suppression guard for successful check-ins.
-- Invariants enforced:
--   - One guard row per (member_id, session_id).
--   - last_success_at is atomically updated only when the duplicate window has elapsed.

CREATE TABLE checkin_duplicate_guards (
    member_id        TEXT NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    session_id       TEXT NOT NULL,
    last_success_at  TEXT NOT NULL,
    PRIMARY KEY (member_id, session_id)
);

CREATE INDEX idx_checkin_duplicate_guards_last_success
    ON checkin_duplicate_guards (last_success_at);
