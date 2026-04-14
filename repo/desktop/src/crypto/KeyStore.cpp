// KeyStore.cpp — ProctorOps
// Master key management with OS-backed protection.

#include "KeyStore.h"
#include "SecureRandom.h"
#include "utils/Validation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#ifdef _WIN32
#   include <windows.h>
#   include <dpapi.h>
#   pragma comment(lib, "crypt32.lib")
#else
#   include <sys/stat.h>
#endif

KeyStore::KeyStore(const QString& storageDir)
    : m_storageDir(storageDir)
    , m_masterKeyPath(QDir(storageDir).filePath(QStringLiteral("master.key")))
{
}

Result<QByteArray> KeyStore::getMasterKey()
{
    QFile keyFile(m_masterKeyPath);

    if (!keyFile.exists()) {
        // First run: generate and persist a new master key
        QByteArray newKey = SecureRandom::generate(Validation::AesGcmKeyBytes);

        auto protectResult = protectData(newKey);
        if (protectResult.isErr())
            return Result<QByteArray>::err(protectResult.errorCode(),
                protectResult.errorMessage());

        QDir().mkpath(m_storageDir);
        if (!keyFile.open(QIODevice::WriteOnly)) {
            return Result<QByteArray>::err(ErrorCode::InternalError,
                QStringLiteral("Cannot write key file"));
        }
        keyFile.write(std::move(protectResult).value());
        keyFile.close();

#ifndef _WIN32
        // Restrict file permissions to owner only
        chmod(m_masterKeyPath.toUtf8().constData(), 0600);
#endif

        return Result<QByteArray>::ok(std::move(newKey));
    }

    // Read existing protected key
    if (!keyFile.open(QIODevice::ReadOnly))
        return Result<QByteArray>::err(ErrorCode::KeyNotFound,
            QStringLiteral("Cannot read key file"));

    QByteArray protectedData = keyFile.readAll();
    keyFile.close();

    return unprotectData(protectedData);
}

Result<void> KeyStore::rotateMasterKey(const QByteArray& newKey)
{
    if (newKey.size() != Validation::AesGcmKeyBytes)
        return Result<void>::err(ErrorCode::ValidationFailed,
            QStringLiteral("Key must be %1 bytes").arg(Validation::AesGcmKeyBytes));

    auto protectResult = protectData(newKey);
    if (protectResult.isErr())
        return Result<void>::err(protectResult.errorCode(), protectResult.errorMessage());

    // Write to a temp file first, then rename for atomicity
    QString tempPath = m_masterKeyPath + QStringLiteral(".new");
    QFile tempFile(tempPath);

    QDir().mkpath(m_storageDir);
    if (!tempFile.open(QIODevice::WriteOnly))
        return Result<void>::err(ErrorCode::InternalError,
            QStringLiteral("Cannot write temp key file"));

    tempFile.write(std::move(protectResult).value());
    tempFile.close();

    // Backup current key before replacing
    QString backupPath = m_masterKeyPath + QStringLiteral(".prev");
    QFile::remove(backupPath);
    QFile::rename(m_masterKeyPath, backupPath);

    if (!QFile::rename(tempPath, m_masterKeyPath))
        return Result<void>::err(ErrorCode::InternalError,
            QStringLiteral("Failed to finalize key rotation"));

#ifndef _WIN32
    chmod(m_masterKeyPath.toUtf8().constData(), 0600);
#endif

    return Result<void>::ok();
}

Result<void> KeyStore::storeSecret(const QString& keyName, const QByteArray& secretData)
{
    if (keyName.trimmed().isEmpty()) {
        return Result<void>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Secret name is required"));
    }

    if (secretData.isEmpty()) {
        return Result<void>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Secret payload is empty"));
    }

    auto protectedData = protectData(secretData);
    if (protectedData.isErr())
        return Result<void>::err(protectedData.errorCode(), protectedData.errorMessage());

    const QString path = secretPath(keyName);
    QDir().mkpath(m_storageDir);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return Result<void>::err(
            ErrorCode::InternalError,
            QStringLiteral("Cannot write secret file"));
    }

    file.write(protectedData.value());
    file.close();

