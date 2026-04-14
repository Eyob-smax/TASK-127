#pragma once
// IKeyStore.h — ProctorOps
// Pure interface for master key retrieval and rotation.
// Concrete implementations use OS-backed secure storage (DPAPI on Windows)
// or a file-based fallback for Docker/test environments.

#include "utils/Result.h"
#include <QByteArray>
#include <QString>

class IKeyStore {
public:
    virtual ~IKeyStore() = default;

    /// Retrieve the current master key (32 bytes for AES-256).
    /// If no key exists, the implementation should generate and persist one.
    [[nodiscard]] virtual Result<QByteArray> getMasterKey() = 0;

    /// Rotate to a new master key. The old key may be retained for
    /// re-encryption purposes during key rotation jobs.
    [[nodiscard]] virtual Result<void> rotateMasterKey(const QByteArray& newKey) = 0;

    /// Store a protected secret blob by logical name.
    [[nodiscard]] virtual Result<void> storeSecret(const QString& keyName,
                                                   const QByteArray& secretData) = 0;

    /// Retrieve a protected secret blob by logical name.
    [[nodiscard]] virtual Result<QByteArray> getSecret(const QString& keyName) const = 0;
};
