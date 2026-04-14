#pragma once
// SecureRandom.h — ProctorOps
// OS-backed cryptographically secure random byte generation.
// Uses CryptGenRandom on Windows, /dev/urandom on Linux (Docker builds).

#include <QByteArray>
#include <QString>

class SecureRandom {
public:
    SecureRandom() = delete;

    /// Generate `bytes` of cryptographically secure random data.
    [[nodiscard]] static QByteArray generate(int bytes);

    /// Generate `bytes` of random data and return as lowercase hex string.
    [[nodiscard]] static QString generateHex(int bytes);
};