#ifndef _WIN32
    chmod(path.toUtf8().constData(), 0600);
#endif

    return Result<void>::ok();
}

Result<QByteArray> KeyStore::getSecret(const QString& keyName) const
{
    if (keyName.trimmed().isEmpty()) {
        return Result<QByteArray>::err(
            ErrorCode::ValidationFailed,
            QStringLiteral("Secret name is required"));
    }

    QFile file(secretPath(keyName));
    if (!file.exists()) {
        return Result<QByteArray>::err(
            ErrorCode::KeyNotFound,
            QStringLiteral("Secret not found"));
    }

    if (!file.open(QIODevice::ReadOnly)) {
        return Result<QByteArray>::err(
            ErrorCode::InternalError,
            QStringLiteral("Cannot read secret file"));
    }

    const QByteArray protectedData = file.readAll();
    file.close();
    return unprotectData(protectedData);
}

QString KeyStore::secretPath(const QString& keyName) const
{
    QString normalized = keyName.trimmed();
    normalized.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")),
                       QStringLiteral("_"));
    return QDir(m_storageDir).filePath(normalized + QStringLiteral(".secret"));
}

Result<QByteArray> KeyStore::protectData(const QByteArray& plainData) const
{
#ifdef _WIN32
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plainData.constData()));
    input.cbData = static_cast<DWORD>(plainData.size());

    DATA_BLOB output;
    if (!CryptProtectData(&input, L"ProctorOps Master Key",
                           nullptr, nullptr, nullptr,
                           CRYPTPROTECT_LOCAL_MACHINE, &output)) {
        return Result<QByteArray>::err(ErrorCode::EncryptionFailed,
            QStringLiteral("DPAPI CryptProtectData failed: %1")
                .arg(GetLastError()));
    }

    QByteArray result(reinterpret_cast<const char*>(output.pbData),
                       static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return Result<QByteArray>::ok(std::move(result));
#else
    // Linux/Docker fallback: XOR with a static entropy pad.
    // This is NOT equivalent to DPAPI security; it exists only to keep the
    // file format consistent and to avoid storing plaintext on disk.
    // In production (Windows), DPAPI provides real OS-level protection.
    static const QByteArray pad = QByteArray::fromHex(
        "a3b1c7d9e5f2081426375a6b9c0d1e2f"
        "3a4b5c6d7e8f90a1b2c3d4e5f6071829");
    QByteArray result(plainData.size(), Qt::Uninitialized);
    for (int i = 0; i < plainData.size(); ++i)
        result[i] = plainData[i] ^ pad[i % pad.size()];
    return Result<QByteArray>::ok(std::move(result));
#endif
}

Result<QByteArray> KeyStore::unprotectData(const QByteArray& protectedData) const
{
#ifdef _WIN32
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(protectedData.constData()));
    input.cbData = static_cast<DWORD>(protectedData.size());

    DATA_BLOB output;
    if (!CryptUnprotectData(&input, nullptr,
                              nullptr, nullptr, nullptr,
                              CRYPTPROTECT_LOCAL_MACHINE, &output)) {
        return Result<QByteArray>::err(ErrorCode::DecryptionFailed,
            QStringLiteral("DPAPI CryptUnprotectData failed: %1")
                .arg(GetLastError()));
    }

    QByteArray result(reinterpret_cast<const char*>(output.pbData),
                       static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return Result<QByteArray>::ok(std::move(result));
#else
    // Linux/Docker fallback: same XOR reversal
    static const QByteArray pad = QByteArray::fromHex(
        "a3b1c7d9e5f2081426375a6b9c0d1e2f"
        "3a4b5c6d7e8f90a1b2c3d4e5f6071829");
    QByteArray result(protectedData.size(), Qt::Uninitialized);
    for (int i = 0; i < protectedData.size(); ++i)
        result[i] = protectedData[i] ^ pad[i % pad.size()];
    return Result<QByteArray>::ok(std::move(result));
#endif
}
