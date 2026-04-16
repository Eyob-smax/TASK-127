// tst_app_bootstrap.cpp — ProctorOps
// Unit tests for startup AppContext wiring previously held in main.cpp.

#include <QtTest/QtTest>

#include <QSqlDatabase>
#include <QStandardPaths>

#include "app/AppBootstrap.h"
#include "app/AppContext.h"
#include "app/AppSettings.h"

class TstAppBootstrap : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void test_buildAppContext_createsInfrastructure();
};

void TstAppBootstrap::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TstAppBootstrap::test_buildAppContext_createsInfrastructure()
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                QStringLiteral("tst_app_bootstrap"));
    db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(db.open());

    AppSettings settings;
    std::unique_ptr<AppContext> ctx = buildAppContext(db, settings);

    QVERIFY(ctx != nullptr);
    QVERIFY(ctx->keyStore != nullptr);
    QVERIFY(ctx->cipher != nullptr);

    QVERIFY(ctx->userRepo != nullptr);
    QVERIFY(ctx->auditRepo != nullptr);
    QVERIFY(ctx->questionRepo != nullptr);
    QVERIFY(ctx->kpRepo != nullptr);
    QVERIFY(ctx->memberRepo != nullptr);
    QVERIFY(ctx->checkInRepo != nullptr);
    QVERIFY(ctx->ingestionRepo != nullptr);
    QVERIFY(ctx->syncRepo != nullptr);
    QVERIFY(ctx->updateRepo != nullptr);

    QVERIFY(ctx->auditService != nullptr);
    QVERIFY(ctx->authService != nullptr);
    QVERIFY(ctx->questionService != nullptr);
    QVERIFY(ctx->checkInService != nullptr);
    QVERIFY(ctx->ingestionService != nullptr);
    QVERIFY(ctx->syncService != nullptr);
    QVERIFY(ctx->dataSubjectService != nullptr);
    QVERIFY(ctx->updateService != nullptr);
    QVERIFY(ctx->jobScheduler != nullptr);

    db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("tst_app_bootstrap"));
}

QTEST_APPLESS_MAIN(TstAppBootstrap)
#include "tst_app_bootstrap.moc"
