// CommonTypes.cpp — ProctorOps
// String conversion for AuditEventType (declared in CommonTypes.h).

#include "CommonTypes.h"

QString auditEventTypeToString(AuditEventType t)
{
    switch (t) {
    // Auth
    case AuditEventType::Login:              return QStringLiteral("LOGIN");
    case AuditEventType::LoginFailed:        return QStringLiteral("LOGIN_FAILED");
    case AuditEventType::LoginLocked:        return QStringLiteral("LOGIN_LOCKED");
    case AuditEventType::CaptchaChallenge:   return QStringLiteral("CAPTCHA_CHALLENGE");
    case AuditEventType::CaptchaSolved:      return QStringLiteral("CAPTCHA_SOLVED");
    case AuditEventType::CaptchaFailed:      return QStringLiteral("CAPTCHA_FAILED");
    case AuditEventType::Logout:             return QStringLiteral("LOGOUT");
    case AuditEventType::ConsoleLocked:      return QStringLiteral("CONSOLE_LOCKED");
    case AuditEventType::ConsoleUnlocked:    return QStringLiteral("CONSOLE_UNLOCKED");
    case AuditEventType::StepUpInitiated:    return QStringLiteral("STEP_UP_INITIATED");
    case AuditEventType::StepUpPassed:       return QStringLiteral("STEP_UP_PASSED");
    case AuditEventType::StepUpFailed:       return QStringLiteral("STEP_UP_FAILED");

    // User management
    case AuditEventType::UserCreated:        return QStringLiteral("USER_CREATED");
    case AuditEventType::UserUpdated:        return QStringLiteral("USER_UPDATED");
    case AuditEventType::UserDeactivated:    return QStringLiteral("USER_DEACTIVATED");
    case AuditEventType::RoleChanged:        return QStringLiteral("ROLE_CHANGED");
    case AuditEventType::UserUnlocked:       return QStringLiteral("USER_UNLOCKED");
    case AuditEventType::PasswordReset:      return QStringLiteral("PASSWORD_RESET");
    case AuditEventType::MemberFreezeApplied:return QStringLiteral("MEMBER_FREEZE_APPLIED");
    case AuditEventType::MemberFreezeThawed: return QStringLiteral("MEMBER_FREEZE_THAWED");

    // Check-in
    case AuditEventType::CheckInAttempted:          return QStringLiteral("CHECKIN_ATTEMPTED");
    case AuditEventType::CheckInSuccess:             return QStringLiteral("CHECKIN_SUCCESS");
    case AuditEventType::CheckInDuplicateBlocked:    return QStringLiteral("CHECKIN_DUPLICATE_BLOCKED");
    case AuditEventType::CheckInFrozenBlocked:       return QStringLiteral("CHECKIN_FROZEN_BLOCKED");
    case AuditEventType::CheckInExpiredBlocked:      return QStringLiteral("CHECKIN_EXPIRED_BLOCKED");
    case AuditEventType::CheckInTermCardInvalid:     return QStringLiteral("CHECKIN_TERM_CARD_INVALID");
    case AuditEventType::CheckInPunchCardExhausted:  return QStringLiteral("CHECKIN_PUNCH_CARD_EXHAUSTED");
    case AuditEventType::DeductionCreated:           return QStringLiteral("DEDUCTION_CREATED");
    case AuditEventType::DeductionReversed:          return QStringLiteral("DEDUCTION_REVERSED");
    case AuditEventType::CorrectionRequested:        return QStringLiteral("CORRECTION_REQUESTED");
    case AuditEventType::CorrectionApproved:         return QStringLiteral("CORRECTION_APPROVED");
    case AuditEventType::CorrectionRejected:         return QStringLiteral("CORRECTION_REJECTED");
    case AuditEventType::CorrectionApplied:          return QStringLiteral("CORRECTION_APPLIED");

    // Question governance
    case AuditEventType::QuestionCreated:           return QStringLiteral("QUESTION_CREATED");
    case AuditEventType::QuestionUpdated:           return QStringLiteral("QUESTION_UPDATED");
    case AuditEventType::QuestionDeleted:           return QStringLiteral("QUESTION_DELETED");
    case AuditEventType::KnowledgePointCreated:     return QStringLiteral("KNOWLEDGE_POINT_CREATED");
    case AuditEventType::KnowledgePointUpdated:     return QStringLiteral("KNOWLEDGE_POINT_UPDATED");
    case AuditEventType::KnowledgePointDeleted:     return QStringLiteral("KNOWLEDGE_POINT_DELETED");
    case AuditEventType::KnowledgePointMapped:      return QStringLiteral("KNOWLEDGE_POINT_MAPPED");
    case AuditEventType::KnowledgePointUnmapped:    return QStringLiteral("KNOWLEDGE_POINT_UNMAPPED");
    case AuditEventType::TagCreated:                return QStringLiteral("TAG_CREATED");
    case AuditEventType::TagApplied:                return QStringLiteral("TAG_APPLIED");
    case AuditEventType::TagRemoved:                return QStringLiteral("TAG_REMOVED");

    // Ingestion
    case AuditEventType::JobCreated:       return QStringLiteral("JOB_CREATED");
    case AuditEventType::JobStarted:       return QStringLiteral("JOB_STARTED");
    case AuditEventType::JobCompleted:     return QStringLiteral("JOB_COMPLETED");
    case AuditEventType::JobFailed:        return QStringLiteral("JOB_FAILED");
    case AuditEventType::JobCancelled:     return QStringLiteral("JOB_CANCELLED");
    case AuditEventType::JobInterrupted:   return QStringLiteral("JOB_INTERRUPTED");

    // Sync and packages
    case AuditEventType::SyncExport:            return QStringLiteral("SYNC_EXPORT");
    case AuditEventType::SyncImport:            return QStringLiteral("SYNC_IMPORT");
    case AuditEventType::SyncConflictResolved:  return QStringLiteral("SYNC_CONFLICT_RESOLVED");

    // Update and rollback
    case AuditEventType::UpdateImported:     return QStringLiteral("UPDATE_IMPORTED");
    case AuditEventType::UpdateStaged:       return QStringLiteral("UPDATE_STAGED");
    case AuditEventType::UpdateApplied:      return QStringLiteral("UPDATE_APPLIED");
    case AuditEventType::UpdateRolledBack:   return QStringLiteral("UPDATE_ROLLED_BACK");

    // Crypto / trust store
    case AuditEventType::KeyImported:  return QStringLiteral("KEY_IMPORTED");
    case AuditEventType::KeyRevoked:   return QStringLiteral("KEY_REVOKED");
    case AuditEventType::KeyRotated:   return QStringLiteral("KEY_ROTATED");

    // Export and compliance
    case AuditEventType::ExportRequested:     return QStringLiteral("EXPORT_REQUESTED");
    case AuditEventType::ExportCompleted:     return QStringLiteral("EXPORT_COMPLETED");
    case AuditEventType::DeletionRequested:   return QStringLiteral("DELETION_REQUESTED");
    case AuditEventType::DeletionApproved:    return QStringLiteral("DELETION_APPROVED");
    case AuditEventType::DeletionCompleted:   return QStringLiteral("DELETION_COMPLETED");

    // Audit itself
    case AuditEventType::ChainVerified:  return QStringLiteral("CHAIN_VERIFIED");
    case AuditEventType::AuditExport:    return QStringLiteral("AUDIT_EXPORT");
    }
    return QStringLiteral("UNKNOWN");
}
