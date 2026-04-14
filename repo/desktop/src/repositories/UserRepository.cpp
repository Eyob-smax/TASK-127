// UserRepository.cpp — ProctorOps
// Concrete SQLite implementation for user, credential, session, lockout, CAPTCHA, and step-up.

#include "UserRepository.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>

UserRepository::UserRepository(QSqlDatabase& db)
    : m_db(db)
{
}

// ── Helpers ────────────────────────────────────────────────────────────────

User UserRepository::userFromQuery(QSqlQuery& q)
{
    User u;
    u.id              = q.value(0).toString();
    u.username        = q.value(1).toString();
    u.role            = roleFromDbString(q.value(2).toString());
    u.status          = userStatusFromString(q.value(3).toString());
    u.createdAt       = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
    u.updatedAt       = QDateTime::fromString(q.value(5).toString(), Qt::ISODateWithMs);
    u.createdByUserId = q.value(6).toString();
    return u;
}

QString UserRepository::userStatusToString(UserStatus s)
{
    switch (s) {
    case UserStatus::Active:      return QStringLiteral("Active");
    case UserStatus::Locked:      return QStringLiteral("Locked");
    case UserStatus::Deactivated: return QStringLiteral("Deactivated");
    }
    return QStringLiteral("Active");
}

UserStatus UserRepository::userStatusFromString(const QString& s)
{
    if (s == QStringLiteral("Locked"))      return UserStatus::Locked;
    if (s == QStringLiteral("Deactivated")) return UserStatus::Deactivated;
    return UserStatus::Active;
}

QString UserRepository::roleToDbString(Role r)
{
    return roleToString(r); // from CommonTypes.h
}

Role UserRepository::roleFromDbString(const QString& s)
{
    if (s == QStringLiteral("FrontDeskOperator") || s == QStringLiteral("Operator"))
        return Role::FrontDeskOperator;
    if (s == QStringLiteral("Proctor"))
        return Role::Proctor;
    if (s == QStringLiteral("ContentManager"))
        return Role::ContentManager;
    if (s == QStringLiteral("SecurityAdministrator"))
        return Role::SecurityAdministrator;
    if (s == QStringLiteral("PROCTOR"))               return Role::Proctor;
    if (s == QStringLiteral("CONTENT_MANAGER"))       return Role::ContentManager;
    if (s == QStringLiteral("SECURITY_ADMINISTRATOR")) return Role::SecurityAdministrator;
    return Role::FrontDeskOperator;
}

// ── Users ──────────────────────────────────────────────────────────────────

Result<User> UserRepository::insertUser(const User& user)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO users (id, username, role, status, created_at, updated_at, created_by_user_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(user.id);
    q.addBindValue(user.username);
    q.addBindValue(roleToDbString(user.role));
    q.addBindValue(userStatusToString(user.status));
    q.addBindValue(user.createdAt.toString(Qt::ISODateWithMs));
    q.addBindValue(user.updatedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(user.createdByUserId.isEmpty() ? QVariant() : user.createdByUserId);

    if (!q.exec())
        return Result<User>::err(ErrorCode::DbError, q.lastError().text());

    return Result<User>::ok(user);
}

Result<User> UserRepository::insertUserWithCredential(const User& user,
                                                      const Credential& cred)
{
    if (cred.userId != user.id) {
        return Result<User>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Credential user_id must match user id"));
    }

    if (!m_db.transaction()) {
        return Result<User>::err(
            ErrorCode::DbError,
            QStringLiteral("Failed to begin user provisioning transaction"));
    }

    auto userResult = insertUser(user);
    if (userResult.isErr()) {
        m_db.rollback();
        return userResult;
    }

    auto credentialResult = upsertCredential(cred);
    if (credentialResult.isErr()) {
        m_db.rollback();
        return Result<User>::err(credentialResult.errorCode(), credentialResult.errorMessage());
    }

    if (!m_db.commit()) {
        m_db.rollback();
        return Result<User>::err(
            ErrorCode::DbError,
            QStringLiteral("Failed to commit user provisioning transaction"));
    }

    return Result<User>::ok(user);
}

Result<User> UserRepository::findUserById(const QString& userId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, username, role, status, created_at, updated_at, created_by_user_id "
        "FROM users WHERE id = ?"));
    q.addBindValue(userId);

    if (!q.exec())
        return Result<User>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<User>::err(ErrorCode::NotFound, QStringLiteral("User not found"));

    return Result<User>::ok(userFromQuery(q));
}

