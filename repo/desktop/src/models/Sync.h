#pragma once
// Sync.h — ProctorOps
// Domain models for offline sync packages, conflict records, and trusted signing keys.
// Sync packages are signed .proctorsync bundles transported on LAN share or USB.

#include <QString>
#include <QDateTime>

// ── SyncPackageStatus ─────────────────────────────────────────────────────────
enum class SyncPackageStatus {
    Pending,   // exported locally; not yet imported on this desk
    Verified,  // Ed25519 signature and all entity digests verified
    Applied,   // all entities imported; no unresolved conflicts
    Partial,   // imported with unresolved conflicts pending review
    Rejected,  // signature or digest verification failed; package refused
};

// ── SyncPackage ───────────────────────────────────────────────────────────────
struct SyncPackage {
    QString          id;               // package_id from manifest.json
    QString          sourceDeskId;     // originating desk identifier
    QString          signerKeyId;      // key_id of the signing key
    QDateTime        exportedAt;       // UTC timestamp from manifest
    QDateTime        sinceWatermark;   // entities collected since this UTC timestamp
    SyncPackageStatus status;
    QString          packageFilePath;  // path where the .proctorsync file was read from
    QDateTime        importedAt;       // null until applied
    QString          importedByUserId;
};

// ── SyncPackageEntity ─────────────────────────────────────────────────────────
// Each entity file within a sync package, with its manifest digest for verification.
struct SyncPackageEntity {
    QString  packageId;
    QString  entityType;     // "checkins", "deductions", "corrections", "content_edits"
    QString  filePath;       // relative path within the zip archive
    QString  sha256Hex;      // expected SHA-256 digest from manifest
    int      recordCount;
    bool     verified;       // true if file digest matches manifest entry
    bool     applied;        // true once imported rows are materialized locally
    QDateTime appliedAt;     // null until applied
};

// ── ConflictType ──────────────────────────────────────────────────────────────
enum class ConflictType {
    DoubleDeduction,          // same member deducted on two desks in the same session
    MutableRecordConflict,    // same mutable entity updated independently on both desks
    DeleteConflict,           // entity deleted on one desk, modified on another
};

// ── ConflictStatus ────────────────────────────────────────────────────────────
enum class ConflictStatus {
    Pending,                  // detected; awaiting operator resolution
    ResolvedAcceptLocal,      // operator kept the local version
    ResolvedAcceptIncoming,   // operator accepted the incoming version
    ResolvedManualMerge,      // operator provided a merged payload
    Skipped,                  // skipped by operator (local version wins by default)
};

// ── ConflictRecord ────────────────────────────────────────────────────────────
struct ConflictRecord {
    QString        id;
    QString        packageId;
    ConflictType   type;
    QString        entityType;         // e.g. "DeductionEvent", "Question"
    QString        entityId;           // the conflicting entity's UUID
    QString        description;        // human-readable conflict summary
    ConflictStatus status;
    QString        incomingPayloadJson; // raw incoming entity payload
    QString        localPayloadJson;    // current local entity payload
    QDateTime      detectedAt;
    QString        resolvedByUserId;   // empty until resolved
    QDateTime      resolvedAt;         // null until resolved
    QString        resolutionActionType; // e.g. "CorrectionApplied"
    QString        resolutionActionId;   // e.g. correction request id
};

// ── TrustedSigningKey ─────────────────────────────────────────────────────────
// Ed25519 public keys trusted to sign sync packages and update packages.
// Managed by security administrators through the trust-store management UI.
struct TrustedSigningKey {
    QString   id;              // UUID
    QString   label;           // human-readable name for the key
    QString   publicKeyDerHex; // DER-encoded Ed25519 public key, hex-encoded
    QString   fingerprint;     // SHA-256 of DER bytes, hex (for display / confirmation)
    QDateTime importedAt;
    QString   importedByUserId;
    QDateTime expiresAt;       // null if no expiry
    bool      revoked;         // if true, reject packages signed with this key
};
