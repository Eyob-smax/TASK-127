-- Migration: 0007_sync_schema.sql
-- Domain: Sync packages, entity manifests, conflict records, and trusted signing keys.
-- Invariants enforced:
--   - Each package has a unique id (from manifest)
--   - Conflict records link to a specific package
--   - Trusted keys are identified by fingerprint (unique)
--   - Revoked keys are rejected at signature verification time

-- Sync packages (.proctorsync bundle metadata)
CREATE TABLE sync_packages (
    id                   TEXT PRIMARY KEY,  -- package_id from manifest.json
    source_desk_id       TEXT NOT NULL,
    signer_key_id        TEXT NOT NULL,
    exported_at          TEXT NOT NULL,     -- ISO-8601 UTC from manifest
    since_watermark      TEXT NOT NULL,     -- entity delta start timestamp
    status               TEXT NOT NULL DEFAULT 'Pending',
                                            -- 'Pending'|'Verified'|'Applied'|'Partial'|'Rejected'
    package_file_path    TEXT NOT NULL,
    imported_at          TEXT,              -- NULL until applied
    imported_by_user_id  TEXT REFERENCES users(id)
);

CREATE INDEX idx_sync_status ON sync_packages (status);

-- Sync package entity manifests (one row per entity file in the package)
CREATE TABLE sync_package_entities (
    package_id    TEXT NOT NULL REFERENCES sync_packages(id) ON DELETE CASCADE,
    entity_type   TEXT NOT NULL,  -- 'checkins'|'deductions'|'corrections'|'content_edits'
    file_path     TEXT NOT NULL,
    sha256_hex    TEXT NOT NULL,
    record_count  INTEGER NOT NULL,
    verified      INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (package_id, entity_type)
);

-- Conflict records (one per conflicting entity detected during import)
CREATE TABLE conflict_records (
    id                     TEXT PRIMARY KEY,
    package_id             TEXT NOT NULL REFERENCES sync_packages(id) ON DELETE CASCADE,
    type                   TEXT NOT NULL,  -- 'DoubleDeduction'|'MutableRecordConflict'|'DeleteConflict'
    entity_type            TEXT NOT NULL,
    entity_id              TEXT NOT NULL,
    description            TEXT NOT NULL,
    status                 TEXT NOT NULL DEFAULT 'Pending',
                                           -- 'Pending'|'ResolvedAcceptLocal'|'ResolvedAcceptIncoming'
                                           -- |'ResolvedManualMerge'|'Skipped'
    incoming_payload_json  TEXT NOT NULL,
    local_payload_json     TEXT NOT NULL,
    detected_at            TEXT NOT NULL,
    resolved_by_user_id    TEXT REFERENCES users(id),
    resolved_at            TEXT
);

CREATE INDEX idx_conflicts_package ON conflict_records (package_id, status);

-- Trusted signing keys (Ed25519 public keys for sync and update package verification)
CREATE TABLE trusted_signing_keys (
    id                   TEXT PRIMARY KEY,   -- UUID
    label                TEXT NOT NULL,
    public_key_der_hex   TEXT NOT NULL,      -- DER-encoded Ed25519 public key, hex
    fingerprint          TEXT NOT NULL UNIQUE, -- SHA-256 of DER bytes, hex
    imported_at          TEXT NOT NULL,
    imported_by_user_id  TEXT NOT NULL REFERENCES users(id),
    expires_at           TEXT,               -- NULL = no expiry
    revoked              INTEGER NOT NULL DEFAULT 0  -- 1 = key is revoked; reject packages
);

CREATE INDEX idx_keys_fingerprint ON trusted_signing_keys (fingerprint);
CREATE INDEX idx_keys_revoked     ON trusted_signing_keys (revoked);
