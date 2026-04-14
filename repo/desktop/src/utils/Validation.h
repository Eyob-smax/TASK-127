#pragma once
// Validation.h — ProctorOps
// Canonical business invariants as constexpr constants and inline helpers.
// These values are authoritative for all services, validators, and tests.
// Do not hardcode any of these values elsewhere — always reference this header.

#include <QString>
#include <QRegularExpression>
#include <cstdint>

namespace Validation {

// ── Authentication ────────────────────────────────────────────────────────────

// Minimum password length (characters)
inline constexpr int PasswordMinLength = 12;

// Number of consecutive failures that trigger account lockout
inline constexpr int LockoutFailureThreshold = 5;

// Time window for counting failures toward lockout (seconds = 10 minutes)
inline constexpr int LockoutWindowSeconds = 600;

// Number of failures after which local CAPTCHA is required
inline constexpr int CaptchaAfterFailures = 3;

// Duration of CAPTCHA requirement / cool-down before CAPTCHA clears (seconds = 15 minutes)
inline constexpr int CaptchaCooldownSeconds = 900;

// Duration of a step-up verification window (seconds = 2 minutes)
inline constexpr int StepUpWindowSeconds = 120;

// ── Argon2id password hashing defaults ───────────────────────────────────────
inline constexpr int Argon2TimeCost    = 3;       // iterations
inline constexpr int Argon2MemoryCost  = 65536;   // KiB (64 MB)
inline constexpr int Argon2Parallelism = 4;
inline constexpr int Argon2TagLength   = 32;      // output bytes
inline constexpr int Argon2SaltLength  = 16;      // random salt bytes

// ── Check-in ─────────────────────────────────────────────────────────────────

// Same member + same session ID cannot be checked in within this window (seconds)
inline constexpr int DuplicateWindowSeconds = 30;

// ── Question governance ───────────────────────────────────────────────────────
inline constexpr int    DifficultyMin        = 1;
inline constexpr int    DifficultyMax        = 5;
inline constexpr double DiscriminationMin    = 0.00;
inline constexpr double DiscriminationMax    = 1.00;
inline constexpr int    QuestionBodyMaxChars = 4000;
inline constexpr int    AnswerOptionMinCount = 2;
inline constexpr int    AnswerOptionMaxCount = 6;
inline constexpr int    AnswerOptionMaxChars = 500;
inline constexpr int    TagNameMaxChars      = 64;

// ── Ingestion scheduler ───────────────────────────────────────────────────────
inline constexpr int SchedulerDefaultWorkers  = 2;
inline constexpr int SchedulerMinWorkers      = 1;
inline constexpr int SchedulerMaxWorkers      = 8;
inline constexpr int SchedulerMaxRetries      = 5;

// Exponential backoff delays (seconds): retry 0→1, 1→2, 2+ →3+
inline constexpr int RetryDelay1Seconds = 5;
inline constexpr int RetryDelay2Seconds = 30;
inline constexpr int RetryDelay3Seconds = 120; // 2 minutes

// ── Mobile number ─────────────────────────────────────────────────────────────
// Expected normalized format: (###) ###-####
inline constexpr const char* MobileRegex = R"(\(\d{3}\) \d{3}-\d{4})";

// ── Audit ─────────────────────────────────────────────────────────────────────
inline constexpr int AuditRetentionYears = 3;

// ── Crypto field encryption ───────────────────────────────────────────────────
inline constexpr int AesGcmKeyBytes   = 32;  // AES-256
inline constexpr int AesGcmNonceBytes = 12;  // 96-bit nonce
inline constexpr int AesGcmTagBytes   = 16;  // 128-bit authentication tag
inline constexpr int HkdfSaltBytes    = 16;  // per-record random salt for HKDF

// ── Helpers ───────────────────────────────────────────────────────────────────

/// Returns true if the password satisfies the minimum length policy.
/// Full strength validation (complexity) is enforced in AuthService.
[[nodiscard]] inline bool isPasswordLengthValid(const QString& password) noexcept {
    return password.length() >= PasswordMinLength;
}

/// Returns true if the mobile number matches the canonical (###) ###-#### format.
[[nodiscard]] inline bool isMobileValid(const QString& mobile) {
    static const QRegularExpression re{QLatin1String(MobileRegex)};
    return re.match(mobile).hasMatch();
}

/// Returns true if difficulty is in the valid range [1, 5].
[[nodiscard]] inline bool isDifficultyValid(int difficulty) noexcept {
    return difficulty >= DifficultyMin && difficulty <= DifficultyMax;
}

/// Returns true if discrimination is in the valid range [0.00, 1.00].
[[nodiscard]] inline bool isDiscriminationValid(double discrimination) noexcept {
    return discrimination >= DiscriminationMin && discrimination <= DiscriminationMax;
}

/// Returns the retry delay in seconds for the given retry count (0-indexed).
/// Follows the 5s / 30s / 2m exponential backoff schedule.
[[nodiscard]] inline int retryDelaySeconds(int retryCount) noexcept {
    if (retryCount <= 0) return RetryDelay1Seconds;
    if (retryCount == 1) return RetryDelay2Seconds;
    return RetryDelay3Seconds;
}

} // namespace Validation
