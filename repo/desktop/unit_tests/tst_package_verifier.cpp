// tst_package_verifier.cpp — ProctorOps
// Unit tests for PackageVerifier with real trust-store repository and crypto.

#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QDateTime>

#include "utils/Migration.h"
#include "services/PackageVerifier.h"
#include "repositories/SyncRepository.h"
#include "crypto/Ed25519Signer.h"
#include "crypto/HashChain.h"

namespace {

void runMigrations(QSqlDatabase& db)
{
    const QString basePath = QStringLiteral(SOURCE_ROOT "/database/migrations");
    Migration runner(db, basePath);
    const auto result = runner.applyPending();
    QVERIFY2(result.success, qPrintable(result.errorMessage));
}

void seedAdmin(QSqlDatabase& db)
{
    QSqlQuery q(db);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO users "
        "(id, username, role, status, created_at, updated_at, created_by_user_id) "
        "VALUES ('u-admin', 'admin', 'SECURITY_ADMINISTRATOR', 'Active', ?, ?, NULL)"));
    q.addBindValue(now);
    q.addBindValue(now);
    QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
}

TrustedSigningKey makeKey(const QString& id,
                          const QByteArray& publicDer,
                          bool revoked = false,
                          const QDateTime& expiresAt = QDateTime())
{
    TrustedSigningKey key;
    key.id = id;
    key.label = id;
    key.publicKeyDerHex = QString::fromLatin1(publicDer.toHex());
    key.fingerprint = HashChain::computeSha256(publicDer);
    key.importedAt = QDateTime::currentDateTimeUtc();
    key.importedByUserId = QStringLiteral("u-admin");
    key.expiresAt = expiresAt;
    key.revoked = revoked;
    return key;
}

}

class TstPackageVerifier : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void test_verifyPackageSignature_validAndInvalid();
    void test_verifyPackageSignature_rejectsRevokedAndExpiredKey();
    void test_verifyFileDigest_and_verifyAllEntities();

private:
    QSqlDatabase m_db;
    int m_dbIndex = 0;
    SyncRepository* m_syncRepo = nullptr;
    PackageVerifier* m_verifier = nullptr;
};

