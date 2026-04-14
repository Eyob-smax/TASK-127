// DataSubjectWindow.cpp — ProctorOps

#include "DataSubjectWindow.h"
#include "app/AppContext.h"
#include "dialogs/StepUpDialog.h"
#include "services/DataSubjectService.h"
#include "utils/ClipboardGuard.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>

DataSubjectWindow::DataSubjectWindow(AppContext& ctx, QWidget* parent)
    : QWidget(parent)
    , m_ctx(ctx)
{
    setWindowTitle(tr("Data Subject Requests"));
    setupUi();

    QTimer::singleShot(0, this, &DataSubjectWindow::onRefreshExportRequests);
    QTimer::singleShot(0, this, &DataSubjectWindow::onRefreshDeletionRequests);
}

void DataSubjectWindow::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    auto* header = new QLabel(
        tr("<b>Data Subject Requests</b> — "
           "GDPR / China MLPS access and erasure workflows. "
           "All operations are bounded to local data; no external transfers occur."), this);
    header->setWordWrap(true);
    mainLayout->addWidget(header);

    auto* tabs = new QTabWidget(this);
    auto* exportTab   = new QWidget();
    auto* deletionTab = new QWidget();

    setupExportTab(exportTab);
    setupDeletionTab(deletionTab);

    tabs->addTab(exportTab,   tr("Export Requests (Access)"));
    tabs->addTab(deletionTab, tr("Deletion Requests (Erasure)"));

    mainLayout->addWidget(tabs);
}

void DataSubjectWindow::setupExportTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);

    auto* note = new QLabel(
        tr("Subject access requests allow members to receive a copy of their stored personal data. "
           "Exported files are watermarked and contain only authorized, masked field values."), tab);
    note->setWordWrap(true);
    layout->addWidget(note);

    m_exportTable = new QTableWidget(0, 5, tab);
    m_exportTable->setHorizontalHeaderLabels({
        tr("Request ID"), tr("Member ID"), tr("Requester"), tr("Status"), tr("Created")
    });
    m_exportTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_exportTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_exportTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_exportTable->setAlternatingRowColors(true);
    connect(m_exportTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = m_exportTable->currentRow();
        m_fulfillExportBtn->setEnabled(row >= 0);
        m_rejectExportBtn->setEnabled(row >= 0);
    });
    layout->addWidget(m_exportTable);

    auto* btnRow = new QHBoxLayout();
    m_createExportBtn  = new QPushButton(tr("New Export Request…"), tab);
    m_fulfillExportBtn = new QPushButton(tr("Fulfill…"), tab);
    m_rejectExportBtn  = new QPushButton(tr("Reject"), tab);
    auto* refreshBtn   = new QPushButton(tr("Refresh"), tab);

    m_fulfillExportBtn->setEnabled(false);
    m_rejectExportBtn->setEnabled(false);

    connect(m_createExportBtn,  &QPushButton::clicked, this, &DataSubjectWindow::onCreateExportRequest);
    connect(m_fulfillExportBtn, &QPushButton::clicked, this, &DataSubjectWindow::onFulfillExportRequest);
    connect(m_rejectExportBtn,  &QPushButton::clicked, this, &DataSubjectWindow::onRejectExportRequest);
    connect(refreshBtn,         &QPushButton::clicked, this, &DataSubjectWindow::onRefreshExportRequests);

    btnRow->addWidget(m_createExportBtn);
    btnRow->addWidget(m_fulfillExportBtn);
    btnRow->addWidget(m_rejectExportBtn);
    btnRow->addStretch();
    btnRow->addWidget(refreshBtn);
    layout->addLayout(btnRow);
}

