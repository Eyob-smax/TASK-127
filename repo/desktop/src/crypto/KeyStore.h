#pragma once
// KeyStore.h — ProctorOps
// Concrete key store implementation.
// Windows: uses DPAPI (CryptProtectData / CryptUnprotectData) for master key protection.
// Linux/Docker: uses a file-based fallback with restrictive permissions.
// Key material never appears in logs, error messages, or unprotected storage.

#include "IKeyStore.h"
#include <QString>

class KeyStore : public IKeyStore {
public:
    /// Construct with the path to the protected key storage directory.
    /// On Windows this is typically %LOCALAPPDATA%/ProctorOps/keys/
    /// In Docker this is a mounted volume with 0600 permissions.
    explicit KeyStore(const QString& storageDir);

    Result<QByteArray> getMasterKey() override;
    Result<void> rotateMasterKey(const QByteArray& newKey) override;
    Result<void> storeSecret(const QString& keyName, const QByteArray& secretData) override;
    Result<QByteArray> getSecret(const QString& keyName) const override;

private:
    QString m_storageDir;
    QString m_masterKeyPath;

    [[nodiscard]] QString secretPath(const QString& keyName) const;

    /// Protect key material using OS-backed encryption before writing to disk.
    [[nodiscard]] Result<QByteArray> protectData(const QByteArray& plainData) const;

    /// Unprotect key material read from disk.
    [[nodiscard]] Result<QByteArray> unprotectData(const QByteArray& protectedData) const;
};