void TstPackageVerifier::init()
{
    const QString connName = QStringLiteral("tst_pkg_verifier_%1").arg(m_dbIndex++);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    m_db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY2(m_db.open(), qPrintable(m_db.lastError().text()));

    QSqlQuery pragma(m_db);
    QVERIFY2(pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON;")), qPrintable(pragma.lastError().text()));

    runMigrations(m_db);
    seedAdmin(m_db);

    m_syncRepo = new SyncRepository(m_db);
    m_verifier = new PackageVerifier(*m_syncRepo);
}

void TstPackageVerifier::cleanup()
{
    delete m_verifier;
    delete m_syncRepo;
    m_verifier = nullptr;
    m_syncRepo = nullptr;

    const QString connName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
}

void TstPackageVerifier::test_verifyPackageSignature_validAndInvalid()
{
    auto kp = Ed25519Signer::generateKeyPair();
    QVERIFY(kp.isOk());

    const QByteArray privateDer = kp.value().first;
    const QByteArray publicDer = kp.value().second;
    QVERIFY(m_syncRepo->insertSigningKey(makeKey(QStringLiteral("k-valid"), publicDer)).isOk());

    const QByteArray manifest = QByteArrayLiteral("{\"package_id\":\"pkg-1\"}");

    auto signOk = Ed25519Signer::sign(manifest, privateDer);
    QVERIFY(signOk.isOk());

    auto verifyOk = m_verifier->verifyPackageSignature(manifest, signOk.value(), QStringLiteral("k-valid"));
    QVERIFY(verifyOk.isOk());
    QVERIFY(verifyOk.value());

    auto verifyBadSig = m_verifier->verifyPackageSignature(manifest, QByteArray("bad-signature"), QStringLiteral("k-valid"));
    QVERIFY(verifyBadSig.isErr() || !verifyBadSig.value());

    auto verifyMissingKey = m_verifier->verifyPackageSignature(manifest, signOk.value(), QStringLiteral("missing-key"));
    QVERIFY(verifyMissingKey.isErr());
    QCOMPARE(verifyMissingKey.errorCode(), ErrorCode::TrustStoreMiss);
}

void TstPackageVerifier::test_verifyPackageSignature_rejectsRevokedAndExpiredKey()
{
    auto kp = Ed25519Signer::generateKeyPair();
    QVERIFY(kp.isOk());

    const QByteArray privateDer = kp.value().first;
    const QByteArray publicDer = kp.value().second;
    const QByteArray manifest = QByteArrayLiteral("manifest-body");
    auto signature = Ed25519Signer::sign(manifest, privateDer);
    QVERIFY(signature.isOk());

    QVERIFY(m_syncRepo->insertSigningKey(makeKey(QStringLiteral("k-revoked"), publicDer, true)).isOk());
    auto revokedRes = m_verifier->verifyPackageSignature(manifest, signature.value(), QStringLiteral("k-revoked"));
    QVERIFY(revokedRes.isErr());
    QCOMPARE(revokedRes.errorCode(), ErrorCode::TrustStoreMiss);

    // Use a distinct keypair so unique fingerprint constraints do not collide.
    auto kpExpired = Ed25519Signer::generateKeyPair();
    QVERIFY(kpExpired.isOk());
    const QByteArray privateDerExpired = kpExpired.value().first;
    const QByteArray publicDerExpired = kpExpired.value().second;
    auto signatureExpired = Ed25519Signer::sign(manifest, privateDerExpired);
    QVERIFY(signatureExpired.isOk());

    const QDateTime pastExpiry = QDateTime::currentDateTimeUtc().addSecs(-60);
    QVERIFY(m_syncRepo->insertSigningKey(makeKey(QStringLiteral("k-expired"), publicDerExpired, false, pastExpiry)).isOk());
    auto expiredRes = m_verifier->verifyPackageSignature(manifest, signatureExpired.value(), QStringLiteral("k-expired"));
    QVERIFY(expiredRes.isErr());
    QCOMPARE(expiredRes.errorCode(), ErrorCode::TrustStoreMiss);
}

void TstPackageVerifier::test_verifyFileDigest_and_verifyAllEntities()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString f1 = tmp.path() + QStringLiteral("/deductions.jsonl");
    const QString f2 = tmp.path() + QStringLiteral("/corrections.jsonl");

    {
        QFile file(f1);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"id\":1}\n");
        file.close();
    }
    {
        QFile file(f2);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"id\":2}\n");
        file.close();
    }

    QFile f1Read(f1);
    QVERIFY(f1Read.open(QIODevice::ReadOnly));
    const QString f1Sha = HashChain::computeSha256(f1Read.readAll());
    f1Read.close();

    auto digestOk = m_verifier->verifyFileDigest(f1, f1Sha);
    QVERIFY(digestOk.isOk());
    QVERIFY(digestOk.value());

    auto digestBad = m_verifier->verifyFileDigest(f1, QString(64, QLatin1Char('0')));
    QVERIFY(digestBad.isOk());
    QVERIFY(!digestBad.value());

    QFile f2Read(f2);
    QVERIFY(f2Read.open(QIODevice::ReadOnly));
    const QString f2Sha = HashChain::computeSha256(f2Read.readAll());
    f2Read.close();

    SyncPackageEntity e1;
    e1.packageId = QStringLiteral("pkg-entities");
    e1.entityType = QStringLiteral("deductions");
    e1.filePath = QStringLiteral("deductions.jsonl");
    e1.sha256Hex = f1Sha;
    e1.recordCount = 1;
    e1.verified = false;
    e1.applied = false;

    SyncPackageEntity e2;
    e2.packageId = QStringLiteral("pkg-entities");
    e2.entityType = QStringLiteral("corrections");
    e2.filePath = QStringLiteral("corrections.jsonl");
    e2.sha256Hex = f2Sha;
    e2.recordCount = 1;
    e2.verified = false;
    e2.applied = false;

    auto allOk = m_verifier->verifyAllEntities(tmp.path(), {e1, e2});
    QVERIFY(allOk.isOk());

    e2.sha256Hex = QString(64, QLatin1Char('f'));
    auto allBad = m_verifier->verifyAllEntities(tmp.path(), {e1, e2});
    QVERIFY(allBad.isErr());
    QCOMPARE(allBad.errorCode(), ErrorCode::PackageCorrupt);
}

QTEST_MAIN(TstPackageVerifier)
#include "tst_package_verifier.moc"
