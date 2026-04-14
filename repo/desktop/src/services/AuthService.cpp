// AuthService.cpp — ProctorOps
// Authentication, lockout, CAPTCHA, RBAC, step-up, and console lock/unlock.

#include "AuthService.h"
#include "crypto/Argon2idHasher.h"
#include "crypto/HashChain.h"
#include "utils/CaptchaGenerator.h"
#include "utils/Logger.h"
#include "utils/Validation.h"

#include <QDateTime>
#include <QUuid>

AuthService::AuthService(IUserRepository& userRepo, IAuditRepository& auditRepo)
    : m_userRepo(userRepo)
    , m_auditRepo(auditRepo)
{
}

Result<UserSession> AuthService::signIn(const QString& username,
                                         const QString& password,
                                         const QString& captchaAnswer)
{
    // 1. Find user
    auto userResult = m_userRepo.findUserByUsername(username);
    if (userResult.isErr()) {
        Logger::instance().security(QStringLiteral("AuthService"),
            QStringLiteral("Sign-in attempt for unknown user"),
            {{QStringLiteral("username"), username}});
        return Result<UserSession>::err(ErrorCode::InvalidCredentials,
            QStringLiteral("Invalid username or password"));
    }
    const User& user = userResult.value();

    // 2. Check if user is deactivated
    if (user.status == UserStatus::Deactivated)
        return Result<UserSession>::err(ErrorCode::AuthorizationDenied,
            QStringLiteral("Account is deactivated"));

    // 3. Check lockout state
    auto lockoutResult = m_userRepo.getLockoutRecord(username);
    if (lockoutResult.isOk()) {
        const LockoutRecord& lockout = lockoutResult.value();
        QDateTime now = QDateTime::currentDateTimeUtc();

        // Check if the lockout window has expired — reset if so
        if (lockout.failedAttempts > 0 && !lockout.firstFailAt.isNull()) {
            qint64 secsSinceFirst = lockout.firstFailAt.secsTo(now);
            if (secsSinceFirst > Validation::LockoutWindowSeconds) {
                // Window expired; reset lockout
                m_userRepo.clearLockoutRecord(username);
            } else {
                // Window still active
                if (!lockout.lockedAt.isNull()) {
                    // Account is locked
                    Logger::instance().security(QStringLiteral("AuthService"),
                        QStringLiteral("Sign-in rejected: account locked"),
                        {{QStringLiteral("username"), username}});
                    recordAudit(user.id, AuditEventType::LoginLocked,
                                QStringLiteral("User"), user.id);
                    return Result<UserSession>::err(ErrorCode::AccountLocked,
                        QStringLiteral("Account is locked due to too many failed attempts"));
                }

                // 4. Check if CAPTCHA is required (3+ failures)
                if (lockout.failedAttempts >= Validation::CaptchaAfterFailures) {
                    auto captchaResult = m_userRepo.getCaptchaState(username);
                    if (captchaResult.isErr()) {
                        // Fail closed: once CAPTCHA is required, a missing state must not
                        // allow password verification to continue.
                        auto refreshResult = generateCaptcha(username);
                        if (refreshResult.isErr()) {
                            Logger::instance().error(QStringLiteral("AuthService"),
                                QStringLiteral("Failed to regenerate CAPTCHA state"),
                                {{QStringLiteral("username"), username},
                                 {QStringLiteral("error"), refreshResult.errorMessage()}});
                        }
                        return Result<UserSession>::err(ErrorCode::CaptchaRequired,
                            QStringLiteral("CAPTCHA verification required"));
                    }

                    bool captchaExpired = false;
                    if (captchaResult.value().expiresAt.isValid())
                        captchaExpired = now > captchaResult.value().expiresAt;

                    if (!captchaExpired) {
                        if (captchaAnswer.isEmpty()) {
                            return Result<UserSession>::err(ErrorCode::CaptchaRequired,
                                QStringLiteral("CAPTCHA verification required"));
                        }

                        // Verify CAPTCHA answer
                        bool captchaOk = CaptchaGenerator::verifyAnswer(
                            captchaAnswer, captchaResult.value().answerHashHex);
                        if (!captchaOk) {
                            recordAudit(user.id, AuditEventType::CaptchaFailed,
                                        QStringLiteral("User"), user.id);
                            return Result<UserSession>::err(ErrorCode::CaptchaRequired,
                                QStringLiteral("Incorrect CAPTCHA answer"));
                        }
                        recordAudit(user.id, AuditEventType::CaptchaSolved,
                                    QStringLiteral("User"), user.id);
                    }
                }
            }
        }
    }

    // 5. Verify password
    auto credResult = m_userRepo.getCredential(user.id);
    if (credResult.isErr())
        return Result<UserSession>::err(ErrorCode::InvalidCredentials,
            QStringLiteral("Invalid username or password"));

    auto verifyResult = Argon2idHasher::verifyPassword(password, credResult.value());
    if (verifyResult.isErr())
        return Result<UserSession>::err(ErrorCode::InternalError, verifyResult.errorMessage());

    if (!verifyResult.value()) {
        // Password mismatch — increment lockout
        QDateTime now = QDateTime::currentDateTimeUtc();
        auto currentLockout = m_userRepo.getLockoutRecord(username);

        LockoutRecord rec;
        rec.username = username;
        if (currentLockout.isOk() && currentLockout.value().failedAttempts > 0) {
            rec = currentLockout.value();
            rec.failedAttempts++;
        } else {
            rec.failedAttempts = 1;
            rec.firstFailAt = now;
        }

        // Lock if threshold reached
        if (rec.failedAttempts >= Validation::LockoutFailureThreshold) {
            rec.lockedAt = now;
            m_userRepo.updateUserStatus(user.id, UserStatus::Locked);
            Logger::instance().security(QStringLiteral("AuthService"),
                QStringLiteral("Account locked after %1 failures")
                    .arg(rec.failedAttempts),
                {{QStringLiteral("username"), username}});
            recordAudit(user.id, AuditEventType::LoginLocked,
                        QStringLiteral("User"), user.id);
        } else {
            recordAudit(user.id, AuditEventType::LoginFailed,
                        QStringLiteral("User"), user.id);
        }

        m_userRepo.upsertLockoutRecord(rec);

        // Generate CAPTCHA if threshold reached
        if (rec.failedAttempts >= Validation::CaptchaAfterFailures) {
            generateCaptcha(username);
        }

        Logger::instance().security(QStringLiteral("AuthService"),
            QStringLiteral("Sign-in failed: wrong password"),
            {{QStringLiteral("username"), username},
             {QStringLiteral("attempts"), rec.failedAttempts}});

        return Result<UserSession>::err(ErrorCode::InvalidCredentials,
            QStringLiteral("Invalid username or password"));
    }

    // 6. Success — clear lockout, create session
    m_userRepo.clearLockoutRecord(username);
    m_userRepo.clearCaptchaState(username);

    if (user.status == UserStatus::Locked)
        m_userRepo.updateUserStatus(user.id, UserStatus::Active);

    QDateTime now = QDateTime::currentDateTimeUtc();
    UserSession session;
    session.token        = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.userId       = user.id;
    session.createdAt    = now;
    session.lastActiveAt = now;
    session.active       = true;

    auto insertResult = m_userRepo.insertSession(session);
    if (insertResult.isErr())
        return Result<UserSession>::err(insertResult.errorCode(), insertResult.errorMessage());

    recordAudit(user.id, AuditEventType::Login, QStringLiteral("User"), user.id);
    Logger::instance().info(QStringLiteral("AuthService"),
        QStringLiteral("Sign-in successful"),
        {{QStringLiteral("username"), username}});

    return Result<UserSession>::ok(std::move(session));
}