Result<User> UserRepository::findUserByUsername(const QString& username)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, username, role, status, created_at, updated_at, created_by_user_id "
        "FROM users WHERE username = ? COLLATE NOCASE"));
    q.addBindValue(username);

    if (!q.exec())
        return Result<User>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<User>::err(ErrorCode::NotFound, QStringLiteral("User not found"));

    return Result<User>::ok(userFromQuery(q));
}

Result<QList<User>> UserRepository::listUsers()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT id, username, role, status, created_at, updated_at, created_by_user_id "
            "FROM users ORDER BY username")))
        return Result<QList<User>>::err(ErrorCode::DbError, q.lastError().text());

    QList<User> users;
    while (q.next())
        users.append(userFromQuery(q));
    return Result<QList<User>>::ok(std::move(users));
}

Result<void> UserRepository::updateUserStatus(const QString& userId, UserStatus status)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE users SET status = ?, updated_at = ? WHERE id = ?"));
    q.addBindValue(userStatusToString(status));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    q.addBindValue(userId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<void>::err(ErrorCode::NotFound, QStringLiteral("User not found"));

    return Result<void>::ok();
}

Result<void> UserRepository::updateUserRole(const QString& userId, Role role)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE users SET role = ?, updated_at = ? WHERE id = ?"));
    q.addBindValue(roleToDbString(role));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    q.addBindValue(userId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<void>::err(ErrorCode::NotFound, QStringLiteral("User not found"));

    return Result<void>::ok();
}

// ── Credentials ────────────────────────────────────────────────────────────

Result<void> UserRepository::upsertCredential(const Credential& cred)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO credentials "
        "(user_id, algorithm, time_cost, memory_cost, parallelism, tag_length, "
        " salt_hex, hash_hex, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(cred.userId);
    q.addBindValue(cred.algorithm);
    q.addBindValue(cred.timeCost);
    q.addBindValue(cred.memoryCost);
    q.addBindValue(cred.parallelism);
    q.addBindValue(cred.tagLength);
    q.addBindValue(cred.saltHex);
    q.addBindValue(cred.hashHex);
    q.addBindValue(cred.updatedAt.toString(Qt::ISODateWithMs));

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<Credential> UserRepository::getCredential(const QString& userId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT user_id, algorithm, time_cost, memory_cost, parallelism, "
        "       tag_length, salt_hex, hash_hex, updated_at "
        "FROM credentials WHERE user_id = ?"));
    q.addBindValue(userId);

    if (!q.exec())
        return Result<Credential>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<Credential>::err(ErrorCode::NotFound, QStringLiteral("Credential not found"));

    Credential c;
    c.userId      = q.value(0).toString();
    c.algorithm   = q.value(1).toString();
    c.timeCost    = q.value(2).toInt();
    c.memoryCost  = q.value(3).toInt();
    c.parallelism = q.value(4).toInt();
    c.tagLength   = q.value(5).toInt();
    c.saltHex     = q.value(6).toString();
    c.hashHex     = q.value(7).toString();
    c.updatedAt   = QDateTime::fromString(q.value(8).toString(), Qt::ISODateWithMs);

    return Result<Credential>::ok(std::move(c));
}

// ── Sessions ───────────────────────────────────────────────────────────────

