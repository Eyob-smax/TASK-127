#pragma once
// MaskingPolicy.h — ProctorOps
// Centralized masking rules for PII fields.
// Default display shows only the last 4 digits; full reveal requires step-up verification.

#include <QString>

class MaskingPolicy {
public:
    MaskingPolicy() = delete;

    /// Mask a mobile number: "(555) 123-4567" → "(***) ***-4567"
    [[nodiscard]] static QString maskMobile(const QString& decrypted);

    /// Mask a barcode: "ABCDE12345678" → "*********5678"
    [[nodiscard]] static QString maskBarcode(const QString& decrypted);

    /// Mask a name: "John Smith" → "J*** S****"
    [[nodiscard]] static QString maskName(const QString& decrypted);

    /// Generic mask: show only last `visibleSuffix` characters.
    [[nodiscard]] static QString maskGeneric(const QString& value, int visibleSuffix = 4);

    /// Full field reveal always requires step-up verification.
    [[nodiscard]] static bool requiresStepUp();
};
