#pragma once
// Argon2idHasher.h — ProctorOps
// Password hashing and verification using Argon2id.
// Parameters sourced from Validation.h constants — never hardcoded here.

#include "models/User.h"
#include "utils/Result.h"

class Argon2idHasher {
public:
    Argon2idHasher() = delete;

    /// Hash a plaintext password using Argon2id with Validation.h constants.
    /// Generates a fresh random salt via SecureRandom.
    /// Returns a populated Credential struct (userId left empty for caller to set).
    [[nodiscard]] static Result<Credential> hashPassword(const QString& password);

    /// Verify a plaintext password against an existing Credential record.
    /// Returns true if the password matches; false on mismatch.
    /// Returns an error result only on internal failure (not on mismatch).
    [[nodiscard]] static Result<bool> verifyPassword(const QString& password,
                                                      const Credential& credential);
};