Result<UserSession> UserRepository::insertSession(const UserSession& session)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO user_sessions (token, user_id, created_at, last_active_at, active) "
        "VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(session.token);
    q.addBindValue(session.userId);
    q.addBindValue(session.createdAt.toString(Qt::ISODateWithMs));
    q.addBindValue(session.lastActiveAt.toString(Qt::ISODateWithMs));
    q.addBindValue(session.active ? 1 : 0);

    if (!q.exec())
        return Result<UserSession>::err(ErrorCode::DbError, q.lastError().text());

    return Result<UserSession>::ok(session);
}

Result<UserSession> UserRepository::findSession(const QString& token)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT token, user_id, created_at, last_active_at, active "
        "FROM user_sessions WHERE token = ?"));
    q.addBindValue(token);

    if (!q.exec())
        return Result<UserSession>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<UserSession>::err(ErrorCode::NotFound, QStringLiteral("Session not found"));

    UserSession s;
    s.token        = q.value(0).toString();
    s.userId       = q.value(1).toString();
    s.createdAt    = QDateTime::fromString(q.value(2).toString(), Qt::ISODateWithMs);
    s.lastActiveAt = QDateTime::fromString(q.value(3).toString(), Qt::ISODateWithMs);
    s.active       = q.value(4).toInt() != 0;

    return Result<UserSession>::ok(std::move(s));
}

Result<void> UserRepository::deactivateSession(const QString& token)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE user_sessions SET active = 0 WHERE token = ?"));
    q.addBindValue(token);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<void> UserRepository::touchSession(const QString& token, const QDateTime& at)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE user_sessions SET last_active_at = ? WHERE token = ? AND active = 1"));
    q.addBindValue(at.toString(Qt::ISODateWithMs));
    q.addBindValue(token);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

// ── Lockout ────────────────────────────────────────────────────────────────

Result<LockoutRecord> UserRepository::getLockoutRecord(const QString& username)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT username, failed_attempts, first_fail_at, locked_at "
        "FROM lockout_records WHERE username = ?"));
    q.addBindValue(username);

    if (!q.exec())
        return Result<LockoutRecord>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<LockoutRecord>::err(ErrorCode::NotFound);

    LockoutRecord rec;
    rec.username       = q.value(0).toString();
    rec.failedAttempts = q.value(1).toInt();
    rec.firstFailAt    = q.value(2).isNull() ? QDateTime()
                           : QDateTime::fromString(q.value(2).toString(), Qt::ISODateWithMs);
    rec.lockedAt       = q.value(3).isNull() ? QDateTime()
                           : QDateTime::fromString(q.value(3).toString(), Qt::ISODateWithMs);

    return Result<LockoutRecord>::ok(std::move(rec));
}

Result<void> UserRepository::upsertLockoutRecord(const LockoutRecord& rec)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO lockout_records "
        "(username, failed_attempts, first_fail_at, locked_at) "
        "VALUES (?, ?, ?, ?)"));
    q.addBindValue(rec.username);
    q.addBindValue(rec.failedAttempts);
    q.addBindValue(rec.firstFailAt.isNull() ? QVariant() : rec.firstFailAt.toString(Qt::ISODateWithMs));
    q.addBindValue(rec.lockedAt.isNull() ? QVariant() : rec.lockedAt.toString(Qt::ISODateWithMs));

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<void> UserRepository::clearLockoutRecord(const QString& username)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM lockout_records WHERE username = ?"));
    q.addBindValue(username);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

// ── CAPTCHA ────────────────────────────────────────────────────────────────

Result<CaptchaState> UserRepository::getCaptchaState(const QString& username)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT username, challenge_id, answer_hash_hex, issued_at, expires_at, "
        "       solve_attempts, solved "
        "FROM captcha_states WHERE username = ?"));
    q.addBindValue(username);

    if (!q.exec())
        return Result<CaptchaState>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<CaptchaState>::err(ErrorCode::NotFound);

    CaptchaState state;
    state.username      = q.value(0).toString();
    state.challengeId   = q.value(1).toString();
    state.answerHashHex = q.value(2).toString();
    state.issuedAt      = QDateTime::fromString(q.value(3).toString(), Qt::ISODateWithMs);
    state.expiresAt     = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
    state.solveAttempts = q.value(5).toInt();
    state.solved        = q.value(6).toInt() != 0;

    return Result<CaptchaState>::ok(std::move(state));
}

