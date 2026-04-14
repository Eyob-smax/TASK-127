// PackageVerifier.cpp — ProctorOps
// Package signature and digest verification implementation.

#include "PackageVerifier.h"
#include "crypto/Ed25519Verifier.h"
#include "crypto/HashChain.h"
#include "utils/Logger.h"

#include <QDir>
#include <QFile>
#include <QDateTime>

PackageVerifier::PackageVerifier(ISyncRepository& syncRepo)
    : m_syncRepo(syncRepo)
{
}

Result<bool> PackageVerifier::verifyPackageSignature(const QByteArray& manifestData,
                                                       const QByteArray& signature,
                                                       const QString& signerKeyId)
{
    // Look up the signing key in the trust store
    auto keyResult = m_syncRepo.findSigningKeyById(signerKeyId);
    if (keyResult.isErr()) {
        Logger::instance().security(QStringLiteral("PackageVerifier"),
            QStringLiteral("Signing key not found in trust store"),
            {{QStringLiteral("keyId"), signerKeyId}});
        return Result<bool>::err(ErrorCode::TrustStoreMiss,
            QStringLiteral("Signing key not found: %1").arg(signerKeyId));
    }

    const TrustedSigningKey& key = keyResult.value();

    // Check revocation
    if (key.revoked) {
        Logger::instance().security(QStringLiteral("PackageVerifier"),
            QStringLiteral("Signing key is revoked"),
            {{QStringLiteral("keyId"), signerKeyId},
             {QStringLiteral("label"), key.label}});
        return Result<bool>::err(ErrorCode::TrustStoreMiss,
            QStringLiteral("Signing key is revoked: %1").arg(key.label));
    }

    // Check expiry
    if (!key.expiresAt.isNull()) {
        QDateTime now = QDateTime::currentDateTimeUtc();
        if (now > key.expiresAt) {
            Logger::instance().security(QStringLiteral("PackageVerifier"),
                QStringLiteral("Signing key has expired"),
                {{QStringLiteral("keyId"), signerKeyId},
                 {QStringLiteral("expiresAt"), key.expiresAt.toString(Qt::ISODateWithMs)}});
            return Result<bool>::err(ErrorCode::TrustStoreMiss,
                QStringLiteral("Signing key has expired: %1").arg(key.label));
        }
    }

    // Decode the DER public key from hex
    QByteArray publicKeyDer = QByteArray::fromHex(key.publicKeyDerHex.toLatin1());

    // Verify the signature
    auto verifyResult = Ed25519Verifier::verify(manifestData, signature, publicKeyDer);
    if (verifyResult.isErr())
        return Result<bool>::err(verifyResult.errorCode(), verifyResult.errorMessage());

    if (!verifyResult.value()) {
        Logger::instance().security(QStringLiteral("PackageVerifier"),
            QStringLiteral("Package signature verification failed"),
            {{QStringLiteral("keyId"), signerKeyId}});
    }

    return verifyResult;
}

Result<bool> PackageVerifier::verifyFileDigest(const QString& filePath,
                                                 const QString& expectedSha256Hex)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return Result<bool>::err(ErrorCode::PackageCorrupt,
            QStringLiteral("Cannot open file: %1").arg(filePath));

    QByteArray contents = file.readAll();
    file.close();

    QString computed = HashChain::computeSha256(contents);
    bool match = (computed == expectedSha256Hex);

    if (!match) {
        Logger::instance().security(QStringLiteral("PackageVerifier"),
            QStringLiteral("File digest mismatch"),
            {{QStringLiteral("file"), filePath},
             {QStringLiteral("expected"), expectedSha256Hex},
             {QStringLiteral("computed"), computed}});
    }

    return Result<bool>::ok(match);
}

Result<void> PackageVerifier::verifyAllEntities(const QString& packageDir,
                                                  const QList<SyncPackageEntity>& entities)
{
    QDir dir(packageDir);

    for (const SyncPackageEntity& entity : entities) {
        QString fullPath = dir.filePath(entity.filePath);

        auto digestResult = verifyFileDigest(fullPath, entity.sha256Hex);
        if (digestResult.isErr())
            return Result<void>::err(digestResult.errorCode(), digestResult.errorMessage());

        if (!digestResult.value()) {
            return Result<void>::err(ErrorCode::PackageCorrupt,
                QStringLiteral("Digest mismatch for entity: %1 (%2)")
                    .arg(entity.entityType, entity.filePath));
        }
    }

    Logger::instance().info(QStringLiteral("PackageVerifier"),
        QStringLiteral("All entity digests verified"),
        {{QStringLiteral("entityCount"), static_cast<int>(entities.size())}});

    return Result<void>::ok();
}
