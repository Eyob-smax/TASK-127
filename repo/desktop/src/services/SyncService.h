#pragma once
// SyncService.h — ProctorOps
// Offline desk-to-desk sync: export signed .proctorsync packages to LAN share or USB,
// import and verify packages from other desks, detect and expose conflict records,
// and manage the trusted signing key store.
//
// Sync packages are directory-based: a folder named <package_id> containing
//   manifest.json   — metadata, entity digests, Ed25519 signature
//   checkins.jsonl  — check-in attempts delta (since last watermark)
//   deductions.jsonl — deduction events delta
//   corrections.jsonl — correction records delta
//   content_edits.jsonl — question/KP changes delta
//
// The Ed25519 signature in manifest.json covers the manifest JSON body
// (excluding the "signature" field). Verification uses the public key
// registered in trusted_signing_keys for the declared signer_key_id.

#include "repositories/ISyncRepository.h"
#include "repositories/IAuditRepository.h"
#include "repositories/ICheckInRepository.h"
#include "services/AuditService.h"
#include "services/PackageVerifier.h"
#include "crypto/Ed25519Signer.h"
#include "utils/Result.h"
#include "models/Sync.h"

#include <QString>
#include <QByteArray>
#include <QList>
#include <QJsonObject>
#include <QSet>

class AuthService;

class SyncService {
public:
    SyncService(ISyncRepository& syncRepo,
                ICheckInRepository& checkInRepo,
                IAuditRepository& auditRepo,
                AuthService& authService,
                AuditService& auditService,
                PackageVerifier& verifier);

    // ── Export ─────────────────────────────────────────────────────────────────
    /// Build and write a sync package to destinationDir.
    /// signerPrivKeyDer: DER-encoded Ed25519 private key for signing.
    /// Collects all delta entities since the last export watermark for deskId.
    /// Records SyncExport audit event and updates the sync watermark.
    [[nodiscard]] Result<SyncPackage> exportPackage(const QString& destinationDir,
                                                     const QString& deskId,
                                                     const QString& signerKeyId,
                                                     const QByteArray& signerPrivKeyDer,
                                                     const QString& actorUserId,
                                                     const QString& stepUpWindowId);

    // ── Import ─────────────────────────────────────────────────────────────────
    /// Read and verify a sync package from packageDir.
    /// Verifies Ed25519 signature + SHA-256 entity digests.
    /// Detects conflicts (double deductions, mutable-record conflicts).
    /// Non-conflicting records are applied; conflicting records are stored as ConflictRecord.
    /// Returns the SyncPackage record (status Verified or Partial).
    [[nodiscard]] Result<SyncPackage> importPackage(const QString& packageDir,
                                                     const QString& actorUserId);

    // ── Conflict resolution ────────────────────────────────────────────────────
    /// Resolve a pending conflict record. Requires SecurityAdministrator role
    /// and a valid step-up window (stepUpWindowId consumed by this call).
    [[nodiscard]] Result<void> resolveConflict(const QString& conflictId,
                                                ConflictStatus resolution,
                                                const QString& actorUserId,
                                                const QString& stepUpWindowId);

    // ── Queries ────────────────────────────────────────────────────────────────
    [[nodiscard]] Result<QList<SyncPackage>>    listPackages(const QString& actorUserId);
    [[nodiscard]] Result<QList<ConflictRecord>> listPendingConflicts(const QString& packageId,
                                                                       const QString& actorUserId);

    // ── Signing key management (SecurityAdministrator + step-up required) ──────
    [[nodiscard]] Result<TrustedSigningKey> importSigningKey(
                                                const QString& label,
                                                const QString& publicKeyDerHex,
                                                const QDateTime& expiresAt,
                                                const QString& actorUserId,
                                                const QString& stepUpWindowId);

    [[nodiscard]] Result<void>              revokeSigningKey(
                                                const QString& keyId,
                                                const QString& actorUserId,
                                                const QString& stepUpWindowId);

    [[nodiscard]] Result<QList<TrustedSigningKey>> listSigningKeys(const QString& actorUserId);

private:
    struct WrittenEntityFile {
        QString sha256Hex;
        int recordCount = 0;
    };

    struct ConflictDetectionOutcome {
        int conflictCount = 0;
        QSet<QString> conflictedEntityIds;
    };

    ISyncRepository&    m_syncRepo;
    ICheckInRepository& m_checkInRepo;
    IAuditRepository&   m_auditRepo;
    AuthService&        m_authService;
    AuditService&       m_auditService;
    PackageVerifier&    m_verifier;

    // Write entity JSONL files and return the SHA-256 hex of each file.
    Result<WrittenEntityFile> writeDeductionsDelta(const QString& dir, const QDateTime& sinceWatermark);
    Result<WrittenEntityFile> writeCorrectionsDelta(const QString& dir, const QDateTime& sinceWatermark);
    Result<QString> resolvePackagePath(const QString& packageRoot, const QString& relativePath) const;

    // Detect and record conflicts for a given check-in entity.
    Result<ConflictDetectionOutcome> detectAndRecordConflicts(const QString& packageId,
                                                              const QString& entityType,
                                                              const QList<QJsonObject>& incomingRecords);
};
