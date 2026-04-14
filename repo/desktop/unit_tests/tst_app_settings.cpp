// tst_app_settings.cpp
// ProctorOps — AppSettings unit tests
//
// Verifies that AppSettings (QSettings-backed application preferences) returns
// documented default values, correctly round-trips mutations, and does not
// embed absolute developer paths. No secrets are stored in AppSettings.
//
// QSettings scope: Qt.IniFormat in a temp location (IsolatedTestScope below)
// so tests do not pollute the developer machine's registry / AppData.

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QString>
#include <QByteArray>

#include "app/AppSettings.h"

class TstAppSettings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // Default value correctness
    void test_databasePath_defaultIsNonEmpty();
    void test_migrationDir_defaultIsNonEmpty();
    void test_logLevel_defaultIsInfo();
    void test_kioskMode_defaultIsFalse();

    // Round-trip mutation
    void test_databasePath_roundTrip();
    void test_logLevel_roundTrip();
    void test_kioskMode_roundTrip();
    void test_mainWindowGeometry_roundTrip();

    // Path honesty: no hard-coded developer paths
    void test_databasePath_containsNoDevPath();
    void test_migrationDir_containsNoDevPath();

private:
    QTemporaryDir m_tempDir;
};

// ── init / cleanup ────────────────────────────────────────────────────────────

void TstAppSettings::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    // Point QSettings to a temp INI file so tests are isolated from
    // the developer machine's registry or AppData.
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
}

void TstAppSettings::cleanupTestCase()
{
    // Temp dir cleaned up automatically by RAII destructor.
}

// ── Default value tests ───────────────────────────────────────────────────────

void TstAppSettings::test_databasePath_defaultIsNonEmpty()
{
    AppSettings s;
    const QString path = s.databasePath();
    QVERIFY2(!path.isEmpty(), "Default database path must not be empty");
}

void TstAppSettings::test_migrationDir_defaultIsNonEmpty()
{
    AppSettings s;
    const QString dir = s.migrationDir();
    QVERIFY2(!dir.isEmpty(), "Default migration dir must not be empty");
}

void TstAppSettings::test_logLevel_defaultIsInfo()
{
    AppSettings s;
    QCOMPARE(s.logLevel(), QStringLiteral("info"));
}

void TstAppSettings::test_kioskMode_defaultIsFalse()
{
    AppSettings s;
    QVERIFY2(!s.kioskMode(), "Kiosk mode must be disabled by default");
}

// ── Round-trip mutation tests ─────────────────────────────────────────────────

void TstAppSettings::test_databasePath_roundTrip()
{
    AppSettings s;
    const QString path = QStringLiteral("/tmp/proctorops_test.db");
    s.setDatabasePath(path);
    QCOMPARE(s.databasePath(), path);
}

void TstAppSettings::test_logLevel_roundTrip()
{
    AppSettings s;
    s.setLogLevel(QStringLiteral("debug"));
    QCOMPARE(s.logLevel(), QStringLiteral("debug"));

    s.setLogLevel(QStringLiteral("warn"));
    QCOMPARE(s.logLevel(), QStringLiteral("warn"));
}

void TstAppSettings::test_kioskMode_roundTrip()
{
    AppSettings s;
    s.setKioskMode(true);
    QVERIFY(s.kioskMode());

    s.setKioskMode(false);
    QVERIFY(!s.kioskMode());
}

void TstAppSettings::test_mainWindowGeometry_roundTrip()
{
    AppSettings s;
    const QByteArray geom = QByteArray::fromHex("deadbeef01020304");
    s.setMainWindowGeometry(geom);
    QCOMPARE(s.mainWindowGeometry(), geom);
}

// ── Path honesty tests ────────────────────────────────────────────────────────

void TstAppSettings::test_databasePath_containsNoDevPath()
{
    AppSettings s;
    const QString path = s.databasePath();
    // Default path must not embed common developer machine roots.
    // (These checks are heuristic; they catch the most common mistakes.)
    QVERIFY2(!path.startsWith(QStringLiteral("C:\\Users\\")) || path.contains(QStringLiteral("AppData")),
             "Default database path must use QStandardPaths, not a hard-coded developer path");
    QVERIFY2(!path.startsWith(QStringLiteral("/home/")),
             "Default database path must use QStandardPaths, not a hard-coded /home/ path");
}

void TstAppSettings::test_migrationDir_containsNoDevPath()
{
    AppSettings s;
    const QString dir = s.migrationDir();
    // Migration dir defaults to applicationDirPath() + /migrations.
    // In test context applicationDirPath is the test executable directory.
    // We just check it does not embed a specific developer username.
    QVERIFY2(!dir.isEmpty(), "Migration dir must not be empty");
    QVERIFY2(!dir.contains(QStringLiteral("C:\\Users\\OMEN\\")),
             "Migration dir must not embed a hard-coded developer path");
}

QTEST_GUILESS_MAIN(TstAppSettings)
#include "tst_app_settings.moc"
