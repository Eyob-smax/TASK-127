// SecureRandom.cpp — ProctorOps
// OS-backed cryptographically secure random byte generation.

#include "SecureRandom.h"

#include <stdexcept>

#ifdef _WIN32
#   include <windows.h>
#   include <bcrypt.h>
#   pragma comment(lib, "bcrypt.lib")
#else
#   include <QFile>
#endif

QByteArray SecureRandom::generate(int bytes)
{
    if (bytes <= 0)
        return {};

    QByteArray buf(bytes, Qt::Uninitialized);

#ifdef _WIN32
    // Windows: BCryptGenRandom (preferred over legacy CryptGenRandom)
    NTSTATUS status = BCryptGenRandom(
        nullptr,
        reinterpret_cast<PUCHAR>(buf.data()),
        static_cast<ULONG>(bytes),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status))
        throw std::runtime_error("BCryptGenRandom failed");
#else
    // Linux / Docker: read from /dev/urandom
    QFile urandom(QStringLiteral("/dev/urandom"));
    if (!urandom.open(QIODevice::ReadOnly))
        throw std::runtime_error("Failed to open /dev/urandom");

    qint64 bytesRead = urandom.read(buf.data(), bytes);
    if (bytesRead != bytes)
        throw std::runtime_error("Short read from /dev/urandom");
#endif

    return buf;
}

QString SecureRandom::generateHex(int bytes)
{
    return QString::fromLatin1(generate(bytes).toHex());
}
