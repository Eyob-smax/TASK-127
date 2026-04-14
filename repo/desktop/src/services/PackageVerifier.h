#pragma once
// PackageVerifier.h — ProctorOps
// Unified package signature and digest verification for sync and update packages.
// Looks up signing keys in the trust store, checks revocation/expiry, and
// delegates to Ed25519Verifier for cryptographic verification.

#include "repositories/ISyncRepository.h"
#include "models/Sync.h"
#include "utils/Result.h"

#include <QByteArray>
#include <QString>
#include <QVector>

class PackageVerifier {
public:
    explicit PackageVerifier(ISyncRepository& syncRepo);

    /// Verify an Ed25519 detached signature over manifest data.
    /// Looks up the signer key in the trust store, checks it is not revoked or expired.
    [[nodiscard]] Result<bool> verifyPackageSignature(const QByteArray& manifestData,
                                                       const QByteArray& signature,
                                                       const QString& signerKeyId);

    /// Verify the SHA-256 digest of a file on disk.
    [[nodiscard]] Result<bool> verifyFileDigest(const QString& filePath,
                                                 const QString& expectedSha256Hex);

    /// Verify all entity file digests within a package directory.
    [[nodiscard]] Result<void> verifyAllEntities(const QString& packageDir,
                                                  const QList<SyncPackageEntity>& entities);

private:
    ISyncRepository& m_syncRepo;
};
