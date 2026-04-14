#pragma once
// MaskedFieldWidget.h — ProctorOps
// PII-masked display widget.
//
// Displays a masked value by default (last 4 characters visible, rest replaced
// with '*'). A "Reveal" button emits revealRequested() — the parent is
// responsible for initiating step-up re-authentication before calling reveal().
// After Validation::StepUpWindowSeconds the value automatically re-masks.
//
// All clipboard operations are routed through ClipboardGuard to prevent raw
// PII from reaching the OS clipboard.

#include <QWidget>
#include <QString>
#include <QTimer>

class QLabel;
class QPushButton;

class MaskedFieldWidget : public QWidget {
    Q_OBJECT

public:
    explicit MaskedFieldWidget(QWidget* parent = nullptr);

    /// Set the PII value. Immediately applies masking.
    void setValue(const QString& value);

    /// True if the full value is currently revealed.
    [[nodiscard]] bool isRevealed() const { return m_revealed; }

    /// Reveal the full value for Validation::StepUpWindowSeconds, then re-mask.
    /// Only call this after step-up authentication has been completed.
    void reveal();

    /// Re-apply masking immediately (e.g. on window focus loss).
    void remask();

signals:
    /// Emitted when the user clicks "Reveal". The parent must complete step-up
    /// before calling reveal() in response.
    void revealRequested();

private slots:
    void onRevealTimeout();
    void onCopyClicked();
    void onRevealClicked();

private:
    void updateDisplay();

    QString      m_value;
    bool         m_revealed{false};
    QTimer       m_revealTimer;
    QLabel*      m_label{nullptr};
    QPushButton* m_copyBtn{nullptr};
    QPushButton* m_revealBtn{nullptr};
};