Result<void> AuthService::signOut(const QString& sessionToken)
{
    auto sessionResult = m_userRepo.findSession(sessionToken);
    if (sessionResult.isErr())
        return Result<void>::err(sessionResult.errorCode(), sessionResult.errorMessage());

    m_userRepo.deactivateSession(sessionToken);
    recordAudit(sessionResult.value().userId, AuditEventType::Logout,
                QStringLiteral("Session"), sessionToken);

    return Result<void>::ok();
}

Result<StepUpWindow> AuthService::initiateStepUp(const QString& sessionToken,
                                                   const QString& password)
{
    auto sessionResult = m_userRepo.findSession(sessionToken);
    if (sessionResult.isErr())
        return Result<StepUpWindow>::err(ErrorCode::NotFound, QStringLiteral("Invalid session"));
    if (!sessionResult.value().active)
        return Result<StepUpWindow>::err(ErrorCode::AuthorizationDenied,
            QStringLiteral("Session is not active"));

    const UserSession& session = sessionResult.value();

    // Verify password
    auto credResult = m_userRepo.getCredential(session.userId);
    if (credResult.isErr())
        return Result<StepUpWindow>::err(ErrorCode::InternalError, QStringLiteral("Credential not found"));

    auto verifyResult = Argon2idHasher::verifyPassword(password, credResult.value());
    if (verifyResult.isErr())
        return Result<StepUpWindow>::err(ErrorCode::InternalError, verifyResult.errorMessage());

    if (!verifyResult.value()) {
        recordAudit(session.userId, AuditEventType::StepUpFailed,
                    QStringLiteral("Session"), sessionToken);
        return Result<StepUpWindow>::err(ErrorCode::InvalidCredentials,
            QStringLiteral("Incorrect password for step-up verification"));
    }

    // Create step-up window
    QDateTime now = QDateTime::currentDateTimeUtc();
    StepUpWindow win;
    win.id           = QUuid::createUuid().toString(QUuid::WithoutBraces);
    win.userId       = session.userId;
    win.sessionToken = sessionToken;
    win.grantedAt    = now;
    win.expiresAt    = now.addSecs(Validation::StepUpWindowSeconds);
    win.consumed     = false;

    auto insertResult = m_userRepo.insertStepUpWindow(win);
    if (insertResult.isErr())
        return Result<StepUpWindow>::err(insertResult.errorCode(), insertResult.errorMessage());

    recordAudit(session.userId, AuditEventType::StepUpInitiated,
                QStringLiteral("StepUpWindow"), win.id);

    return Result<StepUpWindow>::ok(std::move(win));
}

