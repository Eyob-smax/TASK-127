#pragma once
// AesGcmCipher.h — ProctorOps
// AES-256-GCM field-level encryption and decryption.
// Uses HKDF-SHA256 to derive per-record data-encryption keys from a master key.
// Ciphertext format: [version:1][salt:16][nonce:12][ciphertext...][tag:16]

#include "utils/Result.h"
#include <QByteArray>
#include <QString>

class AesGcmCipher {
public:
    /// Construct with a 32-byte master key.
    explicit AesGcmCipher(const QByteArray& masterKey);

    /// Encrypt a plaintext string. The context parameter is used as HKDF info
    /// to bind the derived key to a specific purpose (e.g., "member.mobile").
    /// Returns versioned ciphertext blob.
    [[nodiscard]] Result<QByteArray> encrypt(const QString& plaintext,
                                              const QByteArray& context = {}) const;

    /// Decrypt a versioned ciphertext blob back to plaintext.
    [[nodiscard]] Result<QString> decrypt(const QByteArray& ciphertext,
                                           const QByteArray& context = {}) const;

private:
    QByteArray m_masterKey; // 32 bytes

    static constexpr quint8 CurrentVersion = 1;

    /// Derive a per-record data-encryption key from the master key using HKDF-SHA256.
    [[nodiscard]] Result<QByteArray> deriveKey(const QByteArray& salt,
                                                const QByteArray& context) const;
};
