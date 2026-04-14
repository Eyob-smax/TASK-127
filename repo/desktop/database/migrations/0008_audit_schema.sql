-- Migration: 0008_audit_schema.sql
-- Domain: Tamper-evident audit chain, export requests, and deletion requests.
-- Invariants enforced:
--   - audit_entries has NO UPDATE or DELETE operations at application layer.
--     (Enforced by IAuditRepository — insert-only interface.)
--   - audit_chain_head maintains the rolling hash for new entry computation.
--   - entry_hash includes previous_entry_hash in canonical form.
--   - export/deletion requests use status state machines.

-- Audit entries (append-only; no update or delete)
-- PII fields within before/after payloads are AES-256-GCM encrypted before storage.
CREATE TABLE audit_entries (
    id                    TEXT PRIMARY KEY,   -- UUID
    timestamp             TEXT NOT NULL,      -- ISO-8601 UTC; millisecond precision
    actor_user_id         TEXT NOT NULL,      -- may reference deactivated user (no FK)
    event_type            TEXT NOT NULL,      -- AuditEventType string
    entity_type           TEXT NOT NULL,
    entity_id             TEXT NOT NULL,
    before_payload_json   TEXT NOT NULL DEFAULT '{}',  -- encrypted PII where applicable
    after_payload_json    TEXT NOT NULL DEFAULT '{}',
    previous_entry_hash   TEXT NOT NULL,      -- hex SHA-256; empty string for genesis entry
    entry_hash            TEXT NOT NULL       -- hex SHA-256 of this entry's canonical form
);

CREATE INDEX idx_audit_timestamp   ON audit_entries (timestamp);
CREATE INDEX idx_audit_actor       ON audit_entries (actor_user_id);
CREATE INDEX idx_audit_event_type  ON audit_entries (event_type);
CREATE INDEX idx_audit_entity      ON audit_entries (entity_type, entity_id);
-- For retention cleanup (entries older than 3 years by default):
CREATE INDEX idx_audit_retention   ON audit_entries (timestamp);

-- Audit chain head (single row; updated atomically on each new audit entry)
-- Stores the hash of the most recently written entry for O(1) chain head lookup.
CREATE TABLE audit_chain_head (
    id              INTEGER PRIMARY KEY CHECK (id = 1),  -- enforces single-row
    last_entry_id   TEXT,           -- UUID of latest audit entry; NULL if chain empty
    last_entry_hash TEXT NOT NULL DEFAULT '' -- hex SHA-256; '' for empty chain
);

INSERT INTO audit_chain_head (id, last_entry_id, last_entry_hash) VALUES (1, NULL, '');

-- Data subject export requests (GDPR / MLPS access requests)
CREATE TABLE export_requests (
    id                   TEXT PRIMARY KEY,
    member_id            TEXT NOT NULL REFERENCES members(id),
    requester_user_id    TEXT NOT NULL REFERENCES users(id),
    status               TEXT NOT NULL DEFAULT 'PENDING', -- 'PENDING'|'COMPLETED'|'REJECTED'
    rationale            TEXT NOT NULL,
    created_at           TEXT NOT NULL,
    fulfilled_at         TEXT,          -- NULL until completed
    output_file_path     TEXT           -- NULL until completed
);

CREATE INDEX idx_export_requests_member ON export_requests (member_id);
CREATE INDEX idx_export_requests_status ON export_requests (status);

-- Data subject deletion requests (GDPR / MLPS erasure)
CREATE TABLE deletion_requests (
    id                   TEXT PRIMARY KEY,
    member_id            TEXT NOT NULL REFERENCES members(id),
    requester_user_id    TEXT NOT NULL REFERENCES users(id),
    approver_user_id     TEXT REFERENCES users(id),      -- NULL until approved
    status               TEXT NOT NULL DEFAULT 'PENDING',
                                                          -- 'PENDING'|'APPROVED'|'COMPLETED'|'REJECTED'
    rationale            TEXT NOT NULL,
    created_at           TEXT NOT NULL,
    approved_at          TEXT,          -- NULL until approved
    completed_at         TEXT,          -- NULL until completed
    fields_anonymized    TEXT           -- JSON array of anonymized field names
);

CREATE INDEX idx_deletion_requests_member ON deletion_requests (member_id);
CREATE INDEX idx_deletion_requests_status ON deletion_requests (status);
