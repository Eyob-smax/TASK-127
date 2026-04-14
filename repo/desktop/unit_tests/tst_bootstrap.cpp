// tst_bootstrap.cpp
// ProctorOps — Qt Test framework bootstrap verification
//
// This test verifies that:
//   1. The Qt Test framework compiles and links correctly.
//   2. The CTest integration runs the test and reports results.
//   3. QCoreApplication and basic Qt types are available.
//
// This is a build-infrastructure test, not a business-logic test.
// Business-logic tests are added as modules are implemented.

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonDocument>
#include <QUuid>
#include <span>

class TstBootstrap : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // Qt framework availability
    void test_qtVersion();
    void test_qstringOperations();
    void test_qdatetimeUtc();
    void test_quuidGeneration();
    void test_qjsonRoundTrip();

    // Build environment
    void test_cppStandard();
};

void TstBootstrap::initTestCase()
{
    qDebug() << "ProctorOps bootstrap test starting";
    qDebug() << "Qt version:" << QT_VERSION_STR;
}

void TstBootstrap::cleanupTestCase()
{
    qDebug() << "ProctorOps bootstrap test complete";
}

void TstBootstrap::test_qtVersion()
{
    // Qt 6.5 or newer is required.
    const int major = QT_VERSION_MAJOR;
    const int minor = QT_VERSION_MINOR;
    QVERIFY2(major == 6, "Qt major version must be 6");
    QVERIFY2(minor >= 5, "Qt minor version must be >= 5 (Qt 6.5+ required)");
}

void TstBootstrap::test_qstringOperations()
{
    const QString s = QStringLiteral("ProctorOps");
    QCOMPARE(s.length(), 10);
    QVERIFY(s.startsWith(QStringLiteral("Proctor")));
    QVERIFY(!s.isEmpty());
}

void TstBootstrap::test_qdatetimeUtc()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVERIFY(now.isValid());
    QCOMPARE(now.timeSpec(), Qt::UTC);
    // Basic sanity: year should be >= 2025
    QVERIFY(now.date().year() >= 2025);
}

void TstBootstrap::test_quuidGeneration()
{
    const QUuid id1 = QUuid::createUuid();
    const QUuid id2 = QUuid::createUuid();
    QVERIFY(!id1.isNull());
    QVERIFY(!id2.isNull());
    QVERIFY(id1 != id2);  // UUIDs must be unique
}

void TstBootstrap::test_qjsonRoundTrip()
{
    QJsonObject obj;
    obj[QStringLiteral("app")] = QStringLiteral("ProctorOps");
    obj[QStringLiteral("version")] = QStringLiteral("0.1.0");
    obj[QStringLiteral("offline")] = true;

    const QByteArray serialized = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    QVERIFY(!serialized.isEmpty());

    QJsonParseError err;
    const QJsonDocument parsed = QJsonDocument::fromJson(serialized, &err);
    QCOMPARE(err.error, QJsonParseError::NoError);
    QCOMPARE(parsed.object()[QStringLiteral("app")].toString(),
             QStringLiteral("ProctorOps"));
    QCOMPARE(parsed.object()[QStringLiteral("offline")].toBool(), true);
}

void TstBootstrap::test_cppStandard()
{
    // C++20 features: verify they compile.
    // Structured bindings
    const auto [a, b] = std::pair<int, int>{1, 2};
    QCOMPARE(a, 1);
    QCOMPARE(b, 2);

    // std::span availability (C++20)
    // If this does not compile, the compiler is not in C++20 mode.
    int arr[] = {10, 20, 30};
    std::span<int> view(arr);
    QCOMPARE(static_cast<int>(view.size()), 3);
    QCOMPARE(view[1], 20);
}

QTEST_GUILESS_MAIN(TstBootstrap)
#include "tst_bootstrap.moc"
