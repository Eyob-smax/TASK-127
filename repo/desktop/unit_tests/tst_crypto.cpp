// tst_crypto.cpp — ProctorOps
// Unit tests for crypto primitives: SecureRandom, Argon2idHasher, AesGcmCipher,
// Ed25519Verifier, and HashChain.

#include <QtTest/QtTest>
#include "crypto/SecureRandom.h"
#include "crypto/Argon2idHasher.h"
#include "crypto/AesGcmCipher.h"
#include "crypto/Ed25519Verifier.h"
#include "crypto/Ed25519Signer.h"
#include "crypto/HashChain.h"
#include "models/Audit.h"
#include "models/CommonTypes.h"
#include "utils/Validation.h"

#include <openssl/evp.h>
#include <openssl/pem.h>

class TstCrypto : public QObject
{
    Q_OBJECT

private slots:
    // ── SecureRandom ─────────────────────────────────────────────────────
    void test_secureRandom_generatesRequestedLength();
    void test_secureRandom_noDuplicates();
    void test_secureRandom_hexLength();
    void test_secureRandom_zeroBytes();

    // ── Argon2idHasher ───────────────────────────────────────────────────
    void test_argon2_hashAndVerify();
    void test_argon2_wrongPassword();
    void test_argon2_credentialFields();
    void test_argon2_shortPassword();
    void test_argon2_differentSalts();

    // ── AesGcmCipher ─────────────────────────────────────────────────────
    void test_aesGcm_encryptDecrypt();
    void test_aesGcm_differentCiphertext();
    void test_aesGcm_tamperedCiphertext();
    void test_aesGcm_wrongKey();
    void test_aesGcm_emptyPlaintext();
    void test_aesGcm_contextIsolation();

    // ── Ed25519Verifier ──────────────────────────────────────────────────
    void test_ed25519_validSignature();
    void test_ed25519_invalidSignature();
    void test_ed25519_fingerprint();

    // ── Ed25519Signer ───────────────────────────────────────────────────
    void test_ed25519Signer_generateKeyPair();
    void test_ed25519Signer_signAndVerify();
    void test_ed25519Signer_signDifferentMessages();
    void test_ed25519Signer_tamperedMessageFails();

    // ── HashChain ────────────────────────────────────────────────────────
    void test_hashChain_computesSha256();
    void test_hashChain_entryHash_deterministic();
    void test_hashChain_entryHash_changesWithPreviousHash();
    void test_hashChain_emptyInput();

private:
    // Helper: generate an Ed25519 keypair for testing using OpenSSL
    struct Ed25519TestKeys {
        QByteArray publicKeyDer;
        QByteArray privateKeyDer;
    };
    static Ed25519TestKeys generateEd25519KeyPair();
    static QByteArray signEd25519(const QByteArray& message, const QByteArray& privateKeyDer);
};

// ── Ed25519 test helpers ──────────────────────────────────────────────────────

TstCrypto::Ed25519TestKeys TstCrypto::generateEd25519KeyPair()
{
    Ed25519TestKeys keys;
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_keygen(ctx, &pkey);
    EVP_PKEY_CTX_free(ctx);

    // Extract DER-encoded public key
    unsigned char* pubDer = nullptr;
    int pubLen = i2d_PUBKEY(pkey, &pubDer);
    keys.publicKeyDer = QByteArray(reinterpret_cast<const char*>(pubDer), pubLen);
    OPENSSL_free(pubDer);

    // Extract DER-encoded private key
    unsigned char* privDer = nullptr;
    int privLen = i2d_PrivateKey(pkey, &privDer);
    keys.privateKeyDer = QByteArray(reinterpret_cast<const char*>(privDer), privLen);
    OPENSSL_free(privDer);

    EVP_PKEY_free(pkey);
    return keys;
}

