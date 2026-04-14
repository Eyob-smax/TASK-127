-- Migration: 0004_checkin_schema.sql
-- Domain: Check-in attempts, deduction events, and correction workflows.
-- Invariants enforced:
--   - Indexed lookup on (member_id, session_id, attempted_at, status) supports
--     30-second duplicate suppression queries.
--   - balance_after = balance_before - sessions_deducted (CHECK).
--   - Deduction rows are append-only; corrections write compensating records.

-- Check-in attempts (immutable; one row per attempt)
CREATE TABLE checkin_attempts (
    id                   TEXT    PRIMARY KEY,
    member_id            TEXT    NOT NULL REFERENCES members(id),
    session_id           TEXT    NOT NULL,          -- external session/roster identifier
    operator_user_id     TEXT    NOT NULL REFERENCES users(id),
    status               TEXT    NOT NULL,          -- 'Success' | 'DuplicateBlocked' | ...
    attempted_at         TEXT    NOT NULL,           -- ISO-8601 UTC
    deduction_event_id   TEXT,                      -- NULL on non-success
    failure_reason       TEXT
);

-- Index for 30-second duplicate window query:
--   SELECT * FROM checkin_attempts
--   WHERE member_id = ? AND session_id = ? AND status = 'Success'
--     AND attempted_at >= datetime(?, '-30 seconds')
CREATE INDEX idx_checkin_dedup ON checkin_attempts (member_id, session_id, attempted_at, status);
CREATE INDEX idx_checkin_member ON checkin_attempts (member_id, attempted_at);

-- Deduction events (immutable; compensating correction records added separately)
CREATE TABLE deduction_events (
    id                         TEXT    PRIMARY KEY,
    member_id                  TEXT    NOT NULL REFERENCES members(id),
    punch_card_id              TEXT    NOT NULL REFERENCES punch_cards(id),
    checkin_attempt_id         TEXT    NOT NULL REFERENCES checkin_attempts(id),
    sessions_deducted          INTEGER NOT NULL DEFAULT 1 CHECK (sessions_deducted > 0),
    balance_before             INTEGER NOT NULL CHECK (balance_before >= 0),
    balance_after              INTEGER NOT NULL CHECK (balance_after >= 0),
    deducted_at                TEXT    NOT NULL,
    reversed_by_correction_id  TEXT,               -- NULL until reversed
    CONSTRAINT chk_balance CHECK (balance_after = balance_before - sessions_deducted)
);

CREATE INDEX idx_deductions_member      ON deduction_events (member_id);
CREATE INDEX idx_deductions_punch_card  ON deduction_events (punch_card_id);

-- Correction requests (stateful; submitted by proctor or admin)
CREATE TABLE correction_requests (
    id                    TEXT    PRIMARY KEY,
    deduction_event_id    TEXT    NOT NULL REFERENCES deduction_events(id),
    requested_by_user_id  TEXT    NOT NULL REFERENCES users(id),
    rationale             TEXT    NOT NULL,
    status                TEXT    NOT NULL DEFAULT 'Pending', -- 'Pending'|'Approved'|'Applied'|'Rejected'
    created_at            TEXT    NOT NULL
);

CREATE INDEX idx_corrections_status ON correction_requests (status);
CREATE INDEX idx_corrections_event  ON correction_requests (deduction_event_id);

-- Correction approvals (written by security admin with step-up token)
CREATE TABLE correction_approvals (
    correction_request_id  TEXT    PRIMARY KEY REFERENCES correction_requests(id),
    approved_by_user_id    TEXT    NOT NULL REFERENCES users(id),
    step_up_window_id      TEXT    NOT NULL REFERENCES step_up_windows(id),
    rationale              TEXT    NOT NULL,
    approved_at            TEXT    NOT NULL,
    before_payload_json    TEXT    NOT NULL,    -- serialized DeductionEvent state
    after_payload_json     TEXT    NOT NULL     -- serialized PunchCard balance after reversal
);