Result<void> AuthService::consumeStepUp(const QString& stepUpId)
{
    auto winResult = m_userRepo.findStepUpWindow(stepUpId);
    if (winResult.isErr())
        return Result<void>::err(ErrorCode::NotFound, QStringLiteral("Step-up window not found"));

    const StepUpWindow& win = winResult.value();

    if (win.consumed)
        return Result<void>::err(ErrorCode::StepUpRequired,
            QStringLiteral("Step-up window already consumed"));

    QDateTime now = QDateTime::currentDateTimeUtc();
    if (now > win.expiresAt)
        return Result<void>::err(ErrorCode::StepUpRequired,
            QStringLiteral("Step-up window has expired"));

    auto consumeResult = m_userRepo.consumeStepUpWindow(stepUpId);
    if (consumeResult.isErr())
        return consumeResult;

    recordAudit(win.userId, AuditEventType::StepUpPassed,
                QStringLiteral("StepUpWindow"), stepUpId);

    return Result<void>::ok();
}

Result<void> AuthService::lockConsole(const QString& sessionToken)
{
    auto sessionResult = m_userRepo.findSession(sessionToken);
    if (sessionResult.isErr())
        return Result<void>::err(sessionResult.errorCode(), sessionResult.errorMessage());

    recordAudit(sessionResult.value().userId, AuditEventType::ConsoleLocked,
                QStringLiteral("Session"), sessionToken);

    Logger::instance().info(QStringLiteral("AuthService"),
        QStringLiteral("Console locked"));

    return Result<void>::ok();
}

