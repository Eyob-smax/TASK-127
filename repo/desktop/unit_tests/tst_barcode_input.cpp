// tst_barcode_input.cpp — ProctorOps
// Unit tests for BarcodeInput scanner timing and terminator behavior.

#include <QtTest/QtTest>

#include <QLineEdit>
#include <QSignalSpy>

#include "widgets/BarcodeInput.h"

class TstBarcodeInput : public QObject {
    Q_OBJECT

private slots:
    void test_scannerLikeInput_emitsBarcode();
    void test_tooShortInput_doesNotEmit();
    void test_slowTyping_resetsScannerBuffer();
};

void TstBarcodeInput::test_scannerLikeInput_emitsBarcode()
{
    QLineEdit input;
    BarcodeInput scanner;
    scanner.installOn(&input);
    scanner.setMinBarcodeLength(4);
    scanner.setMaxInterKeyDelay(50);

    QSignalSpy spy(&scanner, &BarcodeInput::barcodeScanned);

    input.show();
    QVERIFY(QTest::qWaitForWindowExposed(&input));
    input.setFocus();

    QTest::keyClicks(&input, QStringLiteral("ABC123"));
    QTest::keyClick(&input, Qt::Key_Return);

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("ABC123"));
}

void TstBarcodeInput::test_tooShortInput_doesNotEmit()
{
    QLineEdit input;
    BarcodeInput scanner;
    scanner.installOn(&input);
    scanner.setMinBarcodeLength(4);

    QSignalSpy spy(&scanner, &BarcodeInput::barcodeScanned);

    input.show();
    QVERIFY(QTest::qWaitForWindowExposed(&input));
    input.setFocus();

    QTest::keyClicks(&input, QStringLiteral("ABC"));
    QTest::keyClick(&input, Qt::Key_Return);

    QCOMPARE(spy.count(), 0);
}

void TstBarcodeInput::test_slowTyping_resetsScannerBuffer()
{
    QLineEdit input;
    BarcodeInput scanner;
    scanner.installOn(&input);
    scanner.setMinBarcodeLength(4);
    scanner.setMaxInterKeyDelay(20);

    QSignalSpy spy(&scanner, &BarcodeInput::barcodeScanned);

    input.show();
    QVERIFY(QTest::qWaitForWindowExposed(&input));
    input.setFocus();

    QTest::keyClick(&input, Qt::Key_A);
    QTest::qWait(40);
    QTest::keyClicks(&input, QStringLiteral("BCDE"));
    QTest::keyClick(&input, Qt::Key_Return);

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("BCDE"));
}

QTEST_MAIN(TstBarcodeInput)
#include "tst_barcode_input.moc"