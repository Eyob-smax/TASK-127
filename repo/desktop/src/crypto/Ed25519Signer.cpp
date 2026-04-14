// Ed25519Signer.cpp — ProctorOps

#include "Ed25519Signer.h"

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/x509.h>

// Helper: get OpenSSL error string
static QString opensslError()
{
    char buf[256];
    ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
    return QString::fromLatin1(buf);
}

Result<QByteArray> Ed25519Signer::sign(const QByteArray& message,
                                        const QByteArray& privateKeyDer)
{
    // Load private key from DER
    const unsigned char* derPtr = reinterpret_cast<const unsigned char*>(privateKeyDer.constData());
    EVP_PKEY* pkey = d2i_PrivateKey(EVP_PKEY_ED25519, nullptr, &derPtr,
                                     static_cast<long>(privateKeyDer.size()));
    if (!pkey)
        return Result<QByteArray>::err(ErrorCode::SignatureInvalid,
                                          QStringLiteral("Failed to load Ed25519 private key: ") + opensslError());

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return Result<QByteArray>::err(ErrorCode::InternalError, QStringLiteral("EVP_MD_CTX_new failed"));
    }

    if (EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, pkey) != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return Result<QByteArray>::err(ErrorCode::InternalError,
                                          QStringLiteral("EVP_DigestSignInit failed: ") + opensslError());
    }

    // Determine signature length
    size_t sigLen = 0;
    if (EVP_DigestSign(ctx, nullptr, &sigLen,
                        reinterpret_cast<const unsigned char*>(message.constData()),
                        static_cast<size_t>(message.size())) != 1)
    {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return Result<QByteArray>::err(ErrorCode::InternalError,
                                          QStringLiteral("EVP_DigestSign (length query) failed: ") + opensslError());
    }

    QByteArray signature(static_cast<int>(sigLen), '\0');
    if (EVP_DigestSign(ctx, reinterpret_cast<unsigned char*>(signature.data()), &sigLen,
                        reinterpret_cast<const unsigned char*>(message.constData()),
                        static_cast<size_t>(message.size())) != 1)
    {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return Result<QByteArray>::err(ErrorCode::InternalError,
                                          QStringLiteral("EVP_DigestSign failed: ") + opensslError());
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);

    signature.resize(static_cast<int>(sigLen));
    return Result<QByteArray>::ok(std::move(signature));
}

Result<std::pair<QByteArray, QByteArray>> Ed25519Signer::generateKeyPair()
{
    EVP_PKEY_CTX* kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    if (!kctx)
        return Result<std::pair<QByteArray, QByteArray>>::err(
            ErrorCode::InternalError, QStringLiteral("EVP_PKEY_CTX_new_id failed"));

    if (EVP_PKEY_keygen_init(kctx) != 1) {
        EVP_PKEY_CTX_free(kctx);
        return Result<std::pair<QByteArray, QByteArray>>::err(
            ErrorCode::InternalError, QStringLiteral("EVP_PKEY_keygen_init failed"));
    }

    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_keygen(kctx, &pkey) != 1) {
        EVP_PKEY_CTX_free(kctx);
        return Result<std::pair<QByteArray, QByteArray>>::err(
            ErrorCode::InternalError, QStringLiteral("EVP_PKEY_keygen failed"));
    }
    EVP_PKEY_CTX_free(kctx);

    // Serialize private key to DER
    int privLen = i2d_PrivateKey(pkey, nullptr);
    if (privLen <= 0) {
        EVP_PKEY_free(pkey);
        return Result<std::pair<QByteArray, QByteArray>>::err(
            ErrorCode::InternalError, QStringLiteral("i2d_PrivateKey failed"));
    }
    QByteArray privDer(privLen, '\0');
    unsigned char* privPtr = reinterpret_cast<unsigned char*>(privDer.data());
    i2d_PrivateKey(pkey, &privPtr);

    // Serialize public key to DER
    int pubLen = i2d_PUBKEY(pkey, nullptr);
    if (pubLen <= 0) {
        EVP_PKEY_free(pkey);
        return Result<std::pair<QByteArray, QByteArray>>::err(
            ErrorCode::InternalError, QStringLiteral("i2d_PUBKEY failed"));
    }
    QByteArray pubDer(pubLen, '\0');
    unsigned char* pubPtr = reinterpret_cast<unsigned char*>(pubDer.data());
    i2d_PUBKEY(pkey, &pubPtr);

    EVP_PKEY_free(pkey);
    return Result<std::pair<QByteArray, QByteArray>>::ok({std::move(privDer), std::move(pubDer)});
}
