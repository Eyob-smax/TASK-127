// UpdateWindow.cpp — ProctorOps
//
// Override: delivery does not require a signed .msi artifact.
// Update/rollback surfaces remain in scope behind package verification and step-up gating. See docs/design.md §2.

#include "UpdateWindow.h"
#include "app/AppContext.h"
#include "dialogs/StepUpDialog.h"
#include "models/Update.h"
#include "services/UpdateService.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>

UpdateWindow::UpdateWindow(AppContext& ctx, QWidget* parent)
    : QWidget(parent)
    , m_ctx(ctx)
{
    setWindowTitle(tr("Update & Rollback"));
    setupUi();

    QTimer::singleShot(0, this, &UpdateWindow::onRefreshPackages);
    QTimer::singleShot(0, this, &UpdateWindow::onRefreshHistory);
    QTimer::singleShot(0, this, &UpdateWindow::onRefreshRollbacks);
}

void UpdateWindow::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    // Signed .msi override note (per docs/design.md §2 and CLAUDE.md)
    m_overrideNote = new QLabel(
        tr("<b>Note:</b> Delivery does not require a signed <tt>.msi</tt> artifact. "
           "Update package import, signature verification, staging, and rollback logic "
           "remain in scope. See design.md §2."),
        this);
    m_overrideNote->setWordWrap(true);
    m_overrideNote->setStyleSheet(QStringLiteral("background: #fffbe6; border: 1px solid #e0cc60; "
                                                   "padding: 4px; border-radius: 3px;"));
    mainLayout->addWidget(m_overrideNote);

    auto* tabs = new QTabWidget(this);
    auto* packagesTab  = new QWidget();
    auto* historyTab   = new QWidget();
    auto* rollbacksTab = new QWidget();

    setupPackagesTab(packagesTab);
    setupHistoryTab(historyTab);
    setupRollbacksTab(rollbacksTab);

    tabs->addTab(packagesTab,  tr("Staged Packages"));
    tabs->addTab(historyTab,   tr("Install History"));
    tabs->addTab(rollbacksTab, tr("Rollback Records"));

    mainLayout->addWidget(tabs);
}

void UpdateWindow::setupPackagesTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);

    auto* note = new QLabel(
        tr("Import a signed <tt>.proctorpkg</tt> package from disk, LAN share, or USB. "
           "The package signature and all component digests are verified before staging."), tab);
    note->setWordWrap(true);
    layout->addWidget(note);

    m_packagesTable = new QTableWidget(0, 6, tab);
    m_packagesTable->setHorizontalHeaderLabels({
        tr("Package ID"), tr("Version"), tr("Platform"), tr("Signature"), tr("Status"), tr("Imported")
    });
    m_packagesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_packagesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_packagesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_packagesTable->setAlternatingRowColors(true);
    connect(m_packagesTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const bool sel = m_packagesTable->currentRow() >= 0;
        m_applyBtn->setEnabled(sel);
        m_cancelBtn->setEnabled(sel);
    });
    layout->addWidget(m_packagesTable);

    auto* btnRow = new QHBoxLayout();
    m_importBtn = new QPushButton(tr("Import Package…"), tab);
    m_applyBtn  = new QPushButton(tr("Apply Package (step-up required)…"), tab);
    m_cancelBtn = new QPushButton(tr("Cancel Staged"), tab);
    auto* refreshBtn = new QPushButton(tr("Refresh"), tab);

    m_applyBtn->setEnabled(false);
    m_cancelBtn->setEnabled(false);

    connect(m_importBtn, &QPushButton::clicked, this, &UpdateWindow::onImportPackage);
    connect(m_applyBtn,  &QPushButton::clicked, this, &UpdateWindow::onApplyPackage);
    connect(m_cancelBtn, &QPushButton::clicked, this, &UpdateWindow::onCancelPackage);
    connect(refreshBtn,  &QPushButton::clicked, this, &UpdateWindow::onRefreshPackages);

    btnRow->addWidget(m_importBtn);
    btnRow->addWidget(m_applyBtn);
    btnRow->addWidget(m_cancelBtn);
    btnRow->addStretch();
    btnRow->addWidget(refreshBtn);
    layout->addLayout(btnRow);
}

