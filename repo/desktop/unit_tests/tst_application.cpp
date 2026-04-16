// tst_application.cpp — ProctorOps
// Unit tests for Application startup lifecycle and crash detection behavior.

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QSqlQuery>
#include <QSettings>

#include "app/Application.h"

class TstApplication : public QObject {
    Q_OBJECT

private slots:
    void test_initialize_createsDatabaseAndLifecycle();
    void test_recordCleanShutdown_closesLifecycleRecord();
};

void TstApplication::test_initialize_createsDatabaseAndLifecycle()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QSettings settings(QStringLiteral("ProctorOps"), QStringLiteral("proctorops"));
    settings.setValue(QStringLiteral("database/path"), tmp.path() + QStringLiteral("/proctorops.db"));
    settings.setValue(QStringLiteral("database/migration_dir"), QStringLiteral(SOURCE_ROOT "/database/migrations"));

    int argc = 1;
    char arg0[] = "tst_application";
    char* argv[] = {arg0, nullptr};
    Application app(argc, argv);

    QVERIFY(app.initialize());
    QVERIFY(app.database().isOpen());
    QVERIFY(app.coldStartMs() >= 0);

    QSqlQuery q(app.database());
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM app_lifecycle")));
    QVERIFY(q.next());
    QVERIFY(q.value(0).toInt() >= 1);

    settings.remove(QStringLiteral("database/path"));
    settings.remove(QStringLiteral("database/migration_dir"));
}

void TstApplication::test_recordCleanShutdown_closesLifecycleRecord()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QSettings settings(QStringLiteral("ProctorOps"), QStringLiteral("proctorops"));
    settings.setValue(QStringLiteral("database/path"), tmp.path() + QStringLiteral("/proctorops.db"));
    settings.setValue(QStringLiteral("database/migration_dir"), QStringLiteral(SOURCE_ROOT "/database/migrations"));

    int argc = 1;
    char arg0[] = "tst_application";
    char* argv[] = {arg0, nullptr};
    Application app(argc, argv);

    QVERIFY(app.initialize());
    app.recordCleanShutdown();

    QSqlQuery q(app.database());
    QVERIFY(q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM app_lifecycle WHERE clean_shutdown_at IS NULL")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    settings.remove(QStringLiteral("database/path"));
    settings.remove(QStringLiteral("database/migration_dir"));
}

QTEST_APPLESS_MAIN(TstApplication)
#include "tst_application.moc"
