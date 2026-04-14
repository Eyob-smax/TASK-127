// ClipboardGuard.cpp — ProctorOps
// Clipboard PII redaction implementation.

#include "ClipboardGuard.h"
#include "Logger.h"

#include <QApplication>
#include <QClipboard>

void ClipboardGuard::copyMasked(const QString& value)
{
    QString masked = maskValue(value);
    QClipboard* clipboard = QApplication::clipboard();
    if (clipboard) {
        clipboard->setText(masked);
        Logger::instance().info(QStringLiteral("ClipboardGuard"),
            QStringLiteral("Copied masked value to clipboard"));
    }
}

void ClipboardGuard::copyRedacted(const QString&)
{
    QClipboard* clipboard = QApplication::clipboard();
    if (clipboard) {
        clipboard->setText(QStringLiteral("[REDACTED]"));
        Logger::instance().info(QStringLiteral("ClipboardGuard"),
            QStringLiteral("Copied redacted placeholder to clipboard"));
    }
}

QString ClipboardGuard::maskValue(const QString& value, int visibleSuffix)
{
    if (value.length() <= visibleSuffix)
        return QString(value.length(), QChar('*'));

    return QString(value.length() - visibleSuffix, QChar('*')) + value.right(visibleSuffix);
}
