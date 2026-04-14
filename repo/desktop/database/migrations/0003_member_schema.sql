-- Migration: 0003_member_schema.sql
-- Domain: Members, term cards, punch cards, and freeze records.
-- Invariants enforced:
--   - member_id stores AES-256-GCM ciphertext (never plaintext)
--   - member_id_hash supports deterministic lookup without decrypting full table
--   - term_end > term_start (CHECK constraint)
--   - punch_card current_balance >= 0 (CHECK constraint)
--   - Only one active freeze record per member at any time

-- Members
-- Sensitive fields (barcode, mobile, name) stored as AES-256-GCM ciphertext.
-- member_id stores AES-256-GCM ciphertext; member_id_hash stores SHA-256 of canonical member ID.
CREATE TABLE members (
    id                   TEXT    PRIMARY KEY,       -- UUID
    member_id            TEXT    NOT NULL,          -- AES-GCM ciphertext
    member_id_hash       TEXT,                      -- SHA-256 hex lookup index
    barcode_encrypted    TEXT    NOT NULL,           -- AES-GCM ciphertext
    mobile_encrypted     TEXT    NOT NULL,           -- AES-GCM ciphertext
    name_encrypted       TEXT    NOT NULL,           -- AES-GCM ciphertext
    deleted              INTEGER NOT NULL DEFAULT 0, -- 1 = soft-deleted
    created_at           TEXT    NOT NULL,
    updated_at           TEXT    NOT NULL
);

CREATE INDEX idx_members_member_id_hash ON members (member_id_hash);
CREATE UNIQUE INDEX idx_members_member_id_hash_unique
    ON members (member_id_hash)
    WHERE member_id_hash IS NOT NULL AND member_id_hash != '';
CREATE INDEX idx_members_deleted   ON members (deleted);

-- Term cards (time-bounded entitlement windows)
CREATE TABLE term_cards (
    id          TEXT    PRIMARY KEY,
    member_id   TEXT    NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    term_start  TEXT    NOT NULL,       -- ISO-8601 date (YYYY-MM-DD)
    term_end    TEXT    NOT NULL,       -- ISO-8601 date; MUST be > term_start
    active      INTEGER NOT NULL DEFAULT 1,
    created_at  TEXT    NOT NULL,
    CONSTRAINT chk_term_dates CHECK (term_end > term_start)
);

CREATE INDEX idx_term_cards_member   ON term_cards (member_id);
CREATE INDEX idx_term_cards_active   ON term_cards (member_id, active, term_start, term_end);

-- Punch cards (consumable session entitlements)
CREATE TABLE punch_cards (
    id               TEXT    PRIMARY KEY,
    member_id        TEXT    NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    product_code     TEXT    NOT NULL,
    initial_balance  INTEGER NOT NULL CHECK (initial_balance >= 0),
    current_balance  INTEGER NOT NULL CHECK (current_balance >= 0),
    created_at       TEXT    NOT NULL,
    updated_at       TEXT    NOT NULL
    -- Invariant: current_balance <= initial_balance
    -- Invariant: current_balance >= 0 (enforced by CHECK and atomic deduction)
);

CREATE INDEX idx_punch_cards_member ON punch_cards (member_id);

-- Member freeze records (active freeze = thawed_at IS NULL)
CREATE TABLE member_freeze_records (
    id                  TEXT    PRIMARY KEY,
    member_id           TEXT    NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    reason              TEXT    NOT NULL,
    frozen_by_user_id   TEXT    NOT NULL REFERENCES users(id),
    frozen_at           TEXT    NOT NULL,
    thawed_by_user_id   TEXT,           -- NULL while frozen
    thawed_at           TEXT            -- NULL while frozen
);

CREATE INDEX idx_freeze_member     ON member_freeze_records (member_id);
CREATE INDEX idx_freeze_active     ON member_freeze_records (member_id, thawed_at);
-- Active freeze: WHERE member_id = ? AND thawed_at IS NULL
