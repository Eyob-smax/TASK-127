#pragma once
// Result.h — ProctorOps
// A lightweight sum type for service and repository return values.
// All service and repository operations return Result<T> instead of throwing exceptions.
// Q_ASSERT preconditions guard misuse; callers must check isOk() before accessing value().

#include <QString>
#include <variant>
#include <cassert>

// ── Error codes ──────────────────────────────────────────────────────────────
enum class ErrorCode {
    // General
    NotFound,
    AlreadyExists,
    ValidationFailed,
    InternalError,
    DbError,

    // Auth
    AuthorizationDenied,
    StepUpRequired,
    AccountLocked,
    CaptchaRequired,
    InvalidCredentials,

    // Check-in
    DuplicateCheckIn,
    TermCardExpired,
    TermCardMissing,
    AccountFrozen,
    PunchCardExhausted,

    // Crypto / packaging
    SignatureInvalid,
    TrustStoreMiss,
    PackageCorrupt,
    EncryptionFailed,
    DecryptionFailed,
    KeyNotFound,

    // Sync
    ConflictUnresolved,

    // Ingestion
    JobDependencyUnmet,
    CheckpointCorrupt,
    IoError,
    InvalidState,

    // Audit
    ChainIntegrityFailed,
};

// ── Result<T> — general template ─────────────────────────────────────────────
template <typename T>
class Result {
public:
    static Result ok(T value) {
        Result r;
        r.m_data.template emplace<0>(std::move(value));
        return r;
    }

    static Result err(ErrorCode code, QString message = {}) {
        Result r;
        r.m_data.template emplace<1>(ErrData{code, std::move(message)});
        return r;
    }

    [[nodiscard]] bool isOk()  const noexcept { return m_data.index() == 0; }
    [[nodiscard]] bool isErr() const noexcept { return m_data.index() == 1; }

    [[nodiscard]] const T& value() const & {
        assert(isOk() && "Result::value() called on error result");
        return std::get<0>(m_data);
    }
    [[nodiscard]] T& value() & {
        assert(isOk() && "Result::value() called on error result");
        return std::get<0>(m_data);
    }
    [[nodiscard]] T value() && {
        assert(isOk() && "Result::value() called on error result");
        return std::move(std::get<0>(m_data));
    }

    [[nodiscard]] ErrorCode errorCode() const {
        assert(isErr() && "Result::errorCode() called on ok result");
        return std::get<1>(m_data).code;
    }
    [[nodiscard]] const QString& errorMessage() const {
        assert(isErr() && "Result::errorMessage() called on ok result");
        return std::get<1>(m_data).message;
    }

private:
    struct ErrData { ErrorCode code; QString message; };
    std::variant<T, ErrData> m_data;
    Result() = default;
};

// ── Result<void> — specialization for operations with no return value ─────────
template <>
class Result<void> {
public:
    static Result ok() {
        Result r;
        r.m_ok = true;
        return r;
    }

    static Result err(ErrorCode code, QString message = {}) {
        Result r;
        r.m_ok = false;
        r.m_code = code;
        r.m_message = std::move(message);
        return r;
    }

    [[nodiscard]] bool isOk()  const noexcept { return m_ok; }
    [[nodiscard]] bool isErr() const noexcept { return !m_ok; }

    [[nodiscard]] ErrorCode errorCode() const {
        assert(isErr() && "Result<void>::errorCode() called on ok result");
        return m_code;
    }
    [[nodiscard]] const QString& errorMessage() const {
        assert(isErr() && "Result<void>::errorMessage() called on ok result");
        return m_message;
    }

private:
    bool      m_ok{false};
    ErrorCode m_code{ErrorCode::InternalError};
    QString   m_message;
    Result() = default;
};
