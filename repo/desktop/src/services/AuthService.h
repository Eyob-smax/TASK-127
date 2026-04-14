#pragma once
// AuthService.h — ProctorOps
// Sign-in, lockout, CAPTCHA, session management, RBAC enforcement,
// step-up verification, and console lock/unlock.

#include "repositories/IUserRepository.h"
#include "repositories/IAuditRepository.h"
#include "utils/CaptchaGenerator.h"
#include "utils/Result.h"
#include "models/User.h"
#include "models/CommonTypes.h"

class AuthService {
public:
    AuthService(IUserRepository& userRepo, IAuditRepository& auditRepo);

    /// Full sign-in flow: lockout check → CAPTCHA check → password verify →
    /// session creation → audit event.
    [[nodiscard]] Result<UserSession> signIn(const QString& username,
                                              const QString& password,
                                              const QString& captchaAnswer = {});

    /// Sign out: deactivate session, record audit event.
    [[nodiscard]] Result<void> signOut(const QString& sessionToken);

    /// Request step-up re-authentication for a sensitive action.
    /// Creates a 2-minute window if password is valid.
    [[nodiscard]] Result<StepUpWindow> initiateStepUp(const QString& sessionToken,
                                                       const QString& password);

    /// Consume a step-up window (marks it as used). Fails if expired or already consumed.
    [[nodiscard]] Result<void> consumeStepUp(const QString& stepUpId);

    /// Lock the console (Ctrl+L). Requires an active session.
    [[nodiscard]] Result<void> lockConsole(const QString& sessionToken);

    /// Unlock the console. Requires password re-entry.
    [[nodiscard]] Result<UserSession> unlockConsole(const QString& sessionToken,
                                                     const QString& password);

    /// Check if a role has sufficient permission for the required level.
    [[nodiscard]] static bool hasPermission(Role userRole, Role requiredRole);

    /// Enforce role requirement. Returns AuthorizationDenied if insufficient.
    [[nodiscard]] Result<void> requireRole(const QString& sessionToken, Role requiredRole);

    /// Enforce role requirement for an active actor user id.
    [[nodiscard]] Result<void> requireRoleForActor(const QString& actorUserId,
                                                    Role requiredRole);

    /// Check for a valid unconsumed step-up window on the session's user.
    /// Returns StepUpRequired if none found.
    [[nodiscard]] Result<void> requireStepUp(const QString& sessionToken);

    /// Validate active actor role and consume a matching step-up window.
    [[nodiscard]] Result<void> authorizePrivilegedAction(const QString& actorUserId,
                                                          Role requiredRole,
                                                          const QString& stepUpWindowId);

    /// Security administration queries and mutations.
    [[nodiscard]] Result<QList<User>> listUsers(const QString& actorUserId);
    [[nodiscard]] Result<User> createUser(const QString& actorUserId,
                                           const QString& username,
                                           const QString& password,
                                           Role role,
                                           const QString& stepUpWindowId);
    [[nodiscard]] Result<void> resetUserPassword(const QString& actorUserId,
                                                  const QString& targetUserId,
                                                  const QString& newPassword,
                                                  const QString& stepUpWindowId);
    [[nodiscard]] Result<void> changeUserRole(const QString& actorUserId,
                                               const QString& targetUserId,
                                               Role newRole,
                                               const QString& stepUpWindowId);
    [[nodiscard]] Result<void> unlockUser(const QString& actorUserId,
                                           const QString& targetUserId,
                                           const QString& stepUpWindowId);
    [[nodiscard]] Result<void> deactivateUser(const QString& actorUserId,
                                               const QString& targetUserId,
                                               const QString& stepUpWindowId);

    /// Generate a new CAPTCHA challenge for a username and store it.
    Result<CaptchaState> generateCaptcha(const QString& username);

    /// Generate a fresh CAPTCHA challenge, store the answer hash, and return the
    /// full CaptchaChallenge (including the rendered QImage) so the login window
    /// can display it without needing direct access to CaptchaGenerator.
    [[nodiscard]] Result<CaptchaChallenge> refreshCaptcha(const QString& username);

    /// True if at least one active SecurityAdministrator account exists.
    /// Used by the login window to detect first-run bootstrap mode.
    [[nodiscard]] bool hasAnySecurityAdministrator() const;

    /// Create the first SecurityAdministrator account and return an active session.
    /// Fails if an active security administrator already exists.
    [[nodiscard]] Result<UserSession> bootstrapSecurityAdministrator(
        const QString& username,
        const QString& password);

private:
    IUserRepository&  m_userRepo;
    IAuditRepository& m_auditRepo;

    [[nodiscard]] Result<User> requireActiveUser(const QString& userId);

    /// Record an audit event with the given parameters.
    void recordAudit(const QString& actorUserId, AuditEventType eventType,
                     const QString& entityType, const QString& entityId);
};
