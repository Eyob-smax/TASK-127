#pragma once
// CheckInWindow.h — ProctorOps
// On-site entry validation window.
//
// Provides three member-resolution paths via a tab widget:
//   Tab 0 — Barcode: USB HID scanner input (BarcodeInput, fires on keystroke timing)
//   Tab 1 — Member ID: manual typed entry
//   Tab 2 — Mobile: typed and normalized to (###) ###-####
//
// Result panel renders the outcome of the most recent check-in:
//   Success      — masked name, remaining balance, timestamp
//   Frozen       — member blocked with freeze reason
//   Term expired — term-card date range displayed
//   Punch exhausted — zero-balance message
//   Duplicate    — suppressed; shows time of last success
//   Failed       — generic failure reason
//
// Corrections (SecurityAdministrator only):
//   A "Request Correction" button is shown after a successful check-in and opens
//   a modal that invokes CheckInService::requestCorrection().

#include <QWidget>
#include <QString>
#include "models/CheckIn.h"
#include "models/Member.h"

class QTabWidget;
class QLabel;
class QLineEdit;
class QPushButton;
class QGroupBox;
class QComboBox;
struct AppContext;

class CheckInWindow : public QWidget {
    Q_OBJECT

public:
    explicit CheckInWindow(AppContext& ctx, QWidget* parent = nullptr);

    static constexpr const char* WindowId = "window.checkin";

public slots:
    /// Invoked by MainShell router action "member.mask_pii".
    /// Copies the last checked-in member's identifier in masked form.
    Q_INVOKABLE void maskSelectedPii();

    /// Invoked by MainShell router action "member.export_request".
    /// Initiates an export request for the last checked-in member.
    Q_INVOKABLE void exportSelectedForRequest();

private slots:
    void onBarcodeScanned(const QString& barcode);
    void onMemberIdCheckIn();
    void onMobileCheckIn();
    void onRequestCorrection();

private:
    void setupUi();
    void runCheckIn(const QString& rawValue, MemberIdentifier::Type idType);
    void showSuccess(const CheckInResult& result, const QString& deductionEventId);
    void showFailure(CheckInStatus status, const QString& reason);
    void clearResult();

    AppContext& m_ctx;

    // Session ID field (operator-entered exam/roster reference)
    QLineEdit*  m_sessionIdEdit{nullptr};

    // Barcode tab
    QLineEdit*  m_barcodeDisplay{nullptr};   // read-only; shows last scanned value

    // Member ID tab
    QLineEdit*  m_memberIdEdit{nullptr};
    QPushButton* m_memberIdBtn{nullptr};

    // Mobile tab
    QLineEdit*  m_mobileEdit{nullptr};
    QPushButton* m_mobileBtn{nullptr};

    // Result panel
    QGroupBox*  m_resultBox{nullptr};
    QLabel*     m_resultIconLabel{nullptr};
    QLabel*     m_resultHeadline{nullptr};
    QLabel*     m_resultDetail{nullptr};
    QPushButton* m_correctionBtn{nullptr};

    // Last successful check-in (for corrections and context-menu actions)
    QString     m_lastDeductionEventId;
    QString     m_lastMemberId;
    bool        m_lastWasSuccess{false};
};
