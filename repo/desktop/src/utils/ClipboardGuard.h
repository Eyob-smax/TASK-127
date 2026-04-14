#pragma once
// ClipboardGuard.h — ProctorOps
// Intercepts clipboard write operations to mask/redact PII before it reaches
// the OS clipboard. Prevents accidental PII leakage through copy-paste.

#include <QString>

class QClipboard;

class ClipboardGuard {
public:
    ClipboardGuard() = delete;

    /// Copy a masked version of the value to the clipboard (last 4 chars visible).
    static void copyMasked(const QString& value);

    /// Copy "[REDACTED]" to the clipboard instead of the actual value.
    static void copyRedacted(const QString& value);

    /// Generic masking: replace all but last `visibleSuffix` chars with '*'.
    [[nodiscard]] static QString maskValue(const QString& value, int visibleSuffix = 4);
};
