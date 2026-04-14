#pragma once
// Ed25519Signer.h — ProctorOps
// Ed25519 package signing for sync export.
// Uses OpenSSL 3.x EVP API. Signing is performed by the exporting desk using
// a desk-local private key whose public key is registered in the trust store.

#include "utils/Result.h"
#include <QByteArray>
#include <QString>

class Ed25519Signer {
public:
    Ed25519Signer() = delete;

    /// Sign a message with a DER-encoded Ed25519 private key.
    /// privateKeyDer: raw DER-encoded Ed25519 private key bytes.
    /// Returns the 64-byte detached signature.
    [[nodiscard]] static Result<QByteArray> sign(const QByteArray& message,
                                                   const QByteArray& privateKeyDer);

    /// Generate a new Ed25519 key pair.
    /// Returns {privateKeyDer, publicKeyDer} pair.
    [[nodiscard]] static Result<std::pair<QByteArray, QByteArray>> generateKeyPair();
};