Result<UserSession> AuthService::unlockConsole(const QString& sessionToken,
                                                const QString& password)
{
    auto sessionResult = m_userRepo.findSession(sessionToken);
    if (sessionResult.isErr())
        return Result<UserSession>::err(sessionResult.errorCode(), sessionResult.errorMessage());

    const UserSession& session = sessionResult.value();

    // Verify password
    auto credResult = m_userRepo.getCredential(session.userId);
    if (credResult.isErr())
        return Result<UserSession>::err(ErrorCode::InternalError, QStringLiteral("Credential not found"));

    auto verifyResult = Argon2idHasher::verifyPassword(password, credResult.value());
    if (verifyResult.isErr())
        return Result<UserSession>::err(ErrorCode::InternalError, verifyResult.errorMessage());

    if (!verifyResult.value())
        return Result<UserSession>::err(ErrorCode::InvalidCredentials,
            QStringLiteral("Incorrect password"));

    // Touch session
    QDateTime now = QDateTime::currentDateTimeUtc();
    m_userRepo.touchSession(sessionToken, now);

    recordAudit(session.userId, AuditEventType::ConsoleUnlocked,
                QStringLiteral("Session"), sessionToken);

    Logger::instance().info(QStringLiteral("AuthService"),
        QStringLiteral("Console unlocked"));

    // Return refreshed session
    auto refreshed = m_userRepo.findSession(sessionToken);
    if (refreshed.isErr())
        return Result<UserSession>::err(refreshed.errorCode(), refreshed.errorMessage());

    return Result<UserSession>::ok(std::move(refreshed).value());
}

bool AuthService::hasPermission(Role userRole, Role requiredRole)
{
    return static_cast<int>(userRole) >= static_cast<int>(requiredRole);
}

Result<User> AuthService::requireActiveUser(const QString& userId)
{
    auto userResult = m_userRepo.findUserById(userId);
    if (userResult.isErr())
        return Result<User>::err(userResult.errorCode(), userResult.errorMessage());

    const User& user = userResult.value();
    if (user.status != UserStatus::Active)
        return Result<User>::err(ErrorCode::AuthorizationDenied,
            QStringLiteral("User is not active"));

    return userResult;
}

Result<void> AuthService::requireRole(const QString& sessionToken, Role requiredRole)
{
    auto sessionResult = m_userRepo.findSession(sessionToken);
    if (sessionResult.isErr())
        return Result<void>::err(ErrorCode::NotFound, QStringLiteral("Invalid session"));
    if (!sessionResult.value().active)
        return Result<void>::err(ErrorCode::AuthorizationDenied, QStringLiteral("Session inactive"));

    return requireRoleForActor(sessionResult.value().userId, requiredRole);
}

Result<void> AuthService::requireRoleForActor(const QString& actorUserId, Role requiredRole)
{
    auto userResult = requireActiveUser(actorUserId);
    if (userResult.isErr())
        return Result<void>::err(userResult.errorCode(), userResult.errorMessage());

    if (!hasPermission(userResult.value().role, requiredRole))
        return Result<void>::err(ErrorCode::AuthorizationDenied,
            QStringLiteral("Insufficient permissions: requires %1")
                .arg(roleToString(requiredRole)));

    return Result<void>::ok();
}

Result<void> AuthService::requireStepUp(const QString& sessionToken)
{
    auto sessionResult = m_userRepo.findSession(sessionToken);
    if (sessionResult.isErr())
        return Result<void>::err(ErrorCode::NotFound, QStringLiteral("Invalid session"));
    if (!sessionResult.value().active)
        return Result<void>::err(ErrorCode::AuthorizationDenied, QStringLiteral("Session inactive"));

    auto winResult = m_userRepo.findLatestStepUpWindowForSession(sessionToken);
    if (winResult.isErr())
        return Result<void>::err(ErrorCode::StepUpRequired,
            QStringLiteral("Step-up verification is required for this action"));

    const StepUpWindow& win = winResult.value();
    if (win.userId != sessionResult.value().userId || win.consumed)
        return Result<void>::err(ErrorCode::StepUpRequired,
            QStringLiteral("Step-up verification is required for this action"));

    if (QDateTime::currentDateTimeUtc() > win.expiresAt)
        return Result<void>::err(ErrorCode::StepUpRequired,
            QStringLiteral("Step-up verification is required for this action"));

    return Result<void>::ok();
}

