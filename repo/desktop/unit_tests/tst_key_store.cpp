// tst_key_store.cpp — ProctorOps
// Unit tests for KeyStore (master key and named-secret persistence).
//
// KeyStore uses DPAPI on Windows and an XOR-based file obfuscation fallback on
// Linux/Docker. These tests exercise the public surface of the Linux fallback
// code path, since the test container is Linux. File I/O goes to a QTemporaryDir
// so that tests are fully isolated from developer state.

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

#include "crypto/KeyStore.h"
#include "utils/Validation.h"

class TstKeyStore : public QObject
{
    Q_OBJECT

private slots:
    void init();

    // ── getMasterKey ─────────────────────────────────────────────────────
    void test_getMasterKey_firstCallGeneratesKey();
    void test_getMasterKey_persistsBetweenCalls();
    void test_getMasterKey_writesKeyFile();

    // ── rotateMasterKey ──────────────────────────────────────────────────
    void test_rotateMasterKey_happyPath();
    void test_rotateMasterKey_rejectsWrongSize_data();
    void test_rotateMasterKey_rejectsWrongSize();
    void test_rotateMasterKey_createsPrevBackup();
    void test_rotateMasterKey_subsequentGetReturnsNewKey();

    // ── storeSecret / getSecret ──────────────────────────────────────────
    void test_storeSecret_roundTrip();
    void test_storeSecret_rejectsEmptyName_data();
    void test_storeSecret_rejectsEmptyName();
    void test_storeSecret_rejectsEmptyPayload();
    void test_storeSecret_overwritesExisting();
    void test_getSecret_rejectsEmptyName_data();
    void test_getSecret_rejectsEmptyName();
    void test_getSecret_notFoundReturnsKeyNotFound();
    void test_secretName_normalizesSpecialChars();

private:
    QTemporaryDir m_tempDir;
    QString storageDir() const { return m_tempDir.path(); }
};

// ── init ─────────────────────────────────────────────────────────────────────

void TstKeyStore::init()
{
    // Fresh temp dir for every test — KeyStore persists to disk, so a shared
    // dir would leak master/secret state across tests.
    QVERIFY(m_tempDir.isValid());
    QDir dir(m_tempDir.path());
    const auto entries = dir.entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QString& entry : entries)
        QFile::remove(dir.filePath(entry));
}

// ── getMasterKey ─────────────────────────────────────────────────────────────

void TstKeyStore::test_getMasterKey_firstCallGeneratesKey()
{
    KeyStore store(storageDir());
    const auto result = store.getMasterKey();
    QVERIFY(result.isOk());
    QCOMPARE(result.value().size(), Validation::AesGcmKeyBytes);
}

void TstKeyStore::test_getMasterKey_persistsBetweenCalls()
{
    KeyStore store(storageDir());
    const auto first  = store.getMasterKey();
    const auto second = store.getMasterKey();
    QVERIFY(first.isOk());
    QVERIFY(second.isOk());
    QCOMPARE(first.value(), second.value());
}

void TstKeyStore::test_getMasterKey_writesKeyFile()
{
    KeyStore store(storageDir());
    QVERIFY(store.getMasterKey().isOk());
    const QString expectedPath = QDir(storageDir()).filePath(QStringLiteral("master.key"));
    QVERIFY2(QFile::exists(expectedPath), qPrintable(expectedPath));
}

// ── rotateMasterKey ──────────────────────────────────────────────────────────

void TstKeyStore::test_rotateMasterKey_happyPath()
{
    KeyStore store(storageDir());
    // Ensure an initial key exists so rotation has a prior key to back up.
    QVERIFY(store.getMasterKey().isOk());

    const QByteArray newKey(Validation::AesGcmKeyBytes, '\x42');
    const auto result = store.rotateMasterKey(newKey);
    QVERIFY(result.isOk());
}

void TstKeyStore::test_rotateMasterKey_rejectsWrongSize_data()
{
    QTest::addColumn<int>("size");
    QTest::newRow("zero")       << 0;
    QTest::newRow("too-short")  << Validation::AesGcmKeyBytes - 1;
    QTest::newRow("too-long")   << Validation::AesGcmKeyBytes + 1;
    QTest::newRow("one-byte")   << 1;
}