void UpdateWindow::setupHistoryTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);

    auto* note = new QLabel(
        tr("Each applied update records a pre-update snapshot. Select a history entry "
           "to roll back to that version. Rollback requires SecurityAdministrator + step-up."), tab);
    note->setWordWrap(true);
    layout->addWidget(note);

    m_historyTable = new QTableWidget(0, 5, tab);
    m_historyTable->setHorizontalHeaderLabels({
        tr("History ID"), tr("From Version"), tr("To Version"), tr("Applied By"), tr("Applied At")
    });
    m_historyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_historyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_historyTable->setAlternatingRowColors(true);
    connect(m_historyTable, &QTableWidget::itemSelectionChanged, this, [this] {
        m_rollbackBtn->setEnabled(m_historyTable->currentRow() >= 0);
    });
    layout->addWidget(m_historyTable);

    auto* btnRow = new QHBoxLayout();
    m_rollbackBtn = new QPushButton(tr("Roll Back to Selected (step-up required)…"), tab);
    auto* refreshBtn = new QPushButton(tr("Refresh"), tab);

    m_rollbackBtn->setEnabled(false);

    connect(m_rollbackBtn, &QPushButton::clicked, this, &UpdateWindow::onRollback);
    connect(refreshBtn,    &QPushButton::clicked, this, &UpdateWindow::onRefreshHistory);

    btnRow->addWidget(m_rollbackBtn);
    btnRow->addStretch();
    btnRow->addWidget(refreshBtn);
    layout->addLayout(btnRow);
}

void UpdateWindow::setupRollbacksTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);

    m_rollbacksTable = new QTableWidget(0, 5, tab);
    m_rollbacksTable->setHorizontalHeaderLabels({
        tr("Rollback ID"), tr("From Version"), tr("To Version"), tr("Rationale"), tr("Rolled Back At")
    });
    m_rollbacksTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_rollbacksTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_rollbacksTable->setAlternatingRowColors(true);
    layout->addWidget(m_rollbacksTable);

    auto* refreshBtn = new QPushButton(tr("Refresh"), tab);
    connect(refreshBtn, &QPushButton::clicked, this, &UpdateWindow::onRefreshRollbacks);
    layout->addWidget(refreshBtn, 0, Qt::AlignRight);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void UpdateWindow::onImportPackage()
{
    if (!m_ctx.updateService) return;

    const QString pkgDir = QFileDialog::getExistingDirectory(
        this, tr("Select .proctorpkg Package Directory"), {});
    if (pkgDir.isEmpty()) return;

    auto res = m_ctx.updateService->importPackage(pkgDir, m_ctx.session.userId);
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Import Failed"),
                               tr("Package import failed:\n%1").arg(res.errorMessage()));
        return;
    }

    const UpdatePackage& pkg = res.value();
    QMessageBox::information(this, tr("Package Staged"),
        tr("Package v%1 staged successfully.\nSignature: %2\n\n"
           "Review the package details and apply when ready.")
        .arg(pkg.version, pkg.signatureValid ? tr("Valid") : tr("Invalid")));

    onRefreshPackages();
}

void UpdateWindow::onApplyPackage()
{
    const int row = m_packagesTable->currentRow();
    if (row < 0 || !m_ctx.updateService) return;

    const QString packageId = m_packagesTable->item(row, 0)->text();
    const QString toVersion = m_packagesTable->item(row, 1)->text();

    if (QMessageBox::warning(this, tr("Apply Update"),
            tr("Apply package %1 (v%2)?\n\n"
               "This action is recorded in the audit log and can be rolled back.").arg(packageId, toVersion),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token,
                     tr("Apply staged update package"), this);
    if (dlg.exec() != QDialog::Accepted) return;

    // Determine current version from latest install history
    QString currentVersion = tr("(unknown)");
    auto histRes = m_ctx.updateService->listInstallHistory(m_ctx.session.userId);
    if (histRes.isOk() && !histRes.value().isEmpty())
        currentVersion = histRes.value().first().toVersion;

    auto res = m_ctx.updateService->applyPackage(packageId, currentVersion,
                                                   m_ctx.session.userId,
                                                   dlg.stepUpWindowId());
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Error"), res.errorMessage());
        return;
    }

    QMessageBox::information(this, tr("Update Applied"),
                              tr("Package applied. Install history entry recorded."));
    onRefreshPackages();
    onRefreshHistory();
}

void UpdateWindow::onCancelPackage()
{
    const int row = m_packagesTable->currentRow();
    if (row < 0 || !m_ctx.updateService) return;

    const QString packageId = m_packagesTable->item(row, 0)->text();

    if (QMessageBox::question(this, tr("Cancel Package"),
            tr("Cancel staged package %1?").arg(packageId)) != QMessageBox::Yes)
        return;

    auto res = m_ctx.updateService->cancelPackage(packageId, m_ctx.session.userId);
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Error"), res.errorMessage());
        return;
    }

    onRefreshPackages();
}

