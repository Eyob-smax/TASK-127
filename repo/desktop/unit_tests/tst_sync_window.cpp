// tst_sync_window.cpp — ProctorOps
// UI structure tests for SyncWindow with a minimal AppContext.

#include <QtTest/QtTest>
#include <QTabWidget>
#include <QPushButton>
#include <QTableWidget>

#include "windows/SyncWindow.h"
#include "AppContextTestTypes.h"

class TstSyncWindow : public QObject
{
    Q_OBJECT

private slots:
    void test_windowTitleAndTabs();
    void test_buttonsInitialState();
    void test_refreshWithoutServices();
};

void TstSyncWindow::test_windowTitleAndTabs()
{
    AppContext ctx;
    SyncWindow win(ctx);

    QCOMPARE(win.windowTitle(), QStringLiteral("Sync Management"));

    auto* tabs = win.findChild<QTabWidget*>();
    QVERIFY(tabs != nullptr);
    QCOMPARE(tabs->count(), 3);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Packages"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("Conflicts"));
    QCOMPARE(tabs->tabText(2), QStringLiteral("Signing Keys"));
}

void TstSyncWindow::test_buttonsInitialState()
{
    AppContext ctx;
    SyncWindow win(ctx);

    QPushButton* resolveBtn = nullptr;
    QPushButton* revokeBtn = nullptr;

    for (QPushButton* btn : win.findChildren<QPushButton*>()) {
        if (btn->text() == QStringLiteral("Resolve Conflict…")) {
            resolveBtn = btn;
        }
        if (btn->text() == QStringLiteral("Revoke Key")) {
            revokeBtn = btn;
        }
    }

    QVERIFY(resolveBtn != nullptr);
    QVERIFY(revokeBtn != nullptr);
    QVERIFY(!resolveBtn->isEnabled());
    QVERIFY(!revokeBtn->isEnabled());
}

void TstSyncWindow::test_refreshWithoutServices()
{
    AppContext ctx;
    SyncWindow win(ctx);
    win.show();

    QTest::qWait(20);

    const auto tables = win.findChildren<QTableWidget*>();
    QVERIFY(tables.size() >= 3);
    for (QTableWidget* table : tables) {
        QCOMPARE(table->rowCount(), 0);
    }
}

QTEST_MAIN(TstSyncWindow)
#include "tst_sync_window.moc"