void DataSubjectWindow::setupDeletionTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);

    auto* note = new QLabel(
        tr("Erasure requests anonymize all PII fields for a member. "
           "Audit tombstones are retained per the 3-year retention policy. "
           "Approval requires SecurityAdministrator + step-up."), tab);
    note->setWordWrap(true);
    layout->addWidget(note);

    m_deletionTable = new QTableWidget(0, 6, tab);
    m_deletionTable->setHorizontalHeaderLabels({
        tr("Request ID"), tr("Member ID"), tr("Requester"), tr("Approver"), tr("Status"), tr("Created")
    });
    m_deletionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_deletionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deletionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deletionTable->setAlternatingRowColors(true);
    connect(m_deletionTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = m_deletionTable->currentRow();
        m_approveDeletionBtn->setEnabled(row >= 0);
        m_completeDeletionBtn->setEnabled(row >= 0);
        m_rejectDeletionBtn->setEnabled(row >= 0);
    });
    layout->addWidget(m_deletionTable);

    auto* btnRow = new QHBoxLayout();
    m_createDeletionBtn   = new QPushButton(tr("New Deletion Request…"), tab);
    m_approveDeletionBtn  = new QPushButton(tr("Approve…"), tab);
    m_completeDeletionBtn = new QPushButton(tr("Complete Deletion…"), tab);
    m_rejectDeletionBtn   = new QPushButton(tr("Reject"), tab);
    auto* refreshBtn      = new QPushButton(tr("Refresh"), tab);

    m_approveDeletionBtn->setEnabled(false);
    m_completeDeletionBtn->setEnabled(false);
    m_rejectDeletionBtn->setEnabled(false);

    connect(m_createDeletionBtn,   &QPushButton::clicked, this, &DataSubjectWindow::onCreateDeletionRequest);
    connect(m_approveDeletionBtn,  &QPushButton::clicked, this, &DataSubjectWindow::onApproveDeletionRequest);
    connect(m_completeDeletionBtn, &QPushButton::clicked, this, &DataSubjectWindow::onCompleteDeletion);
    connect(m_rejectDeletionBtn,   &QPushButton::clicked, this, &DataSubjectWindow::onRejectDeletionRequest);
    connect(refreshBtn,            &QPushButton::clicked, this, &DataSubjectWindow::onRefreshDeletionRequests);

    btnRow->addWidget(m_createDeletionBtn);
    btnRow->addWidget(m_approveDeletionBtn);
    btnRow->addWidget(m_completeDeletionBtn);
    btnRow->addWidget(m_rejectDeletionBtn);
    btnRow->addStretch();
    btnRow->addWidget(refreshBtn);
    layout->addLayout(btnRow);
}

// ── Export slots ──────────────────────────────────────────────────────────────

void DataSubjectWindow::onCreateExportRequest()
{
    if (!m_ctx.dataSubjectService) return;

    bool ok = false;
    const QString memberId = QInputDialog::getText(
        this, tr("New Export Request"), tr("Member ID:"), QLineEdit::Normal, {}, &ok);
    if (!ok || memberId.trimmed().isEmpty()) return;

    const QString rationale = QInputDialog::getText(
        this, tr("New Export Request"), tr("Rationale:"), QLineEdit::Normal, {}, &ok);
    if (!ok || rationale.trimmed().isEmpty()) return;

    auto res = m_ctx.dataSubjectService->createExportRequest(
        memberId, rationale, m_ctx.session.userId);
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Error"), res.errorMessage());
        return;
    }
    onRefreshExportRequests();
}

void DataSubjectWindow::onFulfillExportRequest()
{
    const int row = m_exportTable->currentRow();
    if (row < 0 || !m_ctx.dataSubjectService) return;

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token,
                     tr("Fulfill data export request"), this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString requestId = m_exportTable->item(row, 0)->text();
    const QString outputPath = QFileDialog::getSaveFileName(
        this, tr("Save Export File"), {}, tr("JSON Files (*.json)"));
    if (outputPath.isEmpty()) return;

    auto res = m_ctx.dataSubjectService->fulfillExportRequest(
        requestId, outputPath, m_ctx.session.userId, dlg.stepUpWindowId());
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Error"), res.errorMessage());
        return;
    }

    QMessageBox::information(this, tr("Export Complete"),
                              tr("Export written to:\n%1").arg(outputPath));
    onRefreshExportRequests();
}