QByteArray TstCrypto::signEd25519(const QByteArray& message, const QByteArray& privateKeyDer)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(privateKeyDer.constData());
    EVP_PKEY* pkey = d2i_AutoPrivateKey(nullptr, &p, privateKeyDer.size());
    if (!pkey)
        return {};

    EVP_MD_CTX* mdCtx = EVP_MD_CTX_new();
    EVP_DigestSignInit(mdCtx, nullptr, nullptr, nullptr, pkey);

    size_t sigLen = 0;
    EVP_DigestSign(mdCtx, nullptr, &sigLen,
                   reinterpret_cast<const unsigned char*>(message.constData()),
                   static_cast<size_t>(message.size()));

    QByteArray sig(static_cast<int>(sigLen), '\0');
    EVP_DigestSign(mdCtx, reinterpret_cast<unsigned char*>(sig.data()), &sigLen,
                   reinterpret_cast<const unsigned char*>(message.constData()),
                   static_cast<size_t>(message.size()));
    sig.resize(static_cast<int>(sigLen));

    EVP_MD_CTX_free(mdCtx);
    EVP_PKEY_free(pkey);
    return sig;
}

// ── SecureRandom tests ────────────────────────────────────────────────────────

void TstCrypto::test_secureRandom_generatesRequestedLength()
{
    QByteArray r16 = SecureRandom::generate(16);
    QCOMPARE(r16.size(), 16);

    QByteArray r32 = SecureRandom::generate(32);
    QCOMPARE(r32.size(), 32);

    QByteArray r1 = SecureRandom::generate(1);
    QCOMPARE(r1.size(), 1);
}

void TstCrypto::test_secureRandom_noDuplicates()
{
    QByteArray a = SecureRandom::generate(32);
    QByteArray b = SecureRandom::generate(32);
    QVERIFY2(a != b, "Two 32-byte random outputs must not be identical");
}

void TstCrypto::test_secureRandom_hexLength()
{
    QString hex = SecureRandom::generateHex(16);
    QCOMPARE(hex.length(), 32); // 16 bytes = 32 hex chars
}

void TstCrypto::test_secureRandom_zeroBytes()
{
    QByteArray empty = SecureRandom::generate(0);
    QCOMPARE(empty.size(), 0);
}

// ── Argon2idHasher tests ──────────────────────────────────────────────────────

void TstCrypto::test_argon2_hashAndVerify()
{
    const QString password = QStringLiteral("ValidPassword12!");
    auto hashResult = Argon2idHasher::hashPassword(password);
    QVERIFY2(hashResult.isOk(), hashResult.isErr() ? qPrintable(hashResult.errorMessage()) : "");

    const Credential& cred = hashResult.value();
    auto verifyResult = Argon2idHasher::verifyPassword(password, cred);
    QVERIFY2(verifyResult.isOk(), verifyResult.isErr() ? qPrintable(verifyResult.errorMessage()) : "");
    QVERIFY(verifyResult.value());
}

void TstCrypto::test_argon2_wrongPassword()
{
    const QString password = QStringLiteral("ValidPassword12!");
    auto hashResult = Argon2idHasher::hashPassword(password);
    QVERIFY(hashResult.isOk());

    auto verifyResult = Argon2idHasher::verifyPassword(
        QStringLiteral("WrongPassword99!"), hashResult.value());
    QVERIFY(verifyResult.isOk());
    QVERIFY(!verifyResult.value()); // mismatch returns false, not an error
}

void TstCrypto::test_argon2_credentialFields()
{
    auto hashResult = Argon2idHasher::hashPassword(QStringLiteral("TestPassword12!"));
    QVERIFY(hashResult.isOk());

    const Credential& cred = hashResult.value();
    QCOMPARE(cred.algorithm, QStringLiteral("argon2id"));
    QCOMPARE(cred.timeCost, Validation::Argon2TimeCost);
    QCOMPARE(cred.memoryCost, Validation::Argon2MemoryCost);
    QCOMPARE(cred.parallelism, Validation::Argon2Parallelism);
    QCOMPARE(cred.tagLength, Validation::Argon2TagLength);
    QCOMPARE(cred.saltHex.length(), Validation::Argon2SaltLength * 2); // hex-encoded
    QCOMPARE(cred.hashHex.length(), Validation::Argon2TagLength * 2);
    QVERIFY(!cred.saltHex.isEmpty());
    QVERIFY(!cred.hashHex.isEmpty());
}

