// AesGcmCipher.cpp — ProctorOps
// AES-256-GCM field encryption with HKDF-SHA256 key derivation.

#include "AesGcmCipher.h"
#include "SecureRandom.h"
#include "utils/Validation.h"

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/err.h>

#include <memory>

namespace {

// RAII wrapper for OpenSSL EVP_CIPHER_CTX
struct EvpCipherCtxDeleter {
    void operator()(EVP_CIPHER_CTX* ctx) const { EVP_CIPHER_CTX_free(ctx); }
};
using EvpCipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, EvpCipherCtxDeleter>;

// RAII wrapper for EVP_KDF_CTX
struct EvpKdfCtxDeleter {
    void operator()(EVP_KDF_CTX* ctx) const { EVP_KDF_CTX_free(ctx); }
};
using EvpKdfCtxPtr = std::unique_ptr<EVP_KDF_CTX, EvpKdfCtxDeleter>;

QString opensslError()
{
    unsigned long err = ERR_get_error();
    if (err == 0)
        return QStringLiteral("Unknown OpenSSL error");
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

} // anonymous namespace

AesGcmCipher::AesGcmCipher(const QByteArray& masterKey)
    : m_masterKey(masterKey)
{
    Q_ASSERT(masterKey.size() == Validation::AesGcmKeyBytes);
}

Result<QByteArray> AesGcmCipher::deriveKey(const QByteArray& salt,
                                             const QByteArray& context) const
{
    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
    if (!kdf)
        return Result<QByteArray>::err(ErrorCode::EncryptionFailed,
            QStringLiteral("HKDF fetch failed: %1").arg(opensslError()));

    EvpKdfCtxPtr ctx(EVP_KDF_CTX_new(kdf));
    EVP_KDF_free(kdf);
    if (!ctx)
        return Result<QByteArray>::err(ErrorCode::EncryptionFailed,
            QStringLiteral("HKDF context creation failed"));

    QByteArray derivedKey(Validation::AesGcmKeyBytes, Qt::Uninitialized);

    OSSL_PARAM params[5];
    int idx = 0;
    params[idx++] = OSSL_PARAM_construct_utf8_string("digest",
        const_cast<char*>("SHA256"), 0);
    params[idx++] = OSSL_PARAM_construct_octet_string("key",
        const_cast<char*>(m_masterKey.constData()),
        static_cast<size_t>(m_masterKey.size()));
    params[idx++] = OSSL_PARAM_construct_octet_string("salt",
        const_cast<char*>(salt.constData()),
        static_cast<size_t>(salt.size()));
    if (!context.isEmpty()) {
        params[idx++] = OSSL_PARAM_construct_octet_string("info",
            const_cast<char*>(context.constData()),
            static_cast<size_t>(context.size()));
    }
    params[idx] = OSSL_PARAM_construct_end();

    if (EVP_KDF_derive(ctx.get(),
                        reinterpret_cast<unsigned char*>(derivedKey.data()),
                        static_cast<size_t>(derivedKey.size()),
                        params) <= 0) {
        return Result<QByteArray>::err(ErrorCode::EncryptionFailed,
            QStringLiteral("HKDF derivation failed: %1").arg(opensslError()));
    }

    return Result<QByteArray>::ok(std::move(derivedKey));
}

Result<QByteArray> AesGcmCipher::encrypt(const QString& plaintext,
                                           const QByteArray& context) const
{
    const QByteArray ptBytes = plaintext.toUtf8();
    const QByteArray salt = SecureRandom::generate(Validation::HkdfSaltBytes);
    const QByteArray nonce = SecureRandom::generate(Validation::AesGcmNonceBytes);

    auto keyResult = deriveKey(salt, context);
    if (keyResult.isErr())
        return Result<QByteArray>::err(keyResult.errorCode(), keyResult.errorMessage());

    const QByteArray dek = std::move(keyResult).value();

    EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new());
    if (!ctx)
        return Result<QByteArray>::err(ErrorCode::EncryptionFailed,
            QStringLiteral("EVP_CIPHER_CTX_new failed"));

    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
        return Result<QByteArray>::err(ErrorCode::EncryptionFailed,
            QStringLiteral("EncryptInit failed: %1").arg(opensslError()));

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN,
                             Validation::AesGcmNonceBytes, nullptr) != 1)
        return Result<QByteArray>::err(ErrorCode::EncryptionFailed,
            QStringLiteral("Set IV length failed"));

    if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr,
                            reinterpret_cast<const unsigned char*>(dek.constData()),
                            reinterpret_cast<const unsigned char*>(nonce.constData())) != 1)
        return Result<QByteArray>::err(ErrorCode::EncryptionFailed,
            QStringLiteral("EncryptInit key/nonce failed"));

    QByteArray ciphertext(ptBytes.size() + EVP_MAX_BLOCK_LENGTH, Qt::Uninitialized);
    int outLen = 0;

    if (EVP_EncryptUpdate(ctx.get(),
                           reinterpret_cast<unsigned char*>(ciphertext.data()),
                           &outLen,
                           reinterpret_cast<const unsigned char*>(ptBytes.constData()),
                           ptBytes.size()) != 1) {
        return Result<QByteArray>::err(ErrorCode::EncryptionFailed,
            QStringLiteral("EncryptUpdate failed"));
    }

    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx.get(),
                              reinterpret_cast<unsigned char*>(ciphertext.data()) + outLen,
                              &finalLen) != 1) {
        return Result<QByteArray>::err(ErrorCode::EncryptionFailed,
            QStringLiteral("EncryptFinal failed"));
    }
    ciphertext.resize(outLen + finalLen);

    QByteArray tag(Validation::AesGcmTagBytes, Qt::Uninitialized);
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG,
                             Validation::AesGcmTagBytes,
                             tag.data()) != 1) {
        return Result<QByteArray>::err(ErrorCode::EncryptionFailed,
            QStringLiteral("Get GCM tag failed"));
    }

    // Assemble: [version:1][salt:16][nonce:12][ciphertext][tag:16]
    QByteArray result;
    result.reserve(1 + salt.size() + nonce.size() + ciphertext.size() + tag.size());
    result.append(static_cast<char>(CurrentVersion));
    result.append(salt);
    result.append(nonce);
    result.append(ciphertext);
    result.append(tag);

    return Result<QByteArray>::ok(std::move(result));
}