void DataSubjectWindow::onRejectExportRequest()
{
    const int row = m_exportTable->currentRow();
    if (row < 0 || !m_ctx.dataSubjectService || !m_ctx.authService) return;

    if (QMessageBox::question(this, tr("Reject Request"),
            tr("Reject this export request?")) != QMessageBox::Yes) return;

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token,
                     tr("Reject data export request"), this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString requestId = m_exportTable->item(row, 0)->text();
    auto res = m_ctx.dataSubjectService->rejectExportRequest(
        requestId,
        m_ctx.session.userId,
        dlg.stepUpWindowId());
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Error"), res.errorMessage());
        return;
    }
    onRefreshExportRequests();
}

void DataSubjectWindow::onRefreshExportRequests()
{
    m_exportTable->setRowCount(0);
    if (!m_ctx.dataSubjectService) return;

    auto res = m_ctx.dataSubjectService->listExportRequests(m_ctx.session.userId);
    if (!res.isOk()) return;

    for (const ExportRequest& req : res.value()) {
        const int r = m_exportTable->rowCount();
        m_exportTable->insertRow(r);
        m_exportTable->setItem(r, 0, new QTableWidgetItem(req.id));
        m_exportTable->setItem(r, 1, new QTableWidgetItem(req.memberId));
        m_exportTable->setItem(r, 2, new QTableWidgetItem(req.requesterUserId));
        m_exportTable->setItem(r, 3, new QTableWidgetItem(req.status));
        m_exportTable->setItem(r, 4, new QTableWidgetItem(req.createdAt.toString(Qt::ISODate)));
    }
}

// ── Deletion slots ────────────────────────────────────────────────────────────

void DataSubjectWindow::onCreateDeletionRequest()
{
    if (!m_ctx.dataSubjectService) return;

    bool ok = false;
    const QString memberId = QInputDialog::getText(
        this, tr("New Deletion Request"), tr("Member ID:"), QLineEdit::Normal, {}, &ok);
    if (!ok || memberId.trimmed().isEmpty()) return;

    const QString rationale = QInputDialog::getText(
        this, tr("New Deletion Request"), tr("Rationale:"), QLineEdit::Normal, {}, &ok);
    if (!ok || rationale.trimmed().isEmpty()) return;

    auto res = m_ctx.dataSubjectService->createDeletionRequest(
        memberId, rationale, m_ctx.session.userId);
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Error"), res.errorMessage());
        return;
    }
    onRefreshDeletionRequests();
}

void DataSubjectWindow::onApproveDeletionRequest()
{
    const int row = m_deletionTable->currentRow();
    if (row < 0 || !m_ctx.dataSubjectService) return;

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token,
                     tr("Approve deletion request"), this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString requestId = m_deletionTable->item(row, 0)->text();
    auto res = m_ctx.dataSubjectService->approveDeletionRequest(
        requestId, m_ctx.session.userId, dlg.stepUpWindowId());
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Error"), res.errorMessage());
        return;
    }

    QMessageBox::information(this, tr("Approved"), tr("Deletion request approved."));
    onRefreshDeletionRequests();
}

