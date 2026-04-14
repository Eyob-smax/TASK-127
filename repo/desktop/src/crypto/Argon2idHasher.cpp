// Argon2idHasher.cpp — ProctorOps
// Argon2id password hashing and verification.

#include "Argon2idHasher.h"
#include "SecureRandom.h"
#include "utils/Validation.h"

#include <QDateTime>
#include <argon2.h>
#include <cstring>

Result<Credential> Argon2idHasher::hashPassword(const QString& password)
{
    if (!Validation::isPasswordLengthValid(password))
        return Result<Credential>::err(ErrorCode::ValidationFailed,
            QStringLiteral("Password must be at least %1 characters")
                .arg(Validation::PasswordMinLength));

    const QByteArray passwordUtf8 = password.toUtf8();
    const QByteArray salt = SecureRandom::generate(Validation::Argon2SaltLength);

    QByteArray hash(Validation::Argon2TagLength, Qt::Uninitialized);

    int rc = argon2id_hash_raw(
        Validation::Argon2TimeCost,
        Validation::Argon2MemoryCost,
        Validation::Argon2Parallelism,
        passwordUtf8.constData(),
        static_cast<size_t>(passwordUtf8.size()),
        salt.constData(),
        static_cast<size_t>(salt.size()),
        hash.data(),
        static_cast<size_t>(hash.size()));

    if (rc != ARGON2_OK)
        return Result<Credential>::err(ErrorCode::InternalError,
            QStringLiteral("Argon2id hashing failed: %1")
                .arg(QString::fromUtf8(argon2_error_message(rc))));

    Credential cred;
    cred.algorithm   = QStringLiteral("argon2id");
    cred.timeCost    = Validation::Argon2TimeCost;
    cred.memoryCost  = Validation::Argon2MemoryCost;
    cred.parallelism = Validation::Argon2Parallelism;
    cred.tagLength   = Validation::Argon2TagLength;
    cred.saltHex     = QString::fromLatin1(salt.toHex());
    cred.hashHex     = QString::fromLatin1(hash.toHex());
    cred.updatedAt   = QDateTime::currentDateTimeUtc();

    return Result<Credential>::ok(std::move(cred));
}

Result<bool> Argon2idHasher::verifyPassword(const QString& password,
                                             const Credential& credential)
{
    const QByteArray passwordUtf8 = password.toUtf8();
    const QByteArray salt = QByteArray::fromHex(credential.saltHex.toLatin1());
    const QByteArray storedHash = QByteArray::fromHex(credential.hashHex.toLatin1());

    QByteArray computed(credential.tagLength, Qt::Uninitialized);

    int rc = argon2id_hash_raw(
        credential.timeCost,
        credential.memoryCost,
        credential.parallelism,
        passwordUtf8.constData(),
        static_cast<size_t>(passwordUtf8.size()),
        salt.constData(),
        static_cast<size_t>(salt.size()),
        computed.data(),
        static_cast<size_t>(computed.size()));

    if (rc != ARGON2_OK)
        return Result<bool>::err(ErrorCode::InternalError,
            QStringLiteral("Argon2id verification failed: %1")
                .arg(QString::fromUtf8(argon2_error_message(rc))));

    // Constant-time comparison to prevent timing attacks
    if (computed.size() != storedHash.size())
        return Result<bool>::ok(false);

    volatile unsigned char result = 0;
    for (int i = 0; i < computed.size(); ++i)
        result |= static_cast<unsigned char>(computed[i]) ^
                   static_cast<unsigned char>(storedHash[i]);

    return Result<bool>::ok(result == 0);
}
