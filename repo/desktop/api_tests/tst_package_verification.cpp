// tst_package_verification.cpp — ProctorOps
// Integration tests for Ed25519 signature + SHA-256 digest verification
// for sync and update packages. Uses real crypto and real repository.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryFile>
#include <QTemporaryDir>

#include "services/PackageVerifier.h"
#include "repositories/SyncRepository.h"
#include "crypto/Ed25519Verifier.h"
#include "crypto/HashChain.h"
#include "crypto/SecureRandom.h"
#include "models/Sync.h"

#include <openssl/evp.h>
#include <openssl/x509.h>

class TstPackageVerification : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void test_validSignature_accepted();
    void test_invalidSignature_rejected();
    void test_revokedKey_rejected();
    void test_expiredKey_rejected();
    void test_fileDigest_valid();
    void test_fileDigest_tampered();

private:
    struct TestKeyPair {
        QByteArray publicKeyDer;
        QByteArray privateKeyDer;
        QString publicKeyDerHex;
        QString fingerprint;
    };

    void applySchema();
    TestKeyPair generateKeyPair();
    QByteArray signMessage(const QByteArray& message, const QByteArray& privateKeyDer);
    TrustedSigningKey importTestKey(SyncRepository& repo, const TestKeyPair& keys,
                                    bool revoked = false,
                                    const QDateTime& expiresAt = {});

    QSqlDatabase m_db;
    int m_dbIndex = 0;
};

void TstPackageVerification::initTestCase()
{
    qDebug() << "TstPackageVerification: starting";
}

void TstPackageVerification::cleanupTestCase()
{
    qDebug() << "TstPackageVerification: complete";
}