void DataSubjectWindow::onCompleteDeletion()
{
    const int row = m_deletionTable->currentRow();
    if (row < 0 || !m_ctx.dataSubjectService) return;

    if (QMessageBox::warning(this, tr("Complete Deletion"),
            tr("This will permanently anonymize all PII for this member. Continue?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token,
                     tr("Complete deletion request"), this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString requestId = m_deletionTable->item(row, 0)->text();
    auto res = m_ctx.dataSubjectService->completeDeletion(
        requestId, m_ctx.session.userId, dlg.stepUpWindowId());
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Error"), res.errorMessage());
        return;
    }

    QMessageBox::information(this, tr("Deletion Complete"),
                              tr("Member PII has been anonymized. Audit records are retained."));
    onRefreshDeletionRequests();
}

void DataSubjectWindow::onRejectDeletionRequest()
{
    const int row = m_deletionTable->currentRow();
    if (row < 0 || !m_ctx.dataSubjectService || !m_ctx.authService) return;

    if (QMessageBox::question(this, tr("Reject Request"),
            tr("Reject this deletion request?")) != QMessageBox::Yes) return;

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token,
                     tr("Reject deletion request"), this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString requestId = m_deletionTable->item(row, 0)->text();
    auto res = m_ctx.dataSubjectService->rejectDeletionRequest(
        requestId,
        m_ctx.session.userId,
        dlg.stepUpWindowId());
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Error"), res.errorMessage());
        return;
    }
    onRefreshDeletionRequests();
}

void DataSubjectWindow::onRefreshDeletionRequests()
{
    m_deletionTable->setRowCount(0);
    if (!m_ctx.dataSubjectService) return;

    auto res = m_ctx.dataSubjectService->listDeletionRequests(m_ctx.session.userId);
    if (!res.isOk()) return;

    for (const DeletionRequest& req : res.value()) {
        const int r = m_deletionTable->rowCount();
        m_deletionTable->insertRow(r);
        m_deletionTable->setItem(r, 0, new QTableWidgetItem(req.id));
        m_deletionTable->setItem(r, 1, new QTableWidgetItem(req.memberId));
        m_deletionTable->setItem(r, 2, new QTableWidgetItem(req.requesterUserId));
        m_deletionTable->setItem(r, 3, new QTableWidgetItem(req.approverUserId));
        m_deletionTable->setItem(r, 4, new QTableWidgetItem(req.status));
        m_deletionTable->setItem(r, 5, new QTableWidgetItem(req.createdAt.toString(Qt::ISODate)));
    }
}

// ── Context-menu action handlers ─────────────────────────────────────────────

void DataSubjectWindow::maskSelectedPii()
{
    // Determine which tab is active and get the member ID from the selected row.
    const int exportRow   = m_exportTable  ? m_exportTable->currentRow()   : -1;
    const int deletionRow = m_deletionTable ? m_deletionTable->currentRow() : -1;

    QString memberId;
    if (exportRow >= 0 && m_exportTable->item(exportRow, 1))
        memberId = m_exportTable->item(exportRow, 1)->text();
    else if (deletionRow >= 0 && m_deletionTable->item(deletionRow, 1))
        memberId = m_deletionTable->item(deletionRow, 1)->text();

    if (memberId.isEmpty()) {
        QMessageBox::information(this, tr("Mask PII"),
                                 tr("Select a request row containing a member ID first."));
        return;
    }

    // Copy the member ID to the clipboard using PII-safe redaction (last 4 visible).
    ClipboardGuard::copyMasked(memberId);
    QMessageBox::information(this, tr("PII Masked"),
                             tr("Member identifier copied to clipboard in masked form."));
}

void DataSubjectWindow::exportSelectedForRequest()
{
    // Shortcut: create a new export request for the member ID in the currently
    // selected row of either the export or deletion table.
    const int exportRow   = m_exportTable  ? m_exportTable->currentRow()   : -1;
    const int deletionRow = m_deletionTable ? m_deletionTable->currentRow() : -1;

    QString memberId;
    if (exportRow >= 0 && m_exportTable->item(exportRow, 1))
        memberId = m_exportTable->item(exportRow, 1)->text();
    else if (deletionRow >= 0 && m_deletionTable->item(deletionRow, 1))
        memberId = m_deletionTable->item(deletionRow, 1)->text();

    if (memberId.isEmpty()) {
        QMessageBox::information(this, tr("Export for Request"),
                                 tr("Select a request row containing a member ID first."));
        return;
    }

    bool ok = false;
    const QString rationale = QInputDialog::getText(
        this, tr("Export for Request"),
        tr("Rationale for data export of member %1:").arg(memberId),
        QLineEdit::Normal, {}, &ok);
    if (!ok || rationale.trimmed().isEmpty()) return;

    auto res = m_ctx.dataSubjectService->createExportRequest(
        memberId, rationale.trimmed(), m_ctx.session.userId);
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Error"), res.errorMessage());
        return;
    }

    QMessageBox::information(this, tr("Export Request Created"),
                             tr("Export request created successfully for member %1.").arg(memberId));
    onRefreshExportRequests();
}
