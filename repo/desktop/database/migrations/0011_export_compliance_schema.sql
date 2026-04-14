-- Migration: 0011_export_compliance_schema.sql
-- Domain: Redaction rules and watermark tracking for compliance workflows.
-- Note: Export/deletion request tables were placed in 0008 (audit_schema)
-- because they are authored alongside audit records.
-- This migration adds supplemental compliance tables.

-- Redaction rules (define which fields are PII-sensitive for a given entity type)
-- Used by ExportService and AuditService to determine which payload fields to encrypt.
CREATE TABLE redaction_rules (
    id            TEXT    PRIMARY KEY,
    entity_type   TEXT    NOT NULL,       -- e.g. "Member", "CheckInAttempt"
    field_name    TEXT    NOT NULL,       -- e.g. "mobile", "name", "barcode"
    redaction_type TEXT   NOT NULL,       -- 'ENCRYPT_STORE' | 'MASK_DISPLAY' | 'EXCLUDE_EXPORT'
    created_at    TEXT    NOT NULL,
    CONSTRAINT uq_redaction UNIQUE (entity_type, field_name)
);

-- Seed default redaction rules
INSERT INTO redaction_rules (id, entity_type, field_name, redaction_type, created_at) VALUES
    ('rr-1', 'Member', 'mobile',   'ENCRYPT_STORE', datetime('now')),
    ('rr-2', 'Member', 'name',     'ENCRYPT_STORE', datetime('now')),
    ('rr-3', 'Member', 'barcode',  'ENCRYPT_STORE', datetime('now')),
    ('rr-4', 'AuditEntry', 'before_payload_json', 'ENCRYPT_STORE', datetime('now')),
    ('rr-5', 'AuditEntry', 'after_payload_json',  'ENCRYPT_STORE', datetime('now'));

-- Sync watermarks (per-desk, per-entity-type; tracks the last exported timestamp)
-- Used by SyncPackageService to determine the delta for the next export.
CREATE TABLE sync_watermarks (
    desk_id       TEXT NOT NULL,
    entity_type   TEXT NOT NULL,
    watermark     TEXT NOT NULL,    -- ISO-8601 UTC; last exported-at for this type
    updated_at    TEXT NOT NULL,
    PRIMARY KEY (desk_id, entity_type)
);
