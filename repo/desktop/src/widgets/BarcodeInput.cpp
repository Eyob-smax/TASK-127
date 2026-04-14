// BarcodeInput.cpp — ProctorOps
// USB HID barcode scanner detection via inter-key timing analysis.
// Scanners emit rapid keystrokes (< 50ms apart) ending with Enter/Return.
// Manual typing is slower and buffered characters are discarded on timeout.

#include "BarcodeInput.h"

#include <QWidget>
#include <QKeyEvent>

BarcodeInput::BarcodeInput(QObject* parent)
    : QObject(parent)
{
}

void BarcodeInput::installOn(QWidget* widget)
{
    widget->installEventFilter(this);
}

void BarcodeInput::setMaxInterKeyDelay(int ms)
{
    m_maxInterKeyDelayMs = ms;
}

void BarcodeInput::setMinBarcodeLength(int length)
{
    m_minBarcodeLength = length;
}

bool BarcodeInput::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::KeyPress)
        return QObject::eventFilter(watched, event);

    auto* keyEvent = static_cast<QKeyEvent*>(event);

    // Check if this is a terminator (Enter/Return)
    if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
        if (m_buffer.length() >= m_minBarcodeLength) {
            emit barcodeScanned(m_buffer);
        }
        resetBuffer();
        return true; // consume the Enter key when we have a buffer
    }

    // Get the printable text from the key event
    QString text = keyEvent->text();
    if (text.isEmpty() || !text.at(0).isPrint())
        return QObject::eventFilter(watched, event);

    // Check inter-key timing
    if (m_timerStarted) {
        qint64 elapsed = m_timer.elapsed();
        if (elapsed > m_maxInterKeyDelayMs) {
            // Too slow — this is manual typing, not a scanner
            resetBuffer();
        }
    }

    // Append character and restart timer
    m_buffer.append(text);
    m_timer.restart();
    m_timerStarted = true;

    return false; // don't consume regular key presses (let the widget handle them too)
}

void BarcodeInput::resetBuffer()
{
    m_buffer.clear();
    m_timerStarted = false;
}
