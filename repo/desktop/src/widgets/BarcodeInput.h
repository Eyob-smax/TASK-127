#pragma once
// BarcodeInput.h — ProctorOps
// USB HID barcode scanner input abstraction via keystroke timing detection.
// HID scanners send rapid keystrokes terminated by Enter. This class
// distinguishes scanner input from manual typing by inter-key timing.

#include <QObject>
#include <QElapsedTimer>
#include <QString>

class QWidget;
class QKeyEvent;

class BarcodeInput : public QObject {
    Q_OBJECT

public:
    explicit BarcodeInput(QObject* parent = nullptr);

    /// Install this event filter on a widget to capture barcode scanner input.
    void installOn(QWidget* widget);

    /// Maximum delay between keystrokes to be considered scanner input (default: 50ms).
    void setMaxInterKeyDelay(int ms = 50);

    /// Minimum number of characters for a valid barcode (default: 4).
    void setMinBarcodeLength(int length = 4);

signals:
    /// Emitted when a complete barcode is scanned.
    void barcodeScanned(const QString& barcode);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Reset the keystroke buffer.
    void resetBuffer();

    QString       m_buffer;
    QElapsedTimer m_timer;
    int           m_maxInterKeyDelayMs = 50;
    int           m_minBarcodeLength   = 4;
    bool          m_timerStarted       = false;
};
