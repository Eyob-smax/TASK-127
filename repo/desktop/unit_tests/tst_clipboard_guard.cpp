// tst_clipboard_guard.cpp — ProctorOps
// Unit tests for ClipboardGuard masking and PII redaction.
// Uses QApplication for clipboard access (QTEST_MAIN, not GUILESS).

#include <QtTest/QtTest>
#include <QApplication>
#include <QClipboard>
#include "utils/ClipboardGuard.h"
#include "utils/MaskingPolicy.h"

class TstClipboardGuard : public QObject
{
    Q_OBJECT

private slots:
    // ── maskValue ────────────────────────────────────────────────────────
    void test_maskValue_showsLast4();
    void test_maskValue_shortValue();
    void test_maskValue_emptyValue();
    void test_maskValue_customSuffix();

    // ── copyMasked ───────────────────────────────────────────────────────
    void test_copyMasked_writesToClipboard();
    void test_copyMasked_mobileFormat();

    // ── copyRedacted ─────────────────────────────────────────────────────
    void test_copyRedacted_writesRedacted();
    void test_copyRedacted_ignoresOriginalValue();
};

// ── maskValue tests ──────────────────────────────────────────────────────────

void TstClipboardGuard::test_maskValue_showsLast4()
{
    QString masked = ClipboardGuard::maskValue(QStringLiteral("1234567890"));
    QCOMPARE(masked.length(), 10);
    QVERIFY(masked.endsWith(QStringLiteral("7890")));
    for (int i = 0; i < 6; ++i)
        QCOMPARE(masked[i], QChar('*'));
}

void TstClipboardGuard::test_maskValue_shortValue()
{
    QString masked = ClipboardGuard::maskValue(QStringLiteral("AB"));
    QCOMPARE(masked.length(), 2);
    for (int i = 0; i < masked.length(); ++i)
        QCOMPARE(masked[i], QChar('*'));
}

void TstClipboardGuard::test_maskValue_emptyValue()
{
    QString masked = ClipboardGuard::maskValue(QString());
    QVERIFY(masked.isEmpty());
}

void TstClipboardGuard::test_maskValue_customSuffix()
{
    QString masked = ClipboardGuard::maskValue(QStringLiteral("ABCDEFGH"), 2);
    QVERIFY(masked.endsWith(QStringLiteral("GH")));
    for (int i = 0; i < 6; ++i)
        QCOMPARE(masked[i], QChar('*'));
}

// ── copyMasked tests ─────────────────────────────────────────────────────────

void TstClipboardGuard::test_copyMasked_writesToClipboard()
{
    ClipboardGuard::copyMasked(QStringLiteral("1234567890"));
    QClipboard* clip = QApplication::clipboard();
    QString text = clip->text();
    // Clipboard should contain masked version, not the original
    QVERIFY(text != QStringLiteral("1234567890"));
    QVERIFY(text.endsWith(QStringLiteral("7890")));
    QVERIFY(text.contains(QChar('*')));
}

void TstClipboardGuard::test_copyMasked_mobileFormat()
{
    ClipboardGuard::copyMasked(QStringLiteral("(555) 123-4567"));
    QClipboard* clip = QApplication::clipboard();
    QString text = clip->text();
    QVERIFY(text.contains(QChar('*')));
    QVERIFY(text.endsWith(QStringLiteral("4567")));
}

// ── copyRedacted tests ───────────────────────────────────────────────────────

void TstClipboardGuard::test_copyRedacted_writesRedacted()
{
    ClipboardGuard::copyRedacted(QStringLiteral("sensitive data"));
    QClipboard* clip = QApplication::clipboard();
    QCOMPARE(clip->text(), QStringLiteral("[REDACTED]"));
}

void TstClipboardGuard::test_copyRedacted_ignoresOriginalValue()
{
    ClipboardGuard::copyRedacted(QStringLiteral("(555) 123-4567"));
    QClipboard* clip = QApplication::clipboard();
    QCOMPARE(clip->text(), QStringLiteral("[REDACTED]"));
    QVERIFY(!clip->text().contains(QStringLiteral("555")));
}

QTEST_MAIN(TstClipboardGuard)
#include "tst_clipboard_guard.moc"
