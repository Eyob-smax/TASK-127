-- Migration: 0002_identity_schema.sql
-- Domain: Local users, credentials, sessions, lockout, CAPTCHA, step-up verification.
-- Invariants enforced:
--   - username is unique (case-insensitive enforcement at service layer)
--   - one credential record per user (ON CONFLICT REPLACE on upsert)
--   - one lockout record per username
--   - one CAPTCHA state per username
--   - step-up windows are single-use and time-bounded

-- Users
CREATE TABLE users (
    id               TEXT    PRIMARY KEY,         -- UUID
    username         TEXT    NOT NULL UNIQUE,
    role             TEXT    NOT NULL,             -- 'FRONT_DESK_OPERATOR' | 'PROCTOR' | 'CONTENT_MANAGER' | 'SECURITY_ADMINISTRATOR'
    status           TEXT    NOT NULL DEFAULT 'Active', -- 'Active' | 'Locked' | 'Deactivated'
    created_at       TEXT    NOT NULL,             -- ISO-8601 UTC
    updated_at       TEXT    NOT NULL,
    created_by_user_id TEXT                        -- NULL for bootstrap admin
);

CREATE INDEX idx_users_username    ON users (username);
CREATE INDEX idx_users_status      ON users (status);

-- Credentials (Argon2id hash records)
CREATE TABLE credentials (
    user_id       TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    algorithm     TEXT NOT NULL DEFAULT 'argon2id',
    time_cost     INTEGER NOT NULL,
    memory_cost   INTEGER NOT NULL,
    parallelism   INTEGER NOT NULL,
    tag_length    INTEGER NOT NULL,
    salt_hex      TEXT NOT NULL,    -- 16-byte random salt, hex-encoded
    hash_hex      TEXT NOT NULL,    -- Argon2id output, hex-encoded
    updated_at    TEXT NOT NULL
);

-- User sessions
CREATE TABLE user_sessions (
    token           TEXT PRIMARY KEY,              -- UUID; the session identifier
    user_id         TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at      TEXT NOT NULL,
    last_active_at  TEXT NOT NULL,
    active          INTEGER NOT NULL DEFAULT 1     -- 0 = logged out or locked
);

CREATE INDEX idx_sessions_user_id ON user_sessions (user_id);
CREATE INDEX idx_sessions_active  ON user_sessions (active);

-- Lockout records (one per username; tracks failure window)
CREATE TABLE lockout_records (
    username         TEXT PRIMARY KEY,
    failed_attempts  INTEGER NOT NULL DEFAULT 0,
    first_fail_at    TEXT,          -- NULL if no failures in current window
    locked_at        TEXT           -- NULL if not locked
    -- Service invariant: locked if failed_attempts >= 5 within 600s of first_fail_at
    -- Service invariant: CAPTCHA required if failed_attempts >= 3
);

-- CAPTCHA states (one per username; refreshed on each new challenge)
CREATE TABLE captcha_states (
    username         TEXT PRIMARY KEY,
    challenge_id     TEXT NOT NULL,     -- UUID for this challenge
    answer_hash_hex  TEXT NOT NULL,     -- SHA-256 of correct answer (lowercase trimmed)
    issued_at        TEXT NOT NULL,
    expires_at       TEXT NOT NULL,     -- issued_at + 900s (15 minutes)
    solve_attempts   INTEGER NOT NULL DEFAULT 0,
    solved           INTEGER NOT NULL DEFAULT 0  -- 0 = unsolved
);

-- Step-up verification windows (one-time, 2-minute windows)
CREATE TABLE step_up_windows (
    id              TEXT PRIMARY KEY,   -- UUID
    user_id         TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    session_token   TEXT NOT NULL REFERENCES user_sessions(token) ON DELETE CASCADE,
    granted_at      TEXT NOT NULL,
    expires_at      TEXT NOT NULL,      -- granted_at + 120s
    consumed        INTEGER NOT NULL DEFAULT 0  -- 0 = not yet consumed
);

CREATE INDEX idx_stepup_user_id ON step_up_windows (user_id);
