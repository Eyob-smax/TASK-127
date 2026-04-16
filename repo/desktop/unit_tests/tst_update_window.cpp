// tst_update_window.cpp — ProctorOps
// UI structure tests for UpdateWindow with a minimal AppContext.

#include <QtTest/QtTest>
#include <QTabWidget>
#include <QPushButton>

#include "windows/UpdateWindow.h"
#include "AppContextTestTypes.h"

class TstUpdateWindow : public QObject
{
    Q_OBJECT

private slots:
    void test_windowTitleAndTabs();
    void test_actionButtonsInitiallyDisabled();
};

void TstUpdateWindow::test_windowTitleAndTabs()
{
    AppContext ctx;
    UpdateWindow win(ctx);

    QCOMPARE(win.windowTitle(), QStringLiteral("Update & Rollback"));

    auto* tabs = win.findChild<QTabWidget*>();
    QVERIFY(tabs != nullptr);
    QCOMPARE(tabs->count(), 3);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Staged Packages"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("Install History"));
    QCOMPARE(tabs->tabText(2), QStringLiteral("Rollback Records"));
}

void TstUpdateWindow::test_actionButtonsInitiallyDisabled()
{
    AppContext ctx;
    UpdateWindow win(ctx);

    QPushButton* applyBtn = nullptr;
    QPushButton* cancelBtn = nullptr;
    QPushButton* rollbackBtn = nullptr;

    for (QPushButton* btn : win.findChildren<QPushButton*>()) {
        if (btn->text() == QStringLiteral("Apply Package (step-up required)…")) {
            applyBtn = btn;
        }
        if (btn->text() == QStringLiteral("Cancel Staged")) {
            cancelBtn = btn;
        }
        if (btn->text() == QStringLiteral("Roll Back to Selected (step-up required)…")) {
            rollbackBtn = btn;
        }
    }

    QVERIFY(applyBtn != nullptr);
    QVERIFY(cancelBtn != nullptr);
    QVERIFY(rollbackBtn != nullptr);

    QVERIFY(!applyBtn->isEnabled());
    QVERIFY(!cancelBtn->isEnabled());
    QVERIFY(!rollbackBtn->isEnabled());
}

QTEST_MAIN(TstUpdateWindow)
#include "tst_update_window.moc"