void TstPackageVerification::init()
{
    QString connName = QStringLiteral("tst_pkg_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));

    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON;"));
    applySchema();
}

void TstPackageVerification::cleanup()
{
    QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstPackageVerification::applySchema()
{
    QSqlQuery q(m_db);

    // Users table (needed for FK in trusted_signing_keys)
    q.exec(QStringLiteral(
        "CREATE TABLE users ("
        "  id TEXT PRIMARY KEY, username TEXT NOT NULL UNIQUE,"
        "  role TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'Active',"
        "  created_at TEXT NOT NULL, updated_at TEXT NOT NULL,"
        "  created_by_user_id TEXT)"));

    // Insert a system user for key import
    q.exec(QStringLiteral(
        "INSERT INTO users (id, username, role, status, created_at, updated_at)"
        " VALUES ('system', 'system', 'SECURITY_ADMINISTRATOR', 'Active',"
        " '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z')"));

    // Sync schema
    q.exec(QStringLiteral(
        "CREATE TABLE sync_packages ("
        "  id TEXT PRIMARY KEY, source_desk_id TEXT NOT NULL,"
        "  signer_key_id TEXT NOT NULL, exported_at TEXT NOT NULL,"
        "  since_watermark TEXT NOT NULL,"
        "  status TEXT NOT NULL DEFAULT 'Pending',"
        "  package_file_path TEXT NOT NULL,"
        "  imported_at TEXT, imported_by_user_id TEXT REFERENCES users(id))"));

    q.exec(QStringLiteral(
        "CREATE TABLE sync_package_entities ("
        "  package_id TEXT NOT NULL REFERENCES sync_packages(id) ON DELETE CASCADE,"
        "  entity_type TEXT NOT NULL, file_path TEXT NOT NULL,"
        "  sha256_hex TEXT NOT NULL, record_count INTEGER NOT NULL,"
        "  verified INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (package_id, entity_type))"));

    q.exec(QStringLiteral(
        "CREATE TABLE conflict_records ("
        "  id TEXT PRIMARY KEY, package_id TEXT NOT NULL REFERENCES sync_packages(id),"
        "  type TEXT NOT NULL, entity_type TEXT NOT NULL,"
        "  entity_id TEXT NOT NULL, description TEXT NOT NULL,"
        "  status TEXT NOT NULL DEFAULT 'Pending',"
        "  incoming_payload_json TEXT NOT NULL, local_payload_json TEXT NOT NULL,"
        "  detected_at TEXT NOT NULL, resolved_by_user_id TEXT, resolved_at TEXT)"));

    q.exec(QStringLiteral(
        "CREATE TABLE trusted_signing_keys ("
        "  id TEXT PRIMARY KEY, label TEXT NOT NULL,"
        "  public_key_der_hex TEXT NOT NULL,"
        "  fingerprint TEXT NOT NULL UNIQUE,"
        "  imported_at TEXT NOT NULL,"
        "  imported_by_user_id TEXT NOT NULL REFERENCES users(id),"
        "  expires_at TEXT, revoked INTEGER NOT NULL DEFAULT 0)"));
}

TstPackageVerification::TestKeyPair TstPackageVerification::generateKeyPair()
{
    TestKeyPair result;

    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_keygen(ctx, &pkey);
    EVP_PKEY_CTX_free(ctx);

    // Public key DER
    unsigned char* pubDer = nullptr;
    int pubLen = i2d_PUBKEY(pkey, &pubDer);
    result.publicKeyDer = QByteArray(reinterpret_cast<const char*>(pubDer), pubLen);
    OPENSSL_free(pubDer);

    // Private key DER
    unsigned char* privDer = nullptr;
    int privLen = i2d_PrivateKey(pkey, &privDer);
    result.privateKeyDer = QByteArray(reinterpret_cast<const char*>(privDer), privLen);
    OPENSSL_free(privDer);

    EVP_PKEY_free(pkey);

    result.publicKeyDerHex = QString::fromLatin1(result.publicKeyDer.toHex());

    auto fpResult = Ed25519Verifier::computeFingerprint(result.publicKeyDer);
    result.fingerprint = fpResult.isOk() ? fpResult.value() : QString();

    return result;
}

QByteArray TstPackageVerification::signMessage(const QByteArray& message,
                                                const QByteArray& privateKeyDer)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(privateKeyDer.constData());
    EVP_PKEY* pkey = d2i_AutoPrivateKey(nullptr, &p, privateKeyDer.size());
    if (!pkey) return {};

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

TrustedSigningKey TstPackageVerification::importTestKey(SyncRepository& repo,
                                                         const TestKeyPair& keys,
                                                         bool revoked,
                                                         const QDateTime& expiresAt)
{
    TrustedSigningKey tsk;
    tsk.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tsk.label = QStringLiteral("Test Key");
    tsk.publicKeyDerHex = keys.publicKeyDerHex;
    tsk.fingerprint = keys.fingerprint;
    tsk.importedAt = QDateTime::currentDateTimeUtc();
    tsk.importedByUserId = QStringLiteral("system");
    tsk.expiresAt = expiresAt;
    tsk.revoked = revoked;

    auto result = repo.insertSigningKey(tsk);
    if (result.isErr())
        qWarning() << "Failed to insert test key:" << result.errorMessage();

    return tsk;
}

// ── Valid signature ──────────────────────────────────────────────────────────

void TstPackageVerification::test_validSignature_accepted()
{
    auto keys = generateKeyPair();
    SyncRepository syncRepo(m_db);
    auto tsk = importTestKey(syncRepo, keys);

    PackageVerifier verifier(syncRepo);

    QByteArray manifest = QByteArrayLiteral("{\"package_id\":\"pkg-001\",\"entities\":[]}");
    QByteArray signature = signMessage(manifest, keys.privateKeyDer);
    QVERIFY(!signature.isEmpty());

    auto result = verifier.verifyPackageSignature(manifest, signature, tsk.id);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QVERIFY(result.value());
}

// ── Invalid signature ────────────────────────────────────────────────────────

void TstPackageVerification::test_invalidSignature_rejected()
{
    auto keys = generateKeyPair();
    SyncRepository syncRepo(m_db);
    auto tsk = importTestKey(syncRepo, keys);

    PackageVerifier verifier(syncRepo);

    QByteArray manifest = QByteArrayLiteral("{\"package_id\":\"pkg-001\"}");
    QByteArray signature = signMessage(manifest, keys.privateKeyDer);

    // Tamper with the manifest
    QByteArray tampered = QByteArrayLiteral("{\"package_id\":\"pkg-TAMPERED\"}");
    auto result = verifier.verifyPackageSignature(tampered, signature, tsk.id);
    QVERIFY(result.isOk());
    QVERIFY(!result.value()); // signature invalid
}

// ── Revoked key ──────────────────────────────────────────────────────────────

void TstPackageVerification::test_revokedKey_rejected()
{
    auto keys = generateKeyPair();
    SyncRepository syncRepo(m_db);
    auto tsk = importTestKey(syncRepo, keys, /*revoked=*/true);

    PackageVerifier verifier(syncRepo);

    QByteArray manifest = QByteArrayLiteral("test manifest");
    QByteArray signature = signMessage(manifest, keys.privateKeyDer);

    auto result = verifier.verifyPackageSignature(manifest, signature, tsk.id);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::TrustStoreMiss);
}

// ── Expired key ──────────────────────────────────────────────────────────────

void TstPackageVerification::test_expiredKey_rejected()
{
    auto keys = generateKeyPair();
    SyncRepository syncRepo(m_db);

    // Key that expired yesterday
    QDateTime expired = QDateTime::currentDateTimeUtc().addDays(-1);
    auto tsk = importTestKey(syncRepo, keys, /*revoked=*/false, expired);

    PackageVerifier verifier(syncRepo);

    QByteArray manifest = QByteArrayLiteral("test manifest");
    QByteArray signature = signMessage(manifest, keys.privateKeyDer);

    auto result = verifier.verifyPackageSignature(manifest, signature, tsk.id);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::TrustStoreMiss);
}