Result<QString> AesGcmCipher::decrypt(const QByteArray& blob,
                                        const QByteArray& context) const
{
    // Minimum size: version(1) + salt(16) + nonce(12) + tag(16) = 45 bytes (0 plaintext)
    const int headerSize = 1 + Validation::HkdfSaltBytes + Validation::AesGcmNonceBytes;
    const int minSize = headerSize + Validation::AesGcmTagBytes;

    if (blob.size() < minSize)
        return Result<QString>::err(ErrorCode::DecryptionFailed,
            QStringLiteral("Ciphertext too short"));

    int offset = 0;
    const quint8 version = static_cast<quint8>(blob[offset++]);
    if (version != CurrentVersion)
        return Result<QString>::err(ErrorCode::DecryptionFailed,
            QStringLiteral("Unsupported ciphertext version: %1").arg(version));

    const QByteArray salt = blob.mid(offset, Validation::HkdfSaltBytes);
    offset += Validation::HkdfSaltBytes;

    const QByteArray nonce = blob.mid(offset, Validation::AesGcmNonceBytes);
    offset += Validation::AesGcmNonceBytes;

    const int ctLen = blob.size() - offset - Validation::AesGcmTagBytes;
    const QByteArray ciphertext = blob.mid(offset, ctLen);
    offset += ctLen;

    const QByteArray tag = blob.mid(offset, Validation::AesGcmTagBytes);

    auto keyResult = deriveKey(salt, context);
    if (keyResult.isErr())
        return Result<QString>::err(keyResult.errorCode(), keyResult.errorMessage());

    const QByteArray dek = std::move(keyResult).value();

    EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new());
    if (!ctx)
        return Result<QString>::err(ErrorCode::DecryptionFailed,
            QStringLiteral("EVP_CIPHER_CTX_new failed"));

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
        return Result<QString>::err(ErrorCode::DecryptionFailed,
            QStringLiteral("DecryptInit failed"));

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN,
                             Validation::AesGcmNonceBytes, nullptr) != 1)
        return Result<QString>::err(ErrorCode::DecryptionFailed,
            QStringLiteral("Set IV length failed"));

    if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr,
                            reinterpret_cast<const unsigned char*>(dek.constData()),
                            reinterpret_cast<const unsigned char*>(nonce.constData())) != 1)
        return Result<QString>::err(ErrorCode::DecryptionFailed,
            QStringLiteral("DecryptInit key/nonce failed"));

    QByteArray plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH, Qt::Uninitialized);
    int outLen = 0;

    if (EVP_DecryptUpdate(ctx.get(),
                           reinterpret_cast<unsigned char*>(plaintext.data()),
                           &outLen,
                           reinterpret_cast<const unsigned char*>(ciphertext.constData()),
                           ciphertext.size()) != 1) {
        return Result<QString>::err(ErrorCode::DecryptionFailed,
            QStringLiteral("DecryptUpdate failed"));
    }

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG,
                             Validation::AesGcmTagBytes,
                             const_cast<char*>(tag.constData())) != 1) {
        return Result<QString>::err(ErrorCode::DecryptionFailed,
            QStringLiteral("Set GCM tag failed"));
    }

    int finalLen = 0;
    if (EVP_DecryptFinal_ex(ctx.get(),
                              reinterpret_cast<unsigned char*>(plaintext.data()) + outLen,
                              &finalLen) != 1) {
        return Result<QString>::err(ErrorCode::DecryptionFailed,
            QStringLiteral("Authentication tag verification failed — data may be tampered"));
    }
    plaintext.resize(outLen + finalLen);

    return Result<QString>::ok(QString::fromUtf8(plaintext));
}