Result<void> AuthService::authorizePrivilegedAction(const QString& actorUserId,
                                                     Role requiredRole,
                                                     const QString& stepUpWindowId)
{
    auto roleResult = requireRoleForActor(actorUserId, requiredRole);
    if (roleResult.isErr())
        return roleResult;

    if (stepUpWindowId.trimmed().isEmpty())
        return Result<void>::err(ErrorCode::StepUpRequired,
            QStringLiteral("Step-up verification is required for this action"));

    auto winResult = m_userRepo.findStepUpWindow(stepUpWindowId);
    if (winResult.isErr())
        return Result<void>::err(ErrorCode::StepUpRequired,
            QStringLiteral("Invalid step-up window"));

    const StepUpWindow& win = winResult.value();
    if (win.userId != actorUserId)
        return Result<void>::err(ErrorCode::AuthorizationDenied,
            QStringLiteral("Step-up window does not belong to this actor"));

    return consumeStepUp(stepUpWindowId);
}

Result<QList<User>> AuthService::listUsers(const QString& actorUserId)
{
    auto authResult = requireRoleForActor(actorUserId, Role::SecurityAdministrator);
    if (authResult.isErr())
        return Result<QList<User>>::err(authResult.errorCode(), authResult.errorMessage());

    return m_userRepo.listUsers();
}

Result<User> AuthService::createUser(const QString& actorUserId,
                                     const QString& username,
                                     const QString& password,
                                     Role role,
                                     const QString& stepUpWindowId)
{
    auto authResult = authorizePrivilegedAction(actorUserId,
        Role::SecurityAdministrator, stepUpWindowId);
    if (authResult.isErr())
        return Result<User>::err(authResult.errorCode(), authResult.errorMessage());

    const QString normalizedUsername = username.trimmed();
    if (normalizedUsername.isEmpty()) {
        return Result<User>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Username is required"));
    }

    if (!Validation::isPasswordLengthValid(password)) {
        return Result<User>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Password must be at least %1 characters")
                .arg(Validation::PasswordMinLength));
    }

    auto existingUser = m_userRepo.findUserByUsername(normalizedUsername);
    if (existingUser.isOk()) {
        return Result<User>::err(
            ErrorCode::AlreadyExists,
            QStringLiteral("Username already exists"));
    }
    if (existingUser.errorCode() != ErrorCode::NotFound)
        return Result<User>::err(existingUser.errorCode(), existingUser.errorMessage());

    auto hashResult = Argon2idHasher::hashPassword(password);
    if (hashResult.isErr())
        return Result<User>::err(ErrorCode::InternalError, hashResult.errorMessage());

    const QDateTime now = QDateTime::currentDateTimeUtc();

    User user;
    user.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    user.username = normalizedUsername;
    user.role = role;
    user.status = UserStatus::Active;
    user.createdAt = now;
    user.updatedAt = now;
    user.createdByUserId = actorUserId;

    Credential credential = hashResult.value();
    credential.userId = user.id;

    auto insertResult = m_userRepo.insertUserWithCredential(user, credential);
    if (insertResult.isErr())
        return Result<User>::err(insertResult.errorCode(), insertResult.errorMessage());

    recordAudit(actorUserId, AuditEventType::UserCreated, QStringLiteral("User"), user.id);
    return insertResult;
}

Result<void> AuthService::resetUserPassword(const QString& actorUserId,
                                            const QString& targetUserId,
                                            const QString& newPassword,
                                            const QString& stepUpWindowId)
{
    auto authResult = authorizePrivilegedAction(actorUserId,
        Role::SecurityAdministrator, stepUpWindowId);
    if (authResult.isErr())
        return authResult;

    if (!Validation::isPasswordLengthValid(newPassword)) {
        return Result<void>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Password must be at least %1 characters")
                .arg(Validation::PasswordMinLength));
    }

    auto userResult = m_userRepo.findUserById(targetUserId);
    if (userResult.isErr())
        return Result<void>::err(userResult.errorCode(), userResult.errorMessage());

    auto hashResult = Argon2idHasher::hashPassword(newPassword);
    if (hashResult.isErr())
        return Result<void>::err(ErrorCode::InternalError, hashResult.errorMessage());

    Credential credential = hashResult.value();
    credential.userId = targetUserId;

    auto updateResult = m_userRepo.upsertCredential(credential);
    if (updateResult.isErr())
        return updateResult;

    recordAudit(actorUserId, AuditEventType::PasswordReset, QStringLiteral("User"), targetUserId);
    return Result<void>::ok();
}

