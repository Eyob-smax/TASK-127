#pragma once
// User.h — ProctorOps
// Domain models for identity, authentication, sessions, lockout, and step-up.

#include "CommonTypes.h"
#include <QString>
#include <QDateTime>

// ── User ─────────────────────────────────────────────────────────────────────
enum class UserStatus {
    Active,
    Locked,     // too many login failures
    Deactivated // admin-deactivated; cannot log in
};

struct User {
    QString      id;           // UUID
    QString      username;     // unique, case-insensitive lookup
    Role         role;
    UserStatus   status;
    QDateTime    createdAt;    // UTC
    QDateTime    updatedAt;    // UTC
    QString      createdByUserId; // null for bootstrap admin
};

// ── Credential ───────────────────────────────────────────────────────────────
// Argon2id hash record. Never stores plaintext or reversible forms.
struct Credential {
    QString  userId;
    QString  algorithm;    // always "argon2id"
    int      timeCost;     // default: Validation::Argon2TimeCost
    int      memoryCost;   // default: Validation::Argon2MemoryCost (KiB)
    int      parallelism;  // default: Validation::Argon2Parallelism
    int      tagLength;    // default: Validation::Argon2TagLength (bytes)
    QString  saltHex;      // Validation::Argon2SaltLength random bytes, hex-encoded
    QString  hashHex;      // Argon2id output, hex-encoded
    QDateTime updatedAt;
};

// ── LockoutRecord ────────────────────────────────────────────────────────────
// Tracks per-username failure counts for lockout and CAPTCHA policy.
// Reset on successful login or when the window expires.
struct LockoutRecord {
    QString   username;
    int       failedAttempts;    // incremented on each failure
    QDateTime firstFailAt;       // timestamp of first failure in current window
    QDateTime lockedAt;          // null (isNull) if not currently locked
    // Invariant: locked if failedAttempts >= Validation::LockoutFailureThreshold
    //            within Validation::LockoutWindowSeconds since firstFailAt
    // Invariant: CAPTCHA required if failedAttempts >= Validation::CaptchaAfterFailures
};

// ── CaptchaState ─────────────────────────────────────────────────────────────
// Locally rendered CAPTCHA challenge state per username.
struct CaptchaState {
    QString   username;
    QString   challengeId;     // UUID for this challenge
    QString   answerHashHex;   // SHA-256 of correct answer text (lowercase trimmed)
    QDateTime issuedAt;        // UTC
    QDateTime expiresAt;       // issuedAt + Validation::CaptchaCooldownSeconds
    int       solveAttempts;   // count of incorrect solve attempts for this challenge
    bool      solved;          // true if correctly solved; triggers challenge refresh
};

// ── UserSession ──────────────────────────────────────────────────────────────
// Desktop session token. Created on login, invalidated on logout or lock.
struct UserSession {
    QString   token;           // UUID — the session identifier
    QString   userId;
    QDateTime createdAt;       // UTC
    QDateTime lastActiveAt;    // UTC; updated on each authenticated action
    bool      active;
};

// ── StepUpWindow ─────────────────────────────────────────────────────────────
// Grants permission for one sensitive action within a 2-minute window.
// After use or expiry, the window is consumed and cannot be reused.
struct StepUpWindow {
    QString   id;              // UUID
    QString   userId;
    QString   sessionToken;
    QDateTime grantedAt;       // UTC
    QDateTime expiresAt;       // grantedAt + Validation::StepUpWindowSeconds
    bool      consumed;        // true after the authorized action completes
};
