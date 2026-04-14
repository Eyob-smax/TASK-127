-- Migration: 0009_crypto_trust_schema.sql
-- Domain: Master key store and per-record encryption key metadata.
-- The trusted_signing_keys table was placed in 0007 (sync_schema) because it
-- serves both sync and update package verification.
-- This migration adds the master key store for AES-256-GCM field encryption.
-- Invariants:
--   - Exactly one active master key at any time (enforced at KeyStore service layer).
--   - Versioned ciphertext enables key rotation without schema changes.
--   - key_store_entries stores only key metadata; the master key material
--     is protected by OS-backed secret storage (DPAPI on Windows).

-- Key store entries (metadata; actual key bytes protected by OS-backed storage)
CREATE TABLE key_store_entries (
    id              TEXT    PRIMARY KEY,   -- UUID; used as key identifier in ciphertext headers
    label           TEXT    NOT NULL,      -- human-readable description
    algorithm       TEXT    NOT NULL DEFAULT 'AES-256-GCM',
    status          TEXT    NOT NULL DEFAULT 'Active', -- 'Active' | 'Retired' | 'PendingRotation'
    created_at      TEXT    NOT NULL,
    retired_at      TEXT,                  -- NULL while Active
    created_by_user_id TEXT NOT NULL REFERENCES users(id)
    -- The actual 32-byte AES-256 key material is stored outside SQLite in OS-backed
    -- storage (Windows DPAPI). See docs/questions.md §6 for the key custody design.
);

CREATE INDEX idx_keystore_status ON key_store_entries (status);

-- Key rotation jobs (track in-progress re-encryption batch jobs)
CREATE TABLE key_rotation_jobs (
    id                  TEXT    PRIMARY KEY,
    old_key_id          TEXT    NOT NULL REFERENCES key_store_entries(id),
    new_key_id          TEXT    NOT NULL REFERENCES key_store_entries(id),
    status              TEXT    NOT NULL DEFAULT 'Pending',
                                          -- 'Pending'|'InProgress'|'Completed'|'Failed'
    tables_remaining    TEXT    NOT NULL, -- JSON array of table names still to rotate
    records_rotated     INTEGER NOT NULL DEFAULT 0,
    started_at          TEXT,
    completed_at        TEXT,
    checkpoint_table    TEXT,             -- last table being processed (for resume)
    checkpoint_row_id   TEXT              -- last row_id processed (for resume)
);
