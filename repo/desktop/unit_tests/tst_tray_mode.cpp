// tst_tray_mode.cpp — ProctorOps
// Unit tests for TrayManager state transitions and kiosk mode logic.
// QSystemTrayIcon is created but may not be visually active in headless
// (offscreen) test environments — tests validate state and flag logic only.

#include <QtTest>
#include "tray/TrayManager.h"
#include "windows/MainShell.h"

bool MainShell::promptUnlock()
{
    return true;
}

class TstTrayMode : public QObject {
    Q_OBJECT

private slots:
    void initialStateIsNormal();
    void setStateNormal();
    void setStateLocked();
    void setStateKioskActive();
    void setStateWarning();
    void setStateError();
    void kioskModeFlagInitiallyFalse();
    void enterKioskModeSetsFlagTrue();
    void exitKioskModeSetsFlagFalse();
    void kioskModeToggleEmitsSignal();
    void doubleEnterKioskIsIdempotentFlag();
    void doubleExitKioskIsIdempotentFlag();
};

void TstTrayMode::initialStateIsNormal()
{
    TrayManager tray(nullptr); // null shell → no window interactions
    QCOMPARE(tray.trayState(), TrayManager::TrayState::Normal);
    QVERIFY(!tray.isKioskMode());
}

void TstTrayMode::setStateNormal()
{
    TrayManager tray(nullptr);
    tray.setTrayState(TrayManager::TrayState::Locked);
    tray.setTrayState(TrayManager::TrayState::Normal);
    QCOMPARE(tray.trayState(), TrayManager::TrayState::Normal);
}

void TstTrayMode::setStateLocked()
{
    TrayManager tray(nullptr);
    tray.setTrayState(TrayManager::TrayState::Locked);
    QCOMPARE(tray.trayState(), TrayManager::TrayState::Locked);
}

void TstTrayMode::setStateKioskActive()
{
    TrayManager tray(nullptr);
    tray.setTrayState(TrayManager::TrayState::KioskActive);
    QCOMPARE(tray.trayState(), TrayManager::TrayState::KioskActive);
}

void TstTrayMode::setStateWarning()
{
    TrayManager tray(nullptr);
    tray.setTrayState(TrayManager::TrayState::Warning);
    QCOMPARE(tray.trayState(), TrayManager::TrayState::Warning);
}

void TstTrayMode::setStateError()
{
    TrayManager tray(nullptr);
    tray.setTrayState(TrayManager::TrayState::Error);
    QCOMPARE(tray.trayState(), TrayManager::TrayState::Error);
}

void TstTrayMode::kioskModeFlagInitiallyFalse()
{
    TrayManager tray(nullptr);
    QVERIFY(!tray.isKioskMode());
}

void TstTrayMode::enterKioskModeSetsFlagTrue()
{
    TrayManager tray(nullptr);
    tray.enterKioskMode(); // shell is null → no window calls
    QVERIFY(tray.isKioskMode());
    QCOMPARE(tray.trayState(), TrayManager::TrayState::KioskActive);
}

void TstTrayMode::exitKioskModeSetsFlagFalse()
{
    TrayManager tray(nullptr);
    tray.enterKioskMode();
    tray.exitKioskMode();
    QVERIFY(!tray.isKioskMode());
    QCOMPARE(tray.trayState(), TrayManager::TrayState::Normal);
}

void TstTrayMode::kioskModeToggleEmitsSignal()
{
    TrayManager tray(nullptr);
    QList<bool> received;
    connect(&tray, &TrayManager::kioskModeToggled,
            [&received](bool active){ received.append(active); });

    tray.enterKioskMode();
    tray.exitKioskMode();

    QCOMPARE(received.size(), 2);
    QVERIFY(received.at(0));  // true on enter
    QVERIFY(!received.at(1)); // false on exit
}

void TstTrayMode::doubleEnterKioskIsIdempotentFlag()
{
    TrayManager tray(nullptr);
    tray.enterKioskMode();
    tray.enterKioskMode();
    QVERIFY(tray.isKioskMode());
}

void TstTrayMode::doubleExitKioskIsIdempotentFlag()
{
    TrayManager tray(nullptr);
    tray.enterKioskMode();
    tray.exitKioskMode();
    tray.exitKioskMode(); // should not crash or flip flag
    QVERIFY(!tray.isKioskMode());
}

QTEST_MAIN(TstTrayMode)
#include "tst_tray_mode.moc"
