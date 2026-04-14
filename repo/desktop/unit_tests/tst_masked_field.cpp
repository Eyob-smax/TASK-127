// tst_masked_field.cpp — ProctorOps
// Unit tests for MaskingPolicy field masking rules.
// Verifies mobile, barcode, name, and generic masking formats,
// as well as the step-up requirement policy.

#include <QtTest/QtTest>
#include "utils/MaskingPolicy.h"

class TstMaskedField : public QObject
{
    Q_OBJECT

private slots:
    // ── Generic masking ──────────────────────────────────────────────────
    void test_maskGeneric_default4();
    void test_maskGeneric_shortValue();
    void test_maskGeneric_exactLength();
    void test_maskGeneric_customSuffix();
    void test_maskGeneric_emptyValue();

    // ── Mobile masking ───────────────────────────────────────────────────
    void test_maskMobile_correctFormat();
    void test_maskMobile_preservesLast4Digits();
    void test_maskMobile_differentNumbers();

    // ── Barcode masking ──────────────────────────────────────────────────
    void test_maskBarcode_masksAllButLast4();
    void test_maskBarcode_shortBarcode();

    // ── Name masking ─────────────────────────────────────────────────────
    void test_maskName_twoPartName();
    void test_maskName_singleName();

    // ── Step-up requirement ──────────────────────────────────────────────
    void test_requiresStepUp_alwaysTrue();
};

// ── Generic masking ──────────────────────────────────────────────────────────

void TstMaskedField::test_maskGeneric_default4()
{
    QString masked = MaskingPolicy::maskGeneric(QStringLiteral("1234567890"));
    // Last 4 visible: "7890", rest replaced with '*'
    QCOMPARE(masked.length(), 10);
    QVERIFY(masked.endsWith(QStringLiteral("7890")));
    // First 6 chars should be '*'
    for (int i = 0; i < 6; ++i)
        QCOMPARE(masked[i], QChar('*'));
}

void TstMaskedField::test_maskGeneric_shortValue()
{
    // Value shorter than visibleSuffix (4) — should be fully masked
    QString masked = MaskingPolicy::maskGeneric(QStringLiteral("AB"));
    QCOMPARE(masked.length(), 2);
    for (int i = 0; i < masked.length(); ++i)
        QCOMPARE(masked[i], QChar('*'));
}

void TstMaskedField::test_maskGeneric_exactLength()
{
    // Value exactly 4 chars — should be fully masked (nothing to hide)
    QString masked = MaskingPolicy::maskGeneric(QStringLiteral("1234"));
    QCOMPARE(masked.length(), 4);
    for (int i = 0; i < masked.length(); ++i)
        QCOMPARE(masked[i], QChar('*'));
}

void TstMaskedField::test_maskGeneric_customSuffix()
{
    QString masked = MaskingPolicy::maskGeneric(QStringLiteral("ABCDEFGHIJ"), 2);
    QVERIFY(masked.endsWith(QStringLiteral("IJ")));
    QCOMPARE(masked.length(), 10);
    for (int i = 0; i < 8; ++i)
        QCOMPARE(masked[i], QChar('*'));
}

void TstMaskedField::test_maskGeneric_emptyValue()
{
    QString masked = MaskingPolicy::maskGeneric(QString());
    QVERIFY(masked.isEmpty());
}

// ── Mobile masking ──────────────────────────────────────────────────────────

void TstMaskedField::test_maskMobile_correctFormat()
{
    QString masked = MaskingPolicy::maskMobile(QStringLiteral("(555) 123-4567"));
    // Should mask digits except last 4: "(***) ***-4567"
    QVERIFY(masked.contains(QStringLiteral("4567")));
    QVERIFY(masked.contains(QStringLiteral("*")));
}

void TstMaskedField::test_maskMobile_preservesLast4Digits()
{
    QString masked = MaskingPolicy::maskMobile(QStringLiteral("(800) 999-1234"));
    QVERIFY(masked.endsWith(QStringLiteral("1234")));
}

void TstMaskedField::test_maskMobile_differentNumbers()
{
    QString m1 = MaskingPolicy::maskMobile(QStringLiteral("(555) 123-4567"));
    QString m2 = MaskingPolicy::maskMobile(QStringLiteral("(800) 999-1234"));
    QVERIFY(m1 != m2); // Different last 4 digits
}

// ── Barcode masking ──────────────────────────────────────────────────────────

void TstMaskedField::test_maskBarcode_masksAllButLast4()
{
    QString masked = MaskingPolicy::maskBarcode(QStringLiteral("ABCDE12345678"));
    QCOMPARE(masked.length(), 13);
    QVERIFY(masked.endsWith(QStringLiteral("5678")));
    for (int i = 0; i < 9; ++i)
        QCOMPARE(masked[i], QChar('*'));
}

void TstMaskedField::test_maskBarcode_shortBarcode()
{
    QString masked = MaskingPolicy::maskBarcode(QStringLiteral("ABC"));
    QCOMPARE(masked.length(), 3);
    // Short barcode should be fully masked
    for (int i = 0; i < masked.length(); ++i)
        QCOMPARE(masked[i], QChar('*'));
}

// ── Name masking ─────────────────────────────────────────────────────────────

void TstMaskedField::test_maskName_twoPartName()
{
    QString masked = MaskingPolicy::maskName(QStringLiteral("John Smith"));
    // Should show first char of each word, mask the rest
    QVERIFY(masked.startsWith(QChar('J')));
    QVERIFY(masked.contains(QChar('S')));
    QVERIFY(masked.contains(QChar('*')));
}

void TstMaskedField::test_maskName_singleName()
{
    QString masked = MaskingPolicy::maskName(QStringLiteral("Alice"));
    QVERIFY(masked.startsWith(QChar('A')));
    QVERIFY(masked.contains(QChar('*')));
}

// ── Step-up requirement ──────────────────────────────────────────────────────

void TstMaskedField::test_requiresStepUp_alwaysTrue()
{
    QVERIFY(MaskingPolicy::requiresStepUp());
}

QTEST_GUILESS_MAIN(TstMaskedField)
#include "tst_masked_field.moc"