// ── File digest valid ────────────────────────────────────────────────────────

void TstPackageVerification::test_fileDigest_valid()
{
    SyncRepository syncRepo(m_db);
    PackageVerifier verifier(syncRepo);

    // Write a temp file
    QTemporaryFile tmpFile;
    tmpFile.setAutoRemove(true);
    QVERIFY(tmpFile.open());
    QByteArray content = QByteArrayLiteral("Entity file content for digest verification");
    tmpFile.write(content);
    tmpFile.flush();
    tmpFile.close();

    // Compute expected hash
    QString expectedHash = HashChain::computeSha256(content);

    auto result = verifier.verifyFileDigest(tmpFile.fileName(), expectedHash);
    QVERIFY2(result.isOk(), result.isErr() ? qPrintable(result.errorMessage()) : "");
    QVERIFY(result.value());
}

// ── File digest tampered ─────────────────────────────────────────────────────

void TstPackageVerification::test_fileDigest_tampered()
{
    SyncRepository syncRepo(m_db);
    PackageVerifier verifier(syncRepo);

    QTemporaryFile tmpFile;
    tmpFile.setAutoRemove(true);
    QVERIFY(tmpFile.open());
    QByteArray content = QByteArrayLiteral("Original content");
    tmpFile.write(content);
    tmpFile.flush();
    tmpFile.close();

    // Compute hash of original, but the file now has different content
    // (well, actually the file has original content — use a wrong hash)
    QString wrongHash = HashChain::computeSha256(QByteArrayLiteral("Different content"));

    auto result = verifier.verifyFileDigest(tmpFile.fileName(), wrongHash);
    QVERIFY(result.isOk());
    QVERIFY(!result.value()); // digest mismatch
}

QTEST_GUILESS_MAIN(TstPackageVerification)
#include "tst_package_verification.moc"
