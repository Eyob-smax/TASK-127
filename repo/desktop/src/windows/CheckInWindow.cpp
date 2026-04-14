// CheckInWindow.cpp — ProctorOps

#include "windows/CheckInWindow.h"
#include "app/AppContext.h"
#include "services/CheckInService.h"
#include "services/DataSubjectService.h"
#include "utils/ClipboardGuard.h"
#include "widgets/BarcodeInput.h"
#include "models/CommonTypes.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDateTime>
#include <QLocale>
#include <QInputDialog>
#include <QMessageBox>

CheckInWindow::CheckInWindow(AppContext& ctx, QWidget* parent)
    : QWidget(parent), m_ctx(ctx)
{
    setObjectName(QLatin1String(WindowId));
    setWindowTitle(QStringLiteral("Check-In Station"));
    setupUi();
}

void CheckInWindow::setupUi()
{
    auto* root = new QVBoxLayout(this);
    root->setSpacing(12);

    // ── Session ID ────────────────────────────────────────────────────────────
    auto* sessionRow = new QHBoxLayout;
    sessionRow->addWidget(new QLabel(QStringLiteral("Session / Roster ID:"), this));
    m_sessionIdEdit = new QLineEdit(this);
    m_sessionIdEdit->setPlaceholderText(QStringLiteral("Enter session or roster identifier"));
    sessionRow->addWidget(m_sessionIdEdit, 1);
    root->addLayout(sessionRow);

    // ── Input tabs ────────────────────────────────────────────────────────────
    auto* tabs = new QTabWidget(this);

    // Tab 0 — Barcode
    auto* barcodeWidget = new QWidget(tabs);
    auto* barcodeLayout = new QVBoxLayout(barcodeWidget);
    barcodeLayout->addWidget(
        new QLabel(QStringLiteral("Point the USB barcode scanner at a member card."),
                   barcodeWidget));
    m_barcodeDisplay = new QLineEdit(barcodeWidget);
    m_barcodeDisplay->setReadOnly(true);
    m_barcodeDisplay->setPlaceholderText(QStringLiteral("Waiting for scan…"));
    barcodeLayout->addWidget(m_barcodeDisplay);
    barcodeLayout->addStretch();
    tabs->addTab(barcodeWidget, QStringLiteral("Barcode"));

    // Install BarcodeInput on the whole window (event filter on this widget)
    auto* scanner = new BarcodeInput(this);
    scanner->installOn(this);
    connect(scanner, &BarcodeInput::barcodeScanned,
            this,    &CheckInWindow::onBarcodeScanned);

    // Tab 1 — Member ID
    auto* memberWidget = new QWidget(tabs);
    auto* memberLayout = new QVBoxLayout(memberWidget);
    auto* memberForm   = new QFormLayout;
    m_memberIdEdit = new QLineEdit(memberWidget);
    m_memberIdEdit->setPlaceholderText(QStringLiteral("e.g. M-000123"));
    memberForm->addRow(QStringLiteral("Member ID:"), m_memberIdEdit);
    memberLayout->addLayout(memberForm);
    m_memberIdBtn = new QPushButton(QStringLiteral("Check In"), memberWidget);
    memberLayout->addWidget(m_memberIdBtn);
    memberLayout->addStretch();
    tabs->addTab(memberWidget, QStringLiteral("Member ID"));
    connect(m_memberIdBtn,  &QPushButton::clicked,     this, &CheckInWindow::onMemberIdCheckIn);
    connect(m_memberIdEdit, &QLineEdit::returnPressed, this, &CheckInWindow::onMemberIdCheckIn);

    // Tab 2 — Mobile
    auto* mobileWidget = new QWidget(tabs);
    auto* mobileLayout = new QVBoxLayout(mobileWidget);
    auto* mobileForm   = new QFormLayout;
    m_mobileEdit = new QLineEdit(mobileWidget);
    m_mobileEdit->setPlaceholderText(QStringLiteral("(###) ###-####"));
    mobileForm->addRow(QStringLiteral("Mobile:"), m_mobileEdit);
    mobileLayout->addLayout(mobileForm);
    m_mobileBtn = new QPushButton(QStringLiteral("Check In"), mobileWidget);
    mobileLayout->addWidget(m_mobileBtn);
    mobileLayout->addStretch();
    tabs->addTab(mobileWidget, QStringLiteral("Mobile"));
    connect(m_mobileBtn,  &QPushButton::clicked,     this, &CheckInWindow::onMobileCheckIn);
    connect(m_mobileEdit, &QLineEdit::returnPressed, this, &CheckInWindow::onMobileCheckIn);

    root->addWidget(tabs);

    // ── Result panel ──────────────────────────────────────────────────────────
    m_resultBox = new QGroupBox(QStringLiteral("Result"), this);
    auto* resultLayout = new QVBoxLayout(m_resultBox);

    auto* headRow = new QHBoxLayout;
    m_resultIconLabel = new QLabel(this);
    m_resultIconLabel->setFixedWidth(24);
    m_resultHeadline  = new QLabel(QStringLiteral("Ready"), this);
    QFont hf = m_resultHeadline->font();
    hf.setPointSize(hf.pointSize() + 2);
    hf.setBold(true);
    m_resultHeadline->setFont(hf);
    headRow->addWidget(m_resultIconLabel);
    headRow->addWidget(m_resultHeadline, 1);
    resultLayout->addLayout(headRow);

    m_resultDetail = new QLabel(this);
    m_resultDetail->setWordWrap(true);
    resultLayout->addWidget(m_resultDetail);

    m_correctionBtn = new QPushButton(QStringLiteral("Request Correction…"), this);
    m_correctionBtn->setVisible(false);
    resultLayout->addWidget(m_correctionBtn);
    connect(m_correctionBtn, &QPushButton::clicked,
            this, &CheckInWindow::onRequestCorrection);

    root->addWidget(m_resultBox);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void CheckInWindow::onBarcodeScanned(const QString& barcode)
{
    m_barcodeDisplay->setText(barcode);
    runCheckIn(barcode, MemberIdentifier::Type::Barcode);
}

void CheckInWindow::onMemberIdCheckIn()
{
    const QString val = m_memberIdEdit->text().trimmed();
    if (val.isEmpty()) return;
    runCheckIn(val, MemberIdentifier::Type::MemberId);
}

void CheckInWindow::onMobileCheckIn()
{
    const QString val = m_mobileEdit->text().trimmed();
    if (val.isEmpty()) return;
    runCheckIn(val, MemberIdentifier::Type::Mobile);
}

void CheckInWindow::onRequestCorrection()
{
    if (m_lastDeductionEventId.isEmpty()) return;

    bool ok = false;
    const QString rationale = QInputDialog::getText(
        this,
        QStringLiteral("Request Correction"),
        QStringLiteral("Describe the reason for this correction:"),
        QLineEdit::Normal, {}, &ok);
    if (!ok || rationale.trimmed().isEmpty()) return;

    auto result = m_ctx.checkInService->requestCorrection(
        m_lastDeductionEventId,
        rationale.trimmed(),
        m_ctx.session.userId);

    if (result.isOk()) {
        QMessageBox::information(this,
            QStringLiteral("Correction Requested"),
            QStringLiteral("Correction request submitted and is pending security administrator review."));
        m_correctionBtn->setVisible(false);
    } else {
        QMessageBox::warning(this,
            QStringLiteral("Correction Failed"),
            QStringLiteral("Could not submit correction request: ") + result.errorMessage());
    }
}

// ── Private helpers ───────────────────────────────────────────────────────────

void CheckInWindow::runCheckIn(const QString& rawValue, MemberIdentifier::Type idType)
{
    const QString sessionId = m_sessionIdEdit->text().trimmed();
    if (sessionId.isEmpty()) {
        m_resultHeadline->setText(QStringLiteral("Session ID required"));
        m_resultDetail->setText(
            QStringLiteral("Enter a session or roster identifier before checking in."));
        m_resultIconLabel->setText(QStringLiteral("⚠"));
        m_correctionBtn->setVisible(false);
        m_lastWasSuccess = false;
        return;
    }

    MemberIdentifier identifier{idType, rawValue};
    auto result = m_ctx.checkInService->checkIn(
        identifier, sessionId, m_ctx.session.userId);

    if (result.isOk()) {
        showSuccess(result.value(), result.value().deductionEventId);
    } else {
        // Map typed ErrorCode from CheckInService to CheckInStatus so the UI
        // displays the appropriate scenario-specific guidance to the operator.
        CheckInStatus status = CheckInStatus::Failed;
        switch (result.errorCode()) {
        case ErrorCode::DuplicateCheckIn:  status = CheckInStatus::DuplicateBlocked;  break;
        case ErrorCode::AccountFrozen:     status = CheckInStatus::FrozenBlocked;     break;
        case ErrorCode::TermCardExpired:   status = CheckInStatus::TermCardExpired;   break;
        case ErrorCode::TermCardMissing:   status = CheckInStatus::TermCardMissing;   break;
        case ErrorCode::PunchCardExhausted:status = CheckInStatus::PunchCardExhausted;break;
        default:                           status = CheckInStatus::Failed;            break;
        }
        showFailure(status, result.errorMessage());
    }
}

void CheckInWindow::showSuccess(const CheckInResult& r, const QString& deductionEventId)
{
    m_lastDeductionEventId = deductionEventId;
    m_lastMemberId = r.memberId;
    m_lastWasSuccess = true;

    m_resultIconLabel->setText(QStringLiteral("✓"));
    m_resultIconLabel->setStyleSheet(QStringLiteral("color: #27ae60; font-size: 18px;"));
    m_resultHeadline->setText(QStringLiteral("Check-In Successful"));
    m_resultHeadline->setStyleSheet(QStringLiteral("color: #27ae60;"));

    m_resultDetail->setText(
        QStringLiteral("Member: %1\nBalance remaining: %2\nTime: %3")
            .arg(r.memberNameMasked)
            .arg(r.remainingBalance)
            .arg(QLocale().toString(r.checkInTimestamp.toLocalTime(), QLocale::ShortFormat)));

    // Service enforces role requirements when the correction is submitted.
    // Show the button for any authenticated operator; unauthorized requests are rejected at service level.
    m_correctionBtn->setVisible(!m_ctx.session.userId.isEmpty());
}

void CheckInWindow::showFailure(CheckInStatus status, const QString& reason)
{
    m_lastWasSuccess = false;
    m_lastDeductionEventId.clear();
    m_lastMemberId.clear();
    m_correctionBtn->setVisible(false);

    m_resultIconLabel->setText(QStringLiteral("✗"));
    m_resultIconLabel->setStyleSheet(QStringLiteral("color: #c0392b; font-size: 18px;"));
    m_resultHeadline->setStyleSheet(QStringLiteral("color: #c0392b;"));

    switch (status) {
    case CheckInStatus::DuplicateBlocked:
        m_resultHeadline->setText(QStringLiteral("Duplicate — Already Checked In"));
        m_resultDetail->setText(
            QStringLiteral("This member already checked in to this session within the last 30 seconds."));
        break;
    case CheckInStatus::FrozenBlocked:
        m_resultHeadline->setText(QStringLiteral("Account Frozen"));
        m_resultDetail->setText(
            QStringLiteral("This member's account is frozen and cannot check in.\n") + reason);
        break;
    case CheckInStatus::TermCardExpired:
        m_resultHeadline->setText(QStringLiteral("Term Card Expired"));
        m_resultDetail->setText(
            QStringLiteral("The member's term card has expired. Contact a security administrator."));
        break;
    case CheckInStatus::TermCardMissing:
        m_resultHeadline->setText(QStringLiteral("No Term Card on File"));
        m_resultDetail->setText(
            QStringLiteral("No valid term card found for this member. Contact a security administrator."));
        break;
    case CheckInStatus::PunchCardExhausted:
        m_resultHeadline->setText(QStringLiteral("No Sessions Remaining"));
        m_resultDetail->setText(
            QStringLiteral("The member's punch card balance is zero. Contact a security administrator to issue a new card."));
        break;
    default:
        m_resultHeadline->setText(QStringLiteral("Check-In Failed"));
        m_resultDetail->setText(reason.isEmpty()
            ? QStringLiteral("Member not found or identifier could not be resolved.")
            : reason);
        break;
    }
}

void CheckInWindow::clearResult()
{
    m_resultIconLabel->clear();
    m_resultIconLabel->setStyleSheet({});
    m_resultHeadline->setText(QStringLiteral("Ready"));
    m_resultHeadline->setStyleSheet({});
    m_resultDetail->clear();
    m_correctionBtn->setVisible(false);
    m_lastDeductionEventId.clear();
    m_lastMemberId.clear();
    m_lastWasSuccess = false;
}

// ── Context-menu action handlers ─────────────────────────────────────────────

void CheckInWindow::maskSelectedPii()
{
    if (!m_lastWasSuccess || m_lastMemberId.isEmpty()) {
        QMessageBox::information(this, tr("Mask PII"),
                                 tr("No active check-in result. Perform a check-in first."));
        return;
    }

    ClipboardGuard::copyMasked(m_lastMemberId);
    QMessageBox::information(this, tr("PII Masked"),
                             tr("Member identifier copied to clipboard in masked form."));
}

void CheckInWindow::exportSelectedForRequest()
{
    if (!m_lastWasSuccess || m_lastMemberId.isEmpty()) {
        QMessageBox::information(this, tr("Export for Request"),
                                 tr("No active check-in result. Perform a check-in first."));
        return;
    }
    if (!m_ctx.dataSubjectService) return;

    bool ok = false;
    const QString rationale = QInputDialog::getText(
        this, tr("Export for Request"),
        tr("Rationale for data export of member %1:").arg(m_lastMemberId),
        QLineEdit::Normal, {}, &ok);
    if (!ok || rationale.trimmed().isEmpty()) return;

    auto res = m_ctx.dataSubjectService->createExportRequest(
        m_lastMemberId, rationale.trimmed(), m_ctx.session.userId);
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Error"), res.errorMessage());
        return;
    }

    QMessageBox::information(this, tr("Export Request Created"),
                             tr("Export request created for member %1.").arg(m_lastMemberId));
}
