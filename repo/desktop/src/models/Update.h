#pragma once
// Update.h — ProctorOps
// Domain models for the offline update/rollback subsystem.
//
// Override note: delivery does not require a signed .msi artifact.
// The update domain model, signature verification, staging, and rollback logic
// are all fully in scope. See docs/design.md §2.

#include <QString>
#include <QDateTime>
#include <QList>

// ── UpdatePackageStatus ───────────────────────────────────────────────────────
enum class UpdatePackageStatus {
    Staged,      // imported and staged; awaiting operator decision to apply or cancel
    Applied,     // successfully applied; system is running this version
    RolledBack,  // was applied; subsequently rolled back to a prior version
    Rejected,    // signature or digest verification failed; package refused
    Cancelled,   // staged but operator chose not to apply
};

// ── UpdatePackage ─────────────────────────────────────────────────────────────
struct UpdatePackage {
    QString              id;               // package_id from update-manifest.json
    QString              version;          // e.g. "1.2.0"
    QString              targetPlatform;   // e.g. "windows-x86_64"
    QString              description;
    QString              signerKeyId;      // must exist in trusted_signing_keys
    bool                 signatureValid;
    QString              stagedPath;       // temp directory for staged components
    UpdatePackageStatus  status;
    QDateTime            importedAt;
    QString              importedByUserId;
};

// ── UpdateComponent ──────────────────────────────────────────────────────────
// Each binary or data file within a .proctorpkg package.
struct UpdateComponent {
    QString  packageId;
    QString  name;              // e.g. "proctorops.exe"
    QString  version;
    QString  sha256Hex;         // expected digest from manifest
    QString  componentFilePath; // path within the staged directory
};

// ── InstallHistoryEntry ──────────────────────────────────────────────────────
// Snapshot taken before each update is applied, enabling rollback.
struct InstallHistoryEntry {
    QString   id;
    QString   packageId;
    QString   fromVersion;         // version before this update
    QString   toVersion;           // version this update brings
    QDateTime appliedAt;
    QString   appliedByUserId;
    QString   snapshotPayloadJson; // serialized pre-update component state for rollback
};

// ── RollbackRecord ────────────────────────────────────────────────────────────
// Written when an operator rolls back to a prior install history entry.
struct RollbackRecord {
    QString   id;
    QString   installHistoryId;    // the history entry rolled back to
    QString   fromVersion;         // version at time of rollback
    QString   toVersion;           // version restored by rollback
    QString   rationale;
    QDateTime rolledBackAt;
    QString   rolledBackByUserId;
};
