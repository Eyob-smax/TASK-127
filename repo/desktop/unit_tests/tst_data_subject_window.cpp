// tst_data_subject_window.cpp — ProctorOps
// UI structure tests for DataSubjectWindow with a minimal AppContext.

#include <QtTest/QtTest>
#include <QTabWidget>
#include <QPushButton>

#include "windows/DataSubjectWindow.h"
#include "AppContextTestTypes.h"

class TstDataSubjectWindow : public QObject
{
    Q_OBJECT

private slots:
    void test_windowTitleAndTabs();
    void test_rowScopedButtonsInitiallyDisabled();
};

void TstDataSubjectWindow::test_windowTitleAndTabs()
{
    AppContext ctx;
    DataSubjectWindow win(ctx);

    QCOMPARE(win.windowTitle(), QStringLiteral("Data Subject Requests"));

    auto* tabs = win.findChild<QTabWidget*>();
    QVERIFY(tabs != nullptr);
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Export Requests (Access)"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("Deletion Requests (Erasure)"));
}

void TstDataSubjectWindow::test_rowScopedButtonsInitiallyDisabled()
{
    AppContext ctx;
    DataSubjectWindow win(ctx);

    QPushButton* fulfillBtn = nullptr;
    QPushButton* rejectExportBtn = nullptr;
    QPushButton* approveBtn = nullptr;
    QPushButton* completeBtn = nullptr;
    QPushButton* rejectDeletionBtn = nullptr;

    for (QPushButton* btn : win.findChildren<QPushButton*>()) {
        if (btn->text() == QStringLiteral("Fulfill…")) {
            fulfillBtn = btn;
        }
        if (btn->text() == QStringLiteral("Reject")) {
            if (!rejectExportBtn) {
                rejectExportBtn = btn;
            } else {
                rejectDeletionBtn = btn;
            }
        }
        if (btn->text() == QStringLiteral("Approve…")) {
            approveBtn = btn;
        }
        if (btn->text() == QStringLiteral("Complete Deletion…")) {
            completeBtn = btn;
        }
    }

    QVERIFY(fulfillBtn != nullptr);
    QVERIFY(rejectExportBtn != nullptr);
    QVERIFY(approveBtn != nullptr);
    QVERIFY(completeBtn != nullptr);
    QVERIFY(rejectDeletionBtn != nullptr);

    QVERIFY(!fulfillBtn->isEnabled());
    QVERIFY(!rejectExportBtn->isEnabled());
    QVERIFY(!approveBtn->isEnabled());
    QVERIFY(!completeBtn->isEnabled());
    QVERIFY(!rejectDeletionBtn->isEnabled());
}

QTEST_MAIN(TstDataSubjectWindow)
#include "tst_data_subject_window.moc"
