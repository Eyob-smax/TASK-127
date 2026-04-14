// MaskingPolicy.cpp — ProctorOps
// PII masking implementation.

#include "MaskingPolicy.h"

#include <QStringList>

QString MaskingPolicy::maskMobile(const QString& decrypted)
{
    // Expected format: (###) ###-####
    // Masked: (***) ***-####  (last 4 digits visible)
    if (decrypted.length() < 4)
        return QString(decrypted.length(), QChar('*'));

    // Find the last 4 digit characters
    QString masked = decrypted;
    int digitsFromEnd = 0;
    for (int i = masked.length() - 1; i >= 0 && digitsFromEnd < 4; --i) {
        if (masked[i].isDigit())
            ++digitsFromEnd;
    }

    // Replace all digits except the last 4 with '*'
    int digitsKept = 0;
    for (int i = masked.length() - 1; i >= 0; --i) {
        if (masked[i].isDigit()) {
            if (digitsKept >= 4)
                masked[i] = QChar('*');
            ++digitsKept;
        }
    }
    return masked;
}

QString MaskingPolicy::maskBarcode(const QString& decrypted)
{
    return maskGeneric(decrypted, 4);
}

QString MaskingPolicy::maskName(const QString& decrypted)
{
    if (decrypted.isEmpty())
        return decrypted;

    // Split on spaces, mask each word: keep first char, replace rest with '*'
    QStringList words = decrypted.split(QChar(' '), Qt::SkipEmptyParts);
    QStringList masked;
    for (const QString& word : words) {
        if (word.length() <= 1) {
            masked.append(word);
        } else {
            masked.append(word.left(1) + QString(word.length() - 1, QChar('*')));
        }
    }
    return masked.join(QChar(' '));
}

QString MaskingPolicy::maskGeneric(const QString& value, int visibleSuffix)
{
    if (value.length() <= visibleSuffix)
        return QString(value.length(), QChar('*'));

    return QString(value.length() - visibleSuffix, QChar('*')) + value.right(visibleSuffix);
}

bool MaskingPolicy::requiresStepUp()
{
    return true;
}
