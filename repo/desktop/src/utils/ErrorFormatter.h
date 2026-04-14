#pragma once
// ErrorFormatter.h — ProctorOps
// Converts Result<T> error codes into user-facing dialog messages and form hints.
// Provides classification helpers for security-related and step-up errors.

#include "utils/Result.h"
#include <QString>

class ErrorFormatter {
public:
    ErrorFormatter() = delete;

    /// Map an ErrorCode to a human-readable message suitable for dialog display.
    [[nodiscard]] static QString toUserMessage(ErrorCode code,
                                                const QString& detail = {});

    /// Map an ErrorCode to a short hint suitable for form field validation.
    [[nodiscard]] static QString toFormHint(ErrorCode code);

    /// Returns true if the error code relates to authentication, authorization,
    /// or cryptographic operations.
    [[nodiscard]] static bool isSecurityError(ErrorCode code);

    /// Returns true if the error indicates that step-up verification is required.
    [[nodiscard]] static bool requiresStepUp(ErrorCode code);
};
