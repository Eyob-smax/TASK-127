#pragma once
// UpdateService.h — ProctorOps
// Offline update/rollback service for .proctorpkg packages.
//
// Override note: delivery does not require a signed .msi artifact.
// The full update domain model, signature verification, staging, install history,
// and rollback logic are in scope. See docs/design.md §2.
//
// Update flow:
//   1. Operator selects a .proctorpkg directory from disk (or LAN share/USB).
//   2. importPackage() reads the manifest, verifies the Ed25519 signature and all
//      component SHA-256 digests, stages metadata, and records status = Staged.
//   3. Operator reviews staged package details in UpdateWindow.
//   4. applyPackage() (SecurityAdministrator + step-up): snapshots current install
//      state, records InstallHistoryEntry, marks package Applied.
//   5. If the update causes problems, rollback() reverts to the selected
//      InstallHistoryEntry, records a RollbackRecord, marks the package RolledBack.
//
// All state transitions are audit-logged. The update content is never auto-applied.

#include "repositories/IUpdateRepository.h"
#include "repositories/ISyncRepository.h"
#include "services/AuditService.h"
#include "services/PackageVerifier.h"
#include "utils/Result.h"
#include "models/Update.h"

#include <QString>
#include <QList>

class AuthService;

class UpdateService {
public:
    UpdateService(IUpdateRepository& updateRepo,
                   ISyncRepository& syncRepo,
                   AuthService& authService,
                   PackageVerifier& verifier,
                   AuditService& auditService);

    // ── Import and stage ───────────────────────────────────────────────────────
    /// Read a .proctorpkg directory, verify signature and all component digests.
    /// Records status = Staged or Rejected. Does not apply any files.
    [[nodiscard]] Result<UpdatePackage> importPackage(const QString& pkgDir,
                                                       const QString& actorUserId);

    // ── Apply ──────────────────────────────────────────────────────────────────
    /// Apply a Staged package. Requires SecurityAdministrator + step-up.
    /// Snapshots current state → InstallHistoryEntry → marks Applied.
    [[nodiscard]] Result<void> applyPackage(const QString& packageId,
                                             const QString& currentVersion,
                                             const QString& actorUserId,
                                             const QString& stepUpWindowId);

    // ── Rollback ───────────────────────────────────────────────────────────────
    /// Roll back to a prior install history entry. Requires SecurityAdministrator + step-up.
    /// Records a RollbackRecord and marks the original package as RolledBack.
    [[nodiscard]] Result<RollbackRecord> rollback(const QString& installHistoryId,
                                                   const QString& rationale,
                                                   const QString& actorUserId,
                                                   const QString& stepUpWindowId);

    // ── Cancel ─────────────────────────────────────────────────────────────────
    /// Cancel a Staged package (operator opts not to apply).
    [[nodiscard]] Result<void> cancelPackage(const QString& packageId,
                                              const QString& actorUserId);

    // ── Queries ────────────────────────────────────────────────────────────────
    [[nodiscard]] Result<QList<UpdatePackage>>       listPackages(const QString& actorUserId);
    [[nodiscard]] Result<QList<InstallHistoryEntry>> listInstallHistory(const QString& actorUserId);
    [[nodiscard]] Result<QList<RollbackRecord>>      listRollbackRecords(const QString& actorUserId);

private:
    IUpdateRepository& m_updateRepo;
    ISyncRepository&   m_syncRepo;
    AuthService&       m_authService;
    PackageVerifier&   m_verifier;
    AuditService&      m_auditService;

    /// Build a snapshot of the current component state (for rollback purposes).
    QString buildComponentSnapshot(const QString& packageId);
    [[nodiscard]] Result<QString> resolvePackagePath(const QString& packageRoot,
                                                      const QString& relativePath) const;
};
