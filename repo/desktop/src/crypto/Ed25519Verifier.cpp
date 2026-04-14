// Ed25519Verifier.cpp — ProctorOps
// Ed25519 detached signature verification via OpenSSL 3.x EVP API.

#include "Ed25519Verifier.h"

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/x509.h>

#include <memory>

namespace {

struct EvpPkeyDeleter {
    void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); }
};
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;

struct EvpMdCtxDeleter {
    void operator()(EVP_MD_CTX* ctx) const { EVP_MD_CTX_free(ctx); }
};
using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, EvpMdCtxDeleter>;

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

Result<bool> Ed25519Verifier::verify(const QByteArray& message,
                                      const QByteArray& signature,
                                      const QByteArray& publicKeyDer)
{
    // Ed25519 signatures are always 64 bytes
    if (signature.size() != 64)
        return Result<bool>::ok(false);

    // Decode DER-encoded public key
    const unsigned char* derPtr = reinterpret_cast<const unsigned char*>(publicKeyDer.constData());
    EvpPkeyPtr pkey(d2i_PUBKEY(nullptr, &derPtr, publicKeyDer.size()));
    if (!pkey)
        return Result<bool>::err(ErrorCode::SignatureInvalid,
            QStringLiteral("Failed to decode DER public key: %1").arg(opensslError()));

    // Verify key type is Ed25519
    if (EVP_PKEY_id(pkey.get()) != EVP_PKEY_ED25519)
        return Result<bool>::err(ErrorCode::SignatureInvalid,
            QStringLiteral("Public key is not Ed25519"));

    EvpMdCtxPtr mdCtx(EVP_MD_CTX_new());
    if (!mdCtx)
        return Result<bool>::err(ErrorCode::InternalError,
            QStringLiteral("EVP_MD_CTX_new failed"));

    if (EVP_DigestVerifyInit(mdCtx.get(), nullptr, nullptr, nullptr, pkey.get()) != 1)
        return Result<bool>::err(ErrorCode::InternalError,
            QStringLiteral("DigestVerifyInit failed: %1").arg(opensslError()));

    // Ed25519 uses one-shot verify (no Update step)
    int rc = EVP_DigestVerify(
        mdCtx.get(),
        reinterpret_cast<const unsigned char*>(signature.constData()),
        static_cast<size_t>(signature.size()),
        reinterpret_cast<const unsigned char*>(message.constData()),
        static_cast<size_t>(message.size()));

    if (rc == 1)
        return Result<bool>::ok(true);

    if (rc == 0)
        return Result<bool>::ok(false);

    // rc < 0 indicates an internal error
    return Result<bool>::err(ErrorCode::InternalError,
        QStringLiteral("DigestVerify error: %1").arg(opensslError()));
}

Result<QString> Ed25519Verifier::computeFingerprint(const QByteArray& publicKeyDer)
{
    unsigned char hash[32]; // SHA-256 output
    unsigned int hashLen = 0;

    EvpMdCtxPtr ctx(EVP_MD_CTX_new());
    if (!ctx)
        return Result<QString>::err(ErrorCode::InternalError,
            QStringLiteral("EVP_MD_CTX_new failed"));

    if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx.get(), publicKeyDer.constData(),
                          static_cast<size_t>(publicKeyDer.size())) != 1 ||
        EVP_DigestFinal_ex(ctx.get(), hash, &hashLen) != 1) {
        return Result<QString>::err(ErrorCode::InternalError,
            QStringLiteral("SHA-256 computation failed: %1").arg(opensslError()));
    }

    return Result<QString>::ok(
        QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(hash),
                                        static_cast<int>(hashLen)).toHex()));
}
