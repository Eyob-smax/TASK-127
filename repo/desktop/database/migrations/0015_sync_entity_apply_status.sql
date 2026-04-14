-- Migration: 0015_sync_entity_apply_status.sql
-- Domain: Track per-entity import materialization status for sync packages.

ALTER TABLE sync_package_entities
    ADD COLUMN applied INTEGER NOT NULL DEFAULT 0;

ALTER TABLE sync_package_entities
    ADD COLUMN applied_at TEXT;

CREATE INDEX idx_sync_entities_apply_status
    ON sync_package_entities (package_id, applied);