void TstKeyStore::test_rotateMasterKey_rejectsWrongSize()
{
    QFETCH(int, size);
    KeyStore store(storageDir());
    const auto result = store.rotateMasterKey(QByteArray(size, '\x01'));
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstKeyStore::test_rotateMasterKey_createsPrevBackup()
{
    KeyStore store(storageDir());
    QVERIFY(store.getMasterKey().isOk());

    const QByteArray newKey(Validation::AesGcmKeyBytes, '\x7a');
    QVERIFY(store.rotateMasterKey(newKey).isOk());

    const QString prevPath = QDir(storageDir()).filePath(QStringLiteral("master.key.prev"));
    QVERIFY2(QFile::exists(prevPath), "rotation must back up the previous key to master.key.prev");
}

void TstKeyStore::test_rotateMasterKey_subsequentGetReturnsNewKey()
{
    KeyStore store(storageDir());
    QVERIFY(store.getMasterKey().isOk());

    const QByteArray newKey(Validation::AesGcmKeyBytes, '\x5c');
    QVERIFY(store.rotateMasterKey(newKey).isOk());

    const auto reread = store.getMasterKey();
    QVERIFY(reread.isOk());
    QCOMPARE(reread.value(), newKey);
}

// ── storeSecret / getSecret ──────────────────────────────────────────────────

void TstKeyStore::test_storeSecret_roundTrip()
{
    KeyStore store(storageDir());
    const QByteArray payload = QByteArrayLiteral("\x01\x02\x03\x04super-secret");

    QVERIFY(store.storeSecret(QStringLiteral("signing-key"), payload).isOk());

    const auto read = store.getSecret(QStringLiteral("signing-key"));
    QVERIFY(read.isOk());
    QCOMPARE(read.value(), payload);
}

void TstKeyStore::test_storeSecret_rejectsEmptyName_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("empty")        << QString();
    QTest::newRow("whitespace")   << QStringLiteral("   ");
    QTest::newRow("tab")          << QStringLiteral("\t");
    QTest::newRow("newline")      << QStringLiteral("\n");
}

void TstKeyStore::test_storeSecret_rejectsEmptyName()
{
    QFETCH(QString, name);
    KeyStore store(storageDir());
    const auto result = store.storeSecret(name, QByteArrayLiteral("x"));
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstKeyStore::test_storeSecret_rejectsEmptyPayload()
{
    KeyStore store(storageDir());
    const auto result = store.storeSecret(QStringLiteral("foo"), QByteArray());
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstKeyStore::test_storeSecret_overwritesExisting()
{
    KeyStore store(storageDir());
    const QString name = QStringLiteral("api-token");
    QVERIFY(store.storeSecret(name, QByteArrayLiteral("first")).isOk());
    QVERIFY(store.storeSecret(name, QByteArrayLiteral("second")).isOk());

    const auto read = store.getSecret(name);
    QVERIFY(read.isOk());
    QCOMPARE(read.value(), QByteArrayLiteral("second"));
}

void TstKeyStore::test_getSecret_rejectsEmptyName_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("empty")      << QString();
    QTest::newRow("whitespace") << QStringLiteral("   ");
}

void TstKeyStore::test_getSecret_rejectsEmptyName()
{
    QFETCH(QString, name);
    KeyStore store(storageDir());
    const auto result = store.getSecret(name);
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::ValidationFailed);
}

void TstKeyStore::test_getSecret_notFoundReturnsKeyNotFound()
{
    KeyStore store(storageDir());
    const auto result = store.getSecret(QStringLiteral("never-stored"));
    QVERIFY(result.isErr());
    QCOMPARE(result.errorCode(), ErrorCode::KeyNotFound);
}

void TstKeyStore::test_secretName_normalizesSpecialChars()
{
    KeyStore store(storageDir());
    // Path separators, spaces, and quotes should all be normalized to '_' so
    // they can never escape the storage directory.
    const QString unsafe = QStringLiteral("../evil name/with:weird*chars?");
    const QByteArray payload = QByteArrayLiteral("payload");
    QVERIFY(store.storeSecret(unsafe, payload).isOk());

    // The stored file must live inside storageDir(), not elsewhere on disk.
    // Include Hidden because normalized names that begin with '.' create
    // dotfiles on Linux filesystems.
    QDir dir(storageDir());
    const QStringList secretFiles = dir.entryList(QStringList{QStringLiteral("*.secret")},
                                                  QDir::Files | QDir::Hidden);
    QCOMPARE(secretFiles.size(), 1);

    // Retrieval must succeed using the same input name.
    const auto read = store.getSecret(unsafe);
    QVERIFY(read.isOk());
    QCOMPARE(read.value(), payload);
}

QTEST_APPLESS_MAIN(TstKeyStore)
#include "tst_key_store.moc"