void TstCrypto::test_argon2_shortPassword()
{
    const QString shortPw(Validation::PasswordMinLength - 1, QChar('a'));
    auto result = Argon2idHasher::hashPassword(shortPw);
    QVERIFY2(result.isErr(), "Password shorter than minimum must be rejected");
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstCrypto::test_argon2_differentSalts()
{
    const QString pw = QStringLiteral("SamePassword12!!");
    auto r1 = Argon2idHasher::hashPassword(pw);
    auto r2 = Argon2idHasher::hashPassword(pw);
    QVERIFY(r1.isOk());
    QVERIFY(r2.isOk());
    QVERIFY2(r1.value().saltHex != r2.value().saltHex,
             "Same password hashed twice must use different salts");
    QVERIFY(r1.value().hashHex != r2.value().hashHex);
}

// ── AesGcmCipher tests ────────────────────────────────────────────────────────

void TstCrypto::test_aesGcm_encryptDecrypt()
{
    QByteArray masterKey = SecureRandom::generate(32);
    AesGcmCipher cipher(masterKey);

    const QString plaintext = QStringLiteral("(555) 123-4567");
    const QByteArray context = QByteArrayLiteral("member.mobile");

    auto encResult = cipher.encrypt(plaintext, context);
    QVERIFY2(encResult.isOk(), encResult.isErr() ? qPrintable(encResult.errorMessage()) : "");

    auto decResult = cipher.decrypt(encResult.value(), context);
    QVERIFY2(decResult.isOk(), decResult.isErr() ? qPrintable(decResult.errorMessage()) : "");
    QCOMPARE(decResult.value(), plaintext);
}

void TstCrypto::test_aesGcm_differentCiphertext()
{
    QByteArray masterKey = SecureRandom::generate(32);
    AesGcmCipher cipher(masterKey);

    const QString plaintext = QStringLiteral("Same plaintext input");
    auto ct1 = cipher.encrypt(plaintext);
    auto ct2 = cipher.encrypt(plaintext);
    QVERIFY(ct1.isOk());
    QVERIFY(ct2.isOk());
    QVERIFY2(ct1.value() != ct2.value(),
             "Same plaintext must produce different ciphertext (random nonce/salt)");
}

void TstCrypto::test_aesGcm_tamperedCiphertext()
{
    QByteArray masterKey = SecureRandom::generate(32);
    AesGcmCipher cipher(masterKey);

    auto encResult = cipher.encrypt(QStringLiteral("Sensitive data"));
    QVERIFY(encResult.isOk());

    QByteArray tampered = encResult.value();
    // Tamper with a byte in the ciphertext body (after version+salt+nonce = 1+16+12 = 29)
    if (tampered.size() > 30) {
        tampered[30] = static_cast<char>(tampered[30] ^ 0xFF);
    }

    auto decResult = cipher.decrypt(tampered);
    QVERIFY2(decResult.isErr(), "Tampered ciphertext must fail decryption");
}

void TstCrypto::test_aesGcm_wrongKey()
{
    QByteArray key1 = SecureRandom::generate(32);
    QByteArray key2 = SecureRandom::generate(32);
    AesGcmCipher cipher1(key1);
    AesGcmCipher cipher2(key2);

    auto encResult = cipher1.encrypt(QStringLiteral("Secret"));
    QVERIFY(encResult.isOk());

    auto decResult = cipher2.decrypt(encResult.value());
    QVERIFY2(decResult.isErr(), "Different master key must fail decryption");
}

void TstCrypto::test_aesGcm_emptyPlaintext()
{
    QByteArray masterKey = SecureRandom::generate(32);
    AesGcmCipher cipher(masterKey);

    auto encResult = cipher.encrypt(QString());
    QVERIFY(encResult.isOk());

    auto decResult = cipher.decrypt(encResult.value());
    QVERIFY(decResult.isOk());
    QCOMPARE(decResult.value(), QString());
}

void TstCrypto::test_aesGcm_contextIsolation()
{
    QByteArray masterKey = SecureRandom::generate(32);
    AesGcmCipher cipher(masterKey);

    const QString plaintext = QStringLiteral("context-sensitive");
    auto enc = cipher.encrypt(plaintext, QByteArrayLiteral("context.A"));
    QVERIFY(enc.isOk());

    // Decrypting with different context should fail (HKDF derives different key)
    auto dec = cipher.decrypt(enc.value(), QByteArrayLiteral("context.B"));
    QVERIFY2(dec.isErr(), "Different HKDF context must prevent decryption");
}

// ── Ed25519Verifier tests ─────────────────────────────────────────────────────

void TstCrypto::test_ed25519_validSignature()
{
    auto keys = generateEd25519KeyPair();
    QVERIFY(!keys.publicKeyDer.isEmpty());
    QVERIFY(!keys.privateKeyDer.isEmpty());

    QByteArray message = QByteArrayLiteral("Package manifest content for verification");
    QByteArray signature = signEd25519(message, keys.privateKeyDer);
    QVERIFY(!signature.isEmpty());

    auto result = Ed25519Verifier::verify(message, signature, keys.publicKeyDer);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QVERIFY(result.value());
}

void TstCrypto::test_ed25519_invalidSignature()
{
    auto keys = generateEd25519KeyPair();
    QByteArray message = QByteArrayLiteral("Original message");
    QByteArray signature = signEd25519(message, keys.privateKeyDer);

    // Tamper with message
    QByteArray tampered = QByteArrayLiteral("Tampered message");
    auto result = Ed25519Verifier::verify(tampered, signature, keys.publicKeyDer);
    QVERIFY(result.isOk());
    QVERIFY(!result.value()); // signature invalid for different message
}

void TstCrypto::test_ed25519_fingerprint()
{
    auto keys = generateEd25519KeyPair();
    auto fpResult = Ed25519Verifier::computeFingerprint(keys.publicKeyDer);
    QVERIFY2(fpResult.isOk(), fpResult.isErr() ? qPrintable(fpResult.errorMessage()) : "");

    // SHA-256 hex = 64 chars
    QCOMPARE(fpResult.value().length(), 64);

    // Deterministic: same key yields same fingerprint
    auto fp2 = Ed25519Verifier::computeFingerprint(keys.publicKeyDer);
    QCOMPARE(fpResult.value(), fp2.value());
}

// ── Ed25519Signer tests ──────────────────────────────────────────────────────

void TstCrypto::test_ed25519Signer_generateKeyPair()
{
    auto result = Ed25519Signer::generateKeyPair();
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");

    const auto& [privKey, pubKey] = result.value();
    QVERIFY(!privKey.isEmpty());
    QVERIFY(!pubKey.isEmpty());
    QVERIFY(privKey != pubKey);
}

void TstCrypto::test_ed25519Signer_signAndVerify()
{
    auto keyResult = Ed25519Signer::generateKeyPair();
    QVERIFY(keyResult.isOk());
    const auto& [privKeyDer, pubKeyDer] = keyResult.value();

    const QByteArray message = QByteArrayLiteral("Package manifest for signing test");

    auto signResult = Ed25519Signer::sign(message, privKeyDer);
    QVERIFY2(signResult.isOk(), signResult.isErr() ? qPrintable(signResult.errorMessage()) : "");
    QVERIFY(!signResult.value().isEmpty());

    // Verify using Ed25519Verifier
    auto verifyResult = Ed25519Verifier::verify(message, signResult.value(), pubKeyDer);
    QVERIFY2(verifyResult.isOk(), verifyResult.isErr() ? qPrintable(verifyResult.errorMessage()) : "");
    QVERIFY(verifyResult.value());
}

void TstCrypto::test_ed25519Signer_signDifferentMessages()
{
    auto keyResult = Ed25519Signer::generateKeyPair();
    QVERIFY(keyResult.isOk());
    const auto& [privKeyDer, pubKeyDer] = keyResult.value();

    auto sig1 = Ed25519Signer::sign(QByteArrayLiteral("Message A"), privKeyDer);
    auto sig2 = Ed25519Signer::sign(QByteArrayLiteral("Message B"), privKeyDer);
    QVERIFY(sig1.isOk());
    QVERIFY(sig2.isOk());
    QVERIFY2(sig1.value() != sig2.value(),
             "Different messages must produce different signatures");
}

void TstCrypto::test_ed25519Signer_tamperedMessageFails()
{
    auto keyResult = Ed25519Signer::generateKeyPair();
    QVERIFY(keyResult.isOk());
    const auto& [privKeyDer, pubKeyDer] = keyResult.value();

    auto signResult = Ed25519Signer::sign(QByteArrayLiteral("Original"), privKeyDer);
    QVERIFY(signResult.isOk());

    // Verify against a different message — must fail
    auto verifyResult = Ed25519Verifier::verify(QByteArrayLiteral("Tampered"),
                                                 signResult.value(), pubKeyDer);
    QVERIFY(verifyResult.isOk());
    QVERIFY2(!verifyResult.value(), "Signature must be invalid for a different message");
}

// ── HashChain tests ───────────────────────────────────────────────────────────

void TstCrypto::test_hashChain_computesSha256()
{
    QByteArray data = QByteArrayLiteral("known input");
    QString hash = HashChain::computeSha256(data);

    QCOMPARE(hash.length(), 64); // SHA-256 = 64 hex chars
    QVERIFY(!hash.isEmpty());

    // Same input must produce same hash
    QString hash2 = HashChain::computeSha256(data);
    QCOMPARE(hash, hash2);
}

void TstCrypto::test_hashChain_entryHash_deterministic()
{
    AuditEntry entry;
    entry.id                = QStringLiteral("test-id-001");
    entry.timestamp         = QDateTime(QDate(2026, 4, 14), QTime(12, 0, 0), Qt::UTC);
    entry.actorUserId       = QStringLiteral("user-001");
    entry.eventType         = AuditEventType::Login;
    entry.entityType        = QStringLiteral("User");
    entry.entityId          = QStringLiteral("user-001");
    entry.beforePayloadJson = QStringLiteral("{}");
    entry.afterPayloadJson  = QStringLiteral("{}");
    entry.previousEntryHash = QString();

    QString hash1 = HashChain::computeEntryHash(entry);
    QString hash2 = HashChain::computeEntryHash(entry);
    QCOMPARE(hash1, hash2);
    QCOMPARE(hash1.length(), 64);
}

void TstCrypto::test_hashChain_entryHash_changesWithPreviousHash()
{
    AuditEntry entry;
    entry.id                = QStringLiteral("test-id-002");
    entry.timestamp         = QDateTime(QDate(2026, 4, 14), QTime(12, 0, 0), Qt::UTC);
    entry.actorUserId       = QStringLiteral("user-001");
    entry.eventType         = AuditEventType::Login;
    entry.entityType        = QStringLiteral("User");
    entry.entityId          = QStringLiteral("user-001");
    entry.beforePayloadJson = QStringLiteral("{}");
    entry.afterPayloadJson  = QStringLiteral("{}");

    entry.previousEntryHash = QStringLiteral("aaa");
    QString hashA = HashChain::computeEntryHash(entry);

    entry.previousEntryHash = QStringLiteral("bbb");
    QString hashB = HashChain::computeEntryHash(entry);

    QVERIFY2(hashA != hashB,
             "Different previousEntryHash must produce different entry hashes");
}

void TstCrypto::test_hashChain_emptyInput()
{
    QByteArray empty;
    QString hash = HashChain::computeSha256(empty);
    QCOMPARE(hash.length(), 64);
    // SHA-256 of empty input is the well-known constant
    QCOMPARE(hash, QStringLiteral("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

QTEST_GUILESS_MAIN(TstCrypto)
#include "tst_crypto.moc"
