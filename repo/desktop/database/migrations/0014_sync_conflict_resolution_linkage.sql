-- Migration: 0014_sync_conflict_resolution_linkage.sql
-- Domain: Persist explicit linkage between sync conflict resolution and
-- downstream compensating actions.

ALTER TABLE conflict_records
    ADD COLUMN resolution_action_type TEXT;

ALTER TABLE conflict_records
    ADD COLUMN resolution_action_id TEXT;

CREATE INDEX idx_conflicts_resolution_action
    ON conflict_records (resolution_action_id);
