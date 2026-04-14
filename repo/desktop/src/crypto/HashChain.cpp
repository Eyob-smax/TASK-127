// HashChain.cpp — ProctorOps
// SHA-256 hash computation for audit chain entries.

#include "HashChain.h"
#include "models/CommonTypes.h"

#include <openssl/evp.h>
#include <QByteArray>

QString HashChain::computeEntryHash(const AuditEntry& entry)
{
    // Canonical serialization: pipe-delimited concatenation of all fields
    // that contribute to the entry's integrity.
    // Order matters and must never change once entries are written.
    QByteArray canonical;
    canonical.append(entry.id.toUtf8());
    canonical.append('|');
    canonical.append(entry.timestamp.toUTC().toString(Qt::ISODateWithMs).toUtf8());
    canonical.append('|');
    canonical.append(entry.actorUserId.toUtf8());
    canonical.append('|');
    canonical.append(auditEventTypeToString(entry.eventType).toUtf8());
    canonical.append('|');
    canonical.append(entry.entityType.toUtf8());
    canonical.append('|');
    canonical.append(entry.entityId.toUtf8());
    canonical.append('|');
    canonical.append(entry.beforePayloadJson.toUtf8());
    canonical.append('|');
    canonical.append(entry.afterPayloadJson.toUtf8());
    canonical.append('|');
    canonical.append(entry.previousEntryHash.toUtf8());

    return computeSha256(canonical);
}

QString HashChain::computeSha256(const QByteArray& data)
{
    unsigned char hash[32];
    unsigned int hashLen = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx)
        return {};

    bool ok = (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1) &&
              (EVP_DigestUpdate(ctx, data.constData(),
                                 static_cast<size_t>(data.size())) == 1) &&
              (EVP_DigestFinal_ex(ctx, hash, &hashLen) == 1);

    EVP_MD_CTX_free(ctx);

    if (!ok)
        return {};

    return QString::fromLatin1(
        QByteArray(reinterpret_cast<const char*>(hash),
                    static_cast<int>(hashLen)).toHex());
}
