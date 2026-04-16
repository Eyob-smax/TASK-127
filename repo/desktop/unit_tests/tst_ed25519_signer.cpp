// tst_ed25519_signer.cpp — ProctorOps
// Focused unit tests for Ed25519Signer behavior.

#include <QtTest/QtTest>

#include "crypto/Ed25519Signer.h"
#include "crypto/Ed25519Verifier.h"

class TstEd25519Signer : public QObject
{
    Q_OBJECT

private slots:
    void test_generateKeyPair_returnsDerEncodedKeys();
    void test_signAndVerify_roundTrip();
    void test_sign_rejectsInvalidPrivateKey();
};

void TstEd25519Signer::test_generateKeyPair_returnsDerEncodedKeys()
{
    auto keyPair = Ed25519Signer::generateKeyPair();
    QVERIFY2(keyPair.isOk(), keyPair.isErr() ? qPrintable(keyPair.errorMessage()) : "");

    const QByteArray privateKeyDer = keyPair.value().first;
    const QByteArray publicKeyDer = keyPair.value().second;

    QVERIFY(!privateKeyDer.isEmpty());
    QVERIFY(!publicKeyDer.isEmpty());
}

void TstEd25519Signer::test_signAndVerify_roundTrip()
{
    auto keyPair = Ed25519Signer::generateKeyPair();
    QVERIFY(keyPair.isOk());

    const QByteArray message = QByteArrayLiteral("proctorops-signing-payload");
    const QByteArray privateKeyDer = keyPair.value().first;
    const QByteArray publicKeyDer = keyPair.value().second;

    auto signResult = Ed25519Signer::sign(message, privateKeyDer);
    QVERIFY2(signResult.isOk(), signResult.isErr() ? qPrintable(signResult.errorMessage()) : "");
    QCOMPARE(signResult.value().size(), 64);

    auto verifyResult = Ed25519Verifier::verify(message, signResult.value(), publicKeyDer);
    QVERIFY2(verifyResult.isOk(), verifyResult.isErr() ? qPrintable(verifyResult.errorMessage()) : "");
    QVERIFY(verifyResult.value());
}

void TstEd25519Signer::test_sign_rejectsInvalidPrivateKey()
{
    const QByteArray invalidPrivateKey = QByteArrayLiteral("not-a-der-private-key");
    auto signResult = Ed25519Signer::sign(QByteArrayLiteral("payload"), invalidPrivateKey);

    QVERIFY(signResult.isErr());
    QCOMPARE(signResult.errorCode(), ErrorCode::SignatureInvalid);
}

QTEST_MAIN(TstEd25519Signer)
#include "tst_ed25519_signer.moc"