Result<void> UserRepository::upsertCaptchaState(const CaptchaState& state)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO captcha_states "
        "(username, challenge_id, answer_hash_hex, issued_at, expires_at, "
        " solve_attempts, solved) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(state.username);
    q.addBindValue(state.challengeId);
    q.addBindValue(state.answerHashHex);
    q.addBindValue(state.issuedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(state.expiresAt.toString(Qt::ISODateWithMs));
    q.addBindValue(state.solveAttempts);
    q.addBindValue(state.solved ? 1 : 0);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

Result<void> UserRepository::clearCaptchaState(const QString& username)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM captcha_states WHERE username = ?"));
    q.addBindValue(username);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

// ── Step-up windows ────────────────────────────────────────────────────────

Result<StepUpWindow> UserRepository::insertStepUpWindow(const StepUpWindow& win)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO step_up_windows "
        "(id, user_id, session_token, granted_at, expires_at, consumed) "
        "VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(win.id);
    q.addBindValue(win.userId);
    q.addBindValue(win.sessionToken);
    q.addBindValue(win.grantedAt.toString(Qt::ISODateWithMs));
    q.addBindValue(win.expiresAt.toString(Qt::ISODateWithMs));
    q.addBindValue(win.consumed ? 1 : 0);

    if (!q.exec())
        return Result<StepUpWindow>::err(ErrorCode::DbError, q.lastError().text());

    return Result<StepUpWindow>::ok(win);
}

Result<StepUpWindow> UserRepository::findStepUpWindow(const QString& id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, user_id, session_token, granted_at, expires_at, consumed "
        "FROM step_up_windows WHERE id = ?"));
    q.addBindValue(id);

    if (!q.exec())
        return Result<StepUpWindow>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<StepUpWindow>::err(ErrorCode::NotFound);

    StepUpWindow win;
    win.id           = q.value(0).toString();
    win.userId       = q.value(1).toString();
    win.sessionToken = q.value(2).toString();
    win.grantedAt    = QDateTime::fromString(q.value(3).toString(), Qt::ISODateWithMs);
    win.expiresAt    = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
    win.consumed     = q.value(5).toInt() != 0;

    return Result<StepUpWindow>::ok(std::move(win));
}

Result<StepUpWindow> UserRepository::findLatestStepUpWindowForSession(const QString& token)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, user_id, session_token, granted_at, expires_at, consumed "
        "FROM step_up_windows WHERE session_token = ? "
        "ORDER BY granted_at DESC LIMIT 1"));
    q.addBindValue(token);

    if (!q.exec())
        return Result<StepUpWindow>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<StepUpWindow>::err(ErrorCode::NotFound);

    StepUpWindow win;
    win.id           = q.value(0).toString();
    win.userId       = q.value(1).toString();
    win.sessionToken = q.value(2).toString();
    win.grantedAt    = QDateTime::fromString(q.value(3).toString(), Qt::ISODateWithMs);
    win.expiresAt    = QDateTime::fromString(q.value(4).toString(), Qt::ISODateWithMs);
    win.consumed     = q.value(5).toInt() != 0;

    return Result<StepUpWindow>::ok(std::move(win));
}

Result<void> UserRepository::consumeStepUpWindow(const QString& id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE step_up_windows SET consumed = 1 WHERE id = ? AND consumed = 0"));
    q.addBindValue(id);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<void>::err(ErrorCode::NotFound, QStringLiteral("Step-up window not found or already consumed"));

    return Result<void>::ok();
}
