// ErrorFormatter.cpp — ProctorOps
// User-facing error message formatting.

#include "ErrorFormatter.h"

QString ErrorFormatter::toUserMessage(ErrorCode code, const QString& detail)
{
    QString base;
    switch (code) {
    // General
    case ErrorCode::NotFound:
        base = QStringLiteral("The requested item was not found.");
        break;
    case ErrorCode::AlreadyExists:
        base = QStringLiteral("An item with this identifier already exists.");
        break;
    case ErrorCode::ValidationFailed:
        base = QStringLiteral("Validation failed. Please check your input.");
        break;
    case ErrorCode::InternalError:
        base = QStringLiteral("An internal error occurred. Please try again.");
        break;
    case ErrorCode::DbError:
        base = QStringLiteral("A database error occurred. Please try again.");
        break;

    // Auth
    case ErrorCode::AuthorizationDenied:
        base = QStringLiteral("You do not have permission to perform this action.");
        break;
    case ErrorCode::StepUpRequired:
        base = QStringLiteral("This action requires re-authentication. Please enter your password.");
        break;
    case ErrorCode::AccountLocked:
        base = QStringLiteral("This account is locked due to too many failed login attempts. Please wait and try again.");
        break;
    case ErrorCode::CaptchaRequired:
        base = QStringLiteral("Please complete the security challenge to continue.");
        break;
    case ErrorCode::InvalidCredentials:
        base = QStringLiteral("Invalid username or password.");
        break;

    // Check-in
    case ErrorCode::DuplicateCheckIn:
        base = QStringLiteral("This member was already checked in within the last 30 seconds.");
        break;
    case ErrorCode::TermCardExpired:
        base = QStringLiteral("The member's term card has expired.");
        break;
    case ErrorCode::TermCardMissing:
        base = QStringLiteral("No active term card found for this member.");
        break;
    case ErrorCode::AccountFrozen:
        base = QStringLiteral("This member's account is frozen. Contact a security administrator.");
        break;
    case ErrorCode::PunchCardExhausted:
        base = QStringLiteral("No remaining sessions on the member's punch card.");
        break;

    // Crypto / packaging
    case ErrorCode::SignatureInvalid:
        base = QStringLiteral("The package signature is invalid. The package may be tampered with.");
        break;
    case ErrorCode::TrustStoreMiss:
        base = QStringLiteral("The signing key is not in the trust store, or has been revoked/expired.");
        break;
    case ErrorCode::PackageCorrupt:
        base = QStringLiteral("The package file is corrupt or incomplete.");
        break;
    case ErrorCode::EncryptionFailed:
        base = QStringLiteral("Data encryption failed.");
        break;
    case ErrorCode::DecryptionFailed:
        base = QStringLiteral("Data decryption failed. The data may be corrupt or the key may be wrong.");
        break;
    case ErrorCode::KeyNotFound:
        base = QStringLiteral("The encryption key was not found.");
        break;

    // Sync
    case ErrorCode::ConflictUnresolved:
        base = QStringLiteral("There are unresolved conflicts that must be addressed before proceeding.");
        break;

    // Ingestion
    case ErrorCode::JobDependencyUnmet:
        base = QStringLiteral("A required prerequisite job has not completed yet.");
        break;
    case ErrorCode::CheckpointCorrupt:
        base = QStringLiteral("The job checkpoint data is corrupt. The job may need to be restarted.");
        break;
    case ErrorCode::IoError:
        base = QStringLiteral("An input/output error occurred while reading or writing data.");
        break;
    case ErrorCode::InvalidState:
        base = QStringLiteral("The requested operation is not allowed in the current state.");
        break;

    // Audit
    case ErrorCode::ChainIntegrityFailed:
        base = QStringLiteral("Audit chain integrity check failed. The audit log may have been tampered with.");
        break;
    }

    if (!detail.isEmpty())
        return base + QStringLiteral(" ") + detail;
    return base;
}

QString ErrorFormatter::toFormHint(ErrorCode code)
{
    switch (code) {
    case ErrorCode::ValidationFailed:    return QStringLiteral("Invalid input");
    case ErrorCode::InvalidCredentials:  return QStringLiteral("Wrong username or password");
    case ErrorCode::AccountLocked:       return QStringLiteral("Account locked");
    case ErrorCode::CaptchaRequired:     return QStringLiteral("Security challenge required");
    case ErrorCode::StepUpRequired:      return QStringLiteral("Re-authentication required");
    case ErrorCode::AuthorizationDenied: return QStringLiteral("Insufficient permissions");
    case ErrorCode::AlreadyExists:       return QStringLiteral("Already exists");
    case ErrorCode::NotFound:            return QStringLiteral("Not found");
    default:                             return QStringLiteral("Error");
    }
}

bool ErrorFormatter::isSecurityError(ErrorCode code)
{
    switch (code) {
    case ErrorCode::AuthorizationDenied:
    case ErrorCode::StepUpRequired:
    case ErrorCode::AccountLocked:
    case ErrorCode::CaptchaRequired:
    case ErrorCode::InvalidCredentials:
    case ErrorCode::SignatureInvalid:
    case ErrorCode::TrustStoreMiss:
    case ErrorCode::EncryptionFailed:
    case ErrorCode::DecryptionFailed:
    case ErrorCode::KeyNotFound:
    case ErrorCode::ChainIntegrityFailed:
        return true;
    default:
        return false;
    }
}

bool ErrorFormatter::requiresStepUp(ErrorCode code)
{
    return code == ErrorCode::StepUpRequired;
}
