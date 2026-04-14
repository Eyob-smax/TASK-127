#pragma once
// IUserRepository.h — ProctorOps
// Pure interface for user, credential, lockout, CAPTCHA, and step-up data access.
// Implementations use QSQLITE. Repositories contain no business logic.

#include "models/User.h"
#include "utils/Result.h"
#include <QString>
#include <QList>

class IUserRepository {
public:
    virtual ~IUserRepository() = default;

    // ── Users ──────────────────────────────────────────────────────────────
    virtual Result<User>         insertUser(const User& user)                        = 0;
    // Atomically inserts user and initial credential in a single transaction.
    virtual Result<User>         insertUserWithCredential(const User& user,
                                                          const Credential& cred)     = 0;
    virtual Result<User>         findUserById(const QString& userId)                  = 0;
    virtual Result<User>         findUserByUsername(const QString& username)          = 0;
    virtual Result<QList<User>>  listUsers()                                          = 0;
    virtual Result<void>         updateUserStatus(const QString& userId, UserStatus)  = 0;
    virtual Result<void>         updateUserRole(const QString& userId, Role role)     = 0;

    // ── Credentials ────────────────────────────────────────────────────────
    // One credential record per user (upsert on password change).
    virtual Result<void>        upsertCredential(const Credential& cred)              = 0;
    virtual Result<Credential>  getCredential(const QString& userId)                  = 0;

    // ── Sessions ───────────────────────────────────────────────────────────
    virtual Result<UserSession>  insertSession(const UserSession& session)             = 0;
    virtual Result<UserSession>  findSession(const QString& token)                     = 0;
    virtual Result<void>         deactivateSession(const QString& token)               = 0;
    virtual Result<void>         touchSession(const QString& token, const QDateTime&)  = 0;

    // ── Lockout ────────────────────────────────────────────────────────────
    virtual Result<LockoutRecord>  getLockoutRecord(const QString& username)           = 0;
    virtual Result<void>           upsertLockoutRecord(const LockoutRecord& rec)       = 0;
    virtual Result<void>           clearLockoutRecord(const QString& username)         = 0;

    // ── CAPTCHA ────────────────────────────────────────────────────────────
    virtual Result<CaptchaState>  getCaptchaState(const QString& username)             = 0;
    virtual Result<void>          upsertCaptchaState(const CaptchaState& state)        = 0;
    virtual Result<void>          clearCaptchaState(const QString& username)           = 0;

    // ── Step-up windows ────────────────────────────────────────────────────
    virtual Result<StepUpWindow>  insertStepUpWindow(const StepUpWindow& win)          = 0;
    virtual Result<StepUpWindow>  findStepUpWindow(const QString& id)                  = 0;
    virtual Result<StepUpWindow>  findLatestStepUpWindowForSession(const QString& token) = 0;
    virtual Result<void>          consumeStepUpWindow(const QString& id)               = 0;
};