void UpdateWindow::onRollback()
{
    const int row = m_historyTable->currentRow();
    if (row < 0 || !m_ctx.updateService) return;

    const QString historyId = m_historyTable->item(row, 0)->text();
    const QString toVersion = m_historyTable->item(row, 1)->text(); // fromVersion = roll-back target

    bool ok = false;
    const QString rationale = QInputDialog::getText(
        this, tr("Rollback Rationale"),
        tr("Enter rationale for rolling back to v%1:").arg(toVersion),
        QLineEdit::Normal, {}, &ok);
    if (!ok || rationale.trimmed().isEmpty()) return;

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token,
                     tr("Roll back installed update"), this);
    if (dlg.exec() != QDialog::Accepted) return;

    auto res = m_ctx.updateService->rollback(historyId, rationale,
                                              m_ctx.session.userId, dlg.stepUpWindowId());
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Error"), res.errorMessage());
        return;
    }

    QMessageBox::information(this, tr("Rollback Complete"),
                              tr("Rolled back to v%1. Rollback record created.").arg(toVersion));
    onRefreshHistory();
    onRefreshRollbacks();
}

void UpdateWindow::onRefreshPackages()
{
    m_packagesTable->setRowCount(0);
    if (!m_ctx.updateService) return;

    auto res = m_ctx.updateService->listPackages(m_ctx.session.userId);
    if (!res.isOk()) return;

    for (const UpdatePackage& pkg : res.value()) {
        const int r = m_packagesTable->rowCount();
        m_packagesTable->insertRow(r);
        m_packagesTable->setItem(r, 0, new QTableWidgetItem(pkg.id));
        m_packagesTable->setItem(r, 1, new QTableWidgetItem(pkg.version));
        m_packagesTable->setItem(r, 2, new QTableWidgetItem(pkg.targetPlatform));
        m_packagesTable->setItem(r, 3, new QTableWidgetItem(
            pkg.signatureValid ? tr("Valid") : tr("Invalid")));

        QString statusStr;
        switch (pkg.status) {
            case UpdatePackageStatus::Staged:     statusStr = tr("Staged"); break;
            case UpdatePackageStatus::Applied:    statusStr = tr("Applied"); break;
            case UpdatePackageStatus::RolledBack: statusStr = tr("Rolled Back"); break;
            case UpdatePackageStatus::Rejected:   statusStr = tr("Rejected"); break;
            case UpdatePackageStatus::Cancelled:  statusStr = tr("Cancelled"); break;
        }
        m_packagesTable->setItem(r, 4, new QTableWidgetItem(statusStr));
        m_packagesTable->setItem(r, 5, new QTableWidgetItem(
            pkg.importedAt.toString(Qt::ISODate)));
    }
}

void UpdateWindow::onRefreshHistory()
{
    m_historyTable->setRowCount(0);
    if (!m_ctx.updateService) return;

    auto res = m_ctx.updateService->listInstallHistory(m_ctx.session.userId);
    if (!res.isOk()) return;

    for (const InstallHistoryEntry& h : res.value()) {
        const int r = m_historyTable->rowCount();
        m_historyTable->insertRow(r);
        m_historyTable->setItem(r, 0, new QTableWidgetItem(h.id));
        m_historyTable->setItem(r, 1, new QTableWidgetItem(h.fromVersion));
        m_historyTable->setItem(r, 2, new QTableWidgetItem(h.toVersion));
        m_historyTable->setItem(r, 3, new QTableWidgetItem(h.appliedByUserId));
        m_historyTable->setItem(r, 4, new QTableWidgetItem(h.appliedAt.toString(Qt::ISODate)));
    }
}

void UpdateWindow::onRefreshRollbacks()
{
    m_rollbacksTable->setRowCount(0);
    if (!m_ctx.updateService) return;

    auto res = m_ctx.updateService->listRollbackRecords(m_ctx.session.userId);
    if (!res.isOk()) return;

    for (const RollbackRecord& rec : res.value()) {
        const int r = m_rollbacksTable->rowCount();
        m_rollbacksTable->insertRow(r);
        m_rollbacksTable->setItem(r, 0, new QTableWidgetItem(rec.id));
        m_rollbacksTable->setItem(r, 1, new QTableWidgetItem(rec.fromVersion));
        m_rollbacksTable->setItem(r, 2, new QTableWidgetItem(rec.toVersion));
        m_rollbacksTable->setItem(r, 3, new QTableWidgetItem(rec.rationale));
        m_rollbacksTable->setItem(r, 4, new QTableWidgetItem(rec.rolledBackAt.toString(Qt::ISODate)));
    }
}
