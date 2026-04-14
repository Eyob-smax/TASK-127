-- Migration: 0010_update_schema.sql
-- Domain: Offline update packages, staged components, install history, and rollback records.
-- Override: delivery does not require a signed .msi artifact.
-- The update domain model and all logic remain fully in scope. See docs/design.md §2.
-- Invariants:
--   - Each update_package has a unique id (from manifest)
--   - Install history records the pre-update snapshot for rollback
--   - Rollback records reference a specific install_history entry

-- Update packages (.proctorpkg bundle metadata)
CREATE TABLE update_packages (
    id                   TEXT PRIMARY KEY,  -- package_id from update-manifest.json
    version              TEXT NOT NULL,
    target_platform      TEXT NOT NULL,
    description          TEXT NOT NULL,
    signer_key_id        TEXT NOT NULL REFERENCES trusted_signing_keys(id),
    signature_valid      INTEGER NOT NULL DEFAULT 0,  -- 1 = verified
    staged_path          TEXT,              -- temporary staging directory path
    status               TEXT NOT NULL DEFAULT 'Staged',
                                            -- 'Staged'|'Applied'|'RolledBack'|'Rejected'|'Cancelled'
    imported_at          TEXT NOT NULL,
    imported_by_user_id  TEXT NOT NULL REFERENCES users(id)
);

-- Update package components (one row per binary/data file in the package)
CREATE TABLE update_components (
    package_id            TEXT NOT NULL REFERENCES update_packages(id) ON DELETE CASCADE,
    name                  TEXT NOT NULL,    -- e.g. "proctorops.exe"
    version               TEXT NOT NULL,
    sha256_hex            TEXT NOT NULL,    -- expected digest from manifest
    component_file_path   TEXT NOT NULL,    -- path within staged directory
    PRIMARY KEY (package_id, name)
);

-- Install history (snapshot taken before each update for rollback support)
CREATE TABLE install_history (
    id                     TEXT PRIMARY KEY,
    package_id             TEXT NOT NULL REFERENCES update_packages(id),
    from_version           TEXT NOT NULL,   -- version before this update
    to_version             TEXT NOT NULL,   -- version brought by this update
    applied_at             TEXT NOT NULL,
    applied_by_user_id     TEXT NOT NULL REFERENCES users(id),
    snapshot_payload_json  TEXT NOT NULL    -- serialized component state for rollback
);

CREATE INDEX idx_install_history_applied ON install_history (applied_at DESC);

-- Rollback records (written when an operator rolls back to a prior install)
CREATE TABLE rollback_records (
    id                   TEXT PRIMARY KEY,
    install_history_id   TEXT NOT NULL REFERENCES install_history(id),
    from_version         TEXT NOT NULL,     -- version at time of rollback
    to_version           TEXT NOT NULL,     -- version restored
    rationale            TEXT NOT NULL,
    rolled_back_at       TEXT NOT NULL,
    rolled_back_by_user_id TEXT NOT NULL REFERENCES users(id)
);

CREATE INDEX idx_rollbacks_history ON rollback_records (install_history_id);
