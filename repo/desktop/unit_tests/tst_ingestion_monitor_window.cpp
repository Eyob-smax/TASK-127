// tst_ingestion_monitor_window.cpp — ProctorOps
// UI structure tests for IngestionMonitorWindow with a minimal AppContext.

#include <QtTest/QtTest>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>

#include "windows/IngestionMonitorWindow.h"
#include "AppContextTestTypes.h"

class TstIngestionMonitorWindow : public QObject
{
    Q_OBJECT

private slots:
    void test_windowStructure();
    void test_noServiceStatusMessage();
};

void TstIngestionMonitorWindow::test_windowStructure()
{
    AppContext ctx;
    IngestionMonitorWindow win(ctx);

    QCOMPARE(win.windowTitle(), QStringLiteral("Ingestion Monitor"));

    auto tables = win.findChildren<QTableWidget*>();
    QVERIFY(!tables.isEmpty());
    QCOMPARE(tables.first()->columnCount(), 7);

    bool foundCancel = false;
    for (QPushButton* btn : win.findChildren<QPushButton*>()) {
        if (btn->text() == QStringLiteral("Cancel Selected Job")) {
            foundCancel = true;
            QVERIFY(!btn->isEnabled());
            break;
        }
    }
    QVERIFY(foundCancel);
}

void TstIngestionMonitorWindow::test_noServiceStatusMessage()
{
    AppContext ctx;
    IngestionMonitorWindow win(ctx);
    win.show();

    QTest::qWait(20);

    bool hasUnavailableMessage = false;
    for (QLabel* label : win.findChildren<QLabel*>()) {
        if (label->text() == QStringLiteral("Ingestion service not available.")) {
            hasUnavailableMessage = true;
            break;
        }
    }

    QVERIFY(hasUnavailableMessage);
}

QTEST_MAIN(TstIngestionMonitorWindow)
#include "tst_ingestion_monitor_window.moc"