Result<void> AuthService::changeUserRole(const QString& actorUserId,
                                          const QString& targetUserId,
                                          Role newRole,
                                          const QString& stepUpWindowId)
{
    auto authResult = authorizePrivilegedAction(actorUserId,
        Role::SecurityAdministrator, stepUpWindowId);
    if (authResult.isErr())
        return authResult;

    auto targetResult = m_userRepo.findUserById(targetUserId);
    if (targetResult.isErr())
        return Result<void>::err(targetResult.errorCode(), targetResult.errorMessage());

    auto updateResult = m_userRepo.updateUserRole(targetUserId, newRole);
    if (updateResult.isErr())
        return updateResult;

    recordAudit(actorUserId, AuditEventType::RoleChanged, QStringLiteral("User"), targetUserId);
    return Result<void>::ok();
}

Result<void> AuthService::unlockUser(const QString& actorUserId,
                                      const QString& targetUserId,
                                      const QString& stepUpWindowId)
{
    auto authResult = authorizePrivilegedAction(actorUserId,
        Role::SecurityAdministrator, stepUpWindowId);
    if (authResult.isErr())
        return authResult;

    auto targetResult = m_userRepo.findUserById(targetUserId);
    if (targetResult.isErr())
        return Result<void>::err(targetResult.errorCode(), targetResult.errorMessage());

    auto updateResult = m_userRepo.updateUserStatus(targetUserId, UserStatus::Active);
    if (updateResult.isErr())
        return updateResult;

    m_userRepo.clearLockoutRecord(targetResult.value().username);
    recordAudit(actorUserId, AuditEventType::UserUnlocked, QStringLiteral("User"), targetUserId);
    return Result<void>::ok();
}

Result<void> AuthService::deactivateUser(const QString& actorUserId,
                                          const QString& targetUserId,
                                          const QString& stepUpWindowId)
{
    auto authResult = authorizePrivilegedAction(actorUserId,
        Role::SecurityAdministrator, stepUpWindowId);
    if (authResult.isErr())
        return authResult;

    if (actorUserId == targetUserId)
        return Result<void>::err(ErrorCode::ValidationFailed,
            QStringLiteral("Security administrator cannot deactivate their own account"));

    auto targetResult = m_userRepo.findUserById(targetUserId);
    if (targetResult.isErr())
        return Result<void>::err(targetResult.errorCode(), targetResult.errorMessage());

    auto updateResult = m_userRepo.updateUserStatus(targetUserId, UserStatus::Deactivated);
    if (updateResult.isErr())
        return updateResult;

    recordAudit(actorUserId, AuditEventType::UserDeactivated, QStringLiteral("User"), targetUserId);
    return Result<void>::ok();
}

Result<CaptchaState> AuthService::generateCaptcha(const QString& username)
{
    CaptchaChallenge challenge = CaptchaGenerator::generate();

    QDateTime now = QDateTime::currentDateTimeUtc();
    CaptchaState state;
    state.username      = username;
    state.challengeId   = challenge.challengeId;
    state.answerHashHex = challenge.answerHashHex;
    state.issuedAt      = now;
    state.expiresAt     = now.addSecs(Validation::CaptchaCooldownSeconds);
    state.solveAttempts = 0;
    state.solved        = false;

    auto result = m_userRepo.upsertCaptchaState(state);
    if (result.isErr())
        return Result<CaptchaState>::err(result.errorCode(), result.errorMessage());

    recordAudit(QStringLiteral("system"), AuditEventType::CaptchaChallenge,
                QStringLiteral("User"), username);

    return Result<CaptchaState>::ok(std::move(state));
}

