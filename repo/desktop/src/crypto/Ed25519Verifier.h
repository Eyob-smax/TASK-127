#pragma once
// Ed25519Verifier.h — ProctorOps
// Detached Ed25519 signature verification for sync and update packages.
// Uses OpenSSL 3.x EVP API. Only verification — signing is done by the
// exporting desk or the build pipeline, never by the consuming desk.

#include "utils/Result.h"
#include <QByteArray>
#include <QString>

class Ed25519Verifier {
public:
    Ed25519Verifier() = delete;

    /// Verify a detached Ed25519 signature over message data.
    /// publicKeyDer: raw DER-encoded Ed25519 public key bytes.
    /// Returns true if the signature is valid, false if invalid.
    /// Returns an error result only on internal/crypto failure.
    [[nodiscard]] static Result<bool> verify(const QByteArray& message,
                                              const QByteArray& signature,
                                              const QByteArray& publicKeyDer);

    /// Compute SHA-256 fingerprint of a DER-encoded public key.
    /// Returns lowercase hex string.
    [[nodiscard]] static Result<QString> computeFingerprint(const QByteArray& publicKeyDer);
};
