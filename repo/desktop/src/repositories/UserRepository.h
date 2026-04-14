#pragma once
// UserRepository.h — ProctorOps
// Concrete SQLite implementation of IUserRepository.
// All queries use parameterized bindings — no string interpolation.

#include "IUserRepository.h"
#include <QSqlDatabase>

class UserRepository : public IUserRepository {
public:
    explicit UserRepository(QSqlDatabase& db);

    // ── Users ──────────────────────────────────────────────────────────────
    Result<User>         insertUser(const User& user) override;
    Result<User>         insertUserWithCredential(const User& user,
                                                   const Credential& cred) override;
    Result<User>         findUserById(const QString& userId) override;
    Result<User>         findUserByUsername(const QString& username) override;
    Result<QList<User>>  listUsers() override;
    Result<void>         updateUserStatus(const QString& userId, UserStatus status) override;
    Result<void>         updateUserRole(const QString& userId, Role role) override;

    // ── Credentials ────────────────────────────────────────────────────────
    Result<void>        upsertCredential(const Credential& cred) override;
    Result<Credential>  getCredential(const QString& userId) override;

    // ── Sessions ───────────────────────────────────────────────────────────
    Result<UserSession>  insertSession(const UserSession& session) override;
    Result<UserSession>  findSession(const QString& token) override;
    Result<void>         deactivateSession(const QString& token) override;
    Result<void>         touchSession(const QString& token, const QDateTime& at) override;

    // ── Lockout ────────────────────────────────────────────────────────────
    Result<LockoutRecord>  getLockoutRecord(const QString& username) override;
    Result<void>           upsertLockoutRecord(const LockoutRecord& rec) override;
    Result<void>           clearLockoutRecord(const QString& username) override;

    // ── CAPTCHA ────────────────────────────────────────────────────────────
    Result<CaptchaState>  getCaptchaState(const QString& username) override;
    Result<void>          upsertCaptchaState(const CaptchaState& state) override;
    Result<void>          clearCaptchaState(const QString& username) override;

    // ── Step-up windows ────────────────────────────────────────────────────
    Result<StepUpWindow>  insertStepUpWindow(const StepUpWindow& win) override;
    Result<StepUpWindow>  findStepUpWindow(const QString& id) override;
    Result<StepUpWindow>  findLatestStepUpWindowForSession(const QString& token) override;
    Result<void>          consumeStepUpWindow(const QString& id) override;

private:
    QSqlDatabase& m_db;

    static User userFromQuery(class QSqlQuery& q);
    static QString userStatusToString(UserStatus s);
    static UserStatus userStatusFromString(const QString& s);
    static QString roleToDbString(Role r);
    static Role roleFromDbString(const QString& s);
};