void AuthService::recordAudit(const QString& actorUserId, AuditEventType eventType,
                               const QString& entityType, const QString& entityId)
{
    auto headResult = m_auditRepo.getChainHeadHash();
    QString prevHash = headResult.isOk() ? headResult.value() : QString();

    AuditEntry entry;
    entry.id                = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.timestamp         = QDateTime::currentDateTimeUtc();
    entry.actorUserId       = actorUserId;
    entry.eventType         = eventType;
    entry.entityType        = entityType;
    entry.entityId          = entityId;
    entry.beforePayloadJson = QStringLiteral("{}");
    entry.afterPayloadJson  = QStringLiteral("{}");
    entry.previousEntryHash = prevHash;
    entry.entryHash         = HashChain::computeEntryHash(entry);

    m_auditRepo.insertEntry(entry);
}

Result<CaptchaChallenge> AuthService::refreshCaptcha(const QString& username)
{
    auto challenge = CaptchaGenerator::generate();

    CaptchaState state;
    state.username      = username;
    state.challengeId   = challenge.challengeId;
    state.answerHashHex = challenge.answerHashHex;
    state.issuedAt      = QDateTime::currentDateTimeUtc();
    state.expiresAt     = state.issuedAt.addSecs(Validation::CaptchaCooldownSeconds);
    state.solveAttempts = 0;
    state.solved        = false;

    auto storeResult = m_userRepo.upsertCaptchaState(state);
    if (storeResult.isErr())
        return Result<CaptchaChallenge>::err(storeResult.errorCode(),
                                              storeResult.errorMessage());

    return Result<CaptchaChallenge>::ok(std::move(challenge));
}

bool AuthService::hasAnySecurityAdministrator() const
{
    auto result = m_userRepo.listUsers();
    if (result.isErr()) return false;
    for (const auto& user : result.value()) {
        if (user.role   == Role::SecurityAdministrator
         && user.status == UserStatus::Active)
            return true;
    }
    return false;
}

Result<UserSession> AuthService::bootstrapSecurityAdministrator(const QString& username,
                                                                 const QString& password)
{
    const QString normalizedUsername = username.trimmed();
    if (normalizedUsername.isEmpty()) {
        return Result<UserSession>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Username is required"));
    }

    if (!Validation::isPasswordLengthValid(password)) {
        return Result<UserSession>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Password must be at least %1 characters")
                .arg(Validation::PasswordMinLength));
    }

    if (hasAnySecurityAdministrator()) {
        return Result<UserSession>::err(
            ErrorCode::AuthorizationDenied,
            QStringLiteral("Bootstrap is disabled because an administrator already exists"));
    }

    auto existingUser = m_userRepo.findUserByUsername(normalizedUsername);
    if (existingUser.isOk()) {
        return Result<UserSession>::err(
            ErrorCode::AlreadyExists,
            QStringLiteral("Username already exists"));
    }
    if (existingUser.errorCode() != ErrorCode::NotFound) {
        return Result<UserSession>::err(existingUser.errorCode(), existingUser.errorMessage());
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();

    User user;
    user.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    user.username = normalizedUsername;
    user.role = Role::SecurityAdministrator;
    user.status = UserStatus::Active;
    user.createdAt = now;
    user.updatedAt = now;

    auto credResult = Argon2idHasher::hashPassword(password);
    if (credResult.isErr()) {
        return Result<UserSession>::err(
            ErrorCode::InternalError,
            credResult.errorMessage());
    }

    Credential credential = credResult.value();
    credential.userId = user.id;
    auto provisionResult = m_userRepo.insertUserWithCredential(user, credential);
    if (provisionResult.isErr()) {
        return Result<UserSession>::err(provisionResult.errorCode(), provisionResult.errorMessage());
    }

    recordAudit(user.id, AuditEventType::UserCreated, QStringLiteral("User"), user.id);

    Logger::instance().security(QStringLiteral("AuthService"),
        QStringLiteral("Initial security administrator provisioned"),
        {{QStringLiteral("user_id"), user.id}});

    return signIn(normalizedUsername, password);
}
