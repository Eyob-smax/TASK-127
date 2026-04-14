// SyncWindow.cpp — ProctorOps

#include "SyncWindow.h"
#include "app/AppContext.h"
#include "crypto/KeyStore.h"
#include "crypto/Ed25519Signer.h"
#include "crypto/Ed25519Verifier.h"
#include "dialogs/StepUpDialog.h"
#include "services/SyncService.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QSplitter>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QDateTime>
#include <QTimer>

SyncWindow::SyncWindow(AppContext& ctx, QWidget* parent)
    : QWidget(parent)
    , m_ctx(ctx)
{
    setWindowTitle(tr("Sync Management"));
    setupUi();

    // Initial load
    QTimer::singleShot(0, this, &SyncWindow::onRefreshPackages);
    QTimer::singleShot(0, this, &SyncWindow::onRefreshKeys);
}

void SyncWindow::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    // Header
    auto* header = new QLabel(tr("<b>Desk-to-Desk Sync</b> — "
                                  "Export and import signed .proctorsync packages via LAN share or USB."),
                               this);
    header->setWordWrap(true);
    mainLayout->addWidget(header);

    auto* tabs = new QTabWidget(this);

    auto* packagesTab  = new QWidget();
    auto* conflictsTab = new QWidget();
    auto* keysTab      = new QWidget();

    setupPackagesTab(packagesTab);
    setupConflictsTab(conflictsTab);
    setupKeysTab(keysTab);

    tabs->addTab(packagesTab,  tr("Packages"));
    tabs->addTab(conflictsTab, tr("Conflicts"));
    tabs->addTab(keysTab,      tr("Signing Keys"));

    mainLayout->addWidget(tabs);
}

void SyncWindow::setupPackagesTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);

    m_packagesTable = new QTableWidget(0, 5, tab);
    m_packagesTable->setHorizontalHeaderLabels({
        tr("Package ID"), tr("Source Desk"), tr("Exported At"), tr("Status"), tr("File Path")
    });
    m_packagesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_packagesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_packagesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_packagesTable->setAlternatingRowColors(true);
    connect(m_packagesTable, &QTableWidget::itemSelectionChanged,
            this, &SyncWindow::onPackageSelected);

    layout->addWidget(m_packagesTable);

    auto* btnRow = new QHBoxLayout();
    m_exportBtn = new QPushButton(tr("Export Package…"), tab);
    m_importBtn = new QPushButton(tr("Import Package…"), tab);
    auto* refreshBtn = new QPushButton(tr("Refresh"), tab);

    connect(m_exportBtn,  &QPushButton::clicked, this, &SyncWindow::onExportPackage);
    connect(m_importBtn,  &QPushButton::clicked, this, &SyncWindow::onImportPackage);
    connect(refreshBtn,   &QPushButton::clicked, this, &SyncWindow::onRefreshPackages);

    btnRow->addWidget(m_exportBtn);
    btnRow->addWidget(m_importBtn);
    btnRow->addStretch();
    btnRow->addWidget(refreshBtn);
    layout->addLayout(btnRow);
}

void SyncWindow::setupConflictsTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);

    m_conflictsHint = new QLabel(
        tr("Select a package in the Packages tab to view its conflicts."), tab);
    layout->addWidget(m_conflictsHint);

    m_conflictsTable = new QTableWidget(0, 5, tab);
    m_conflictsTable->setHorizontalHeaderLabels({
        tr("Conflict ID"), tr("Type"), tr("Entity"), tr("Description"), tr("Status")
    });
    m_conflictsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_conflictsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_conflictsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_conflictsTable->setAlternatingRowColors(true);
    layout->addWidget(m_conflictsTable);

    auto* btnRow = new QHBoxLayout();
    m_resolveBtn = new QPushButton(tr("Resolve Conflict…"), tab);
    m_resolveBtn->setEnabled(false);
    connect(m_resolveBtn, &QPushButton::clicked, this, &SyncWindow::onResolveConflict);

    btnRow->addWidget(m_resolveBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);
}

void SyncWindow::setupKeysTab(QWidget* tab)
{
    auto* layout = new QVBoxLayout(tab);

    auto* note = new QLabel(
        tr("Trusted signing keys authorize sync and update packages from other desks. "
           "Importing and revoking keys requires SecurityAdministrator + step-up."), tab);
    note->setWordWrap(true);
    layout->addWidget(note);

    m_keysTable = new QTableWidget(0, 5, tab);
    m_keysTable->setHorizontalHeaderLabels({
        tr("Label"), tr("Fingerprint"), tr("Imported At"), tr("Expires"), tr("Status")
    });
    m_keysTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_keysTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_keysTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_keysTable->setAlternatingRowColors(true);
    layout->addWidget(m_keysTable);

    auto* btnRow = new QHBoxLayout();
    m_importKeyBtn = new QPushButton(tr("Import Key…"), tab);
    m_revokeKeyBtn = new QPushButton(tr("Revoke Key"), tab);
    m_revokeKeyBtn->setEnabled(false);
    auto* refreshKeysBtn = new QPushButton(tr("Refresh"), tab);

    connect(m_importKeyBtn,  &QPushButton::clicked, this, &SyncWindow::onImportSigningKey);
    connect(m_revokeKeyBtn,  &QPushButton::clicked, this, &SyncWindow::onRevokeSigningKey);
    connect(refreshKeysBtn,  &QPushButton::clicked, this, &SyncWindow::onRefreshKeys);
    connect(m_keysTable, &QTableWidget::itemSelectionChanged, this, [this] {
        m_revokeKeyBtn->setEnabled(m_keysTable->currentRow() >= 0);
    });

    btnRow->addWidget(m_importKeyBtn);
    btnRow->addWidget(m_revokeKeyBtn);
    btnRow->addStretch();
    btnRow->addWidget(refreshKeysBtn);
    layout->addLayout(btnRow);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void SyncWindow::onExportPackage()
{
    if (!m_ctx.syncService || !m_ctx.keyStore || !m_ctx.authService) {
        QMessageBox::warning(this, tr("Unavailable"), tr("Sync service is not initialized."));
        return;
    }

    auto keysResult = m_ctx.syncService->listSigningKeys(m_ctx.session.userId);
    if (!keysResult.isOk()) {
        QMessageBox::critical(this, tr("Authorization Failed"), keysResult.errorMessage());
        return;
    }

    QList<TrustedSigningKey> activeKeys;
    for (const TrustedSigningKey& key : keysResult.value()) {
        if (key.revoked)
            continue;
        if (key.expiresAt.isValid() && key.expiresAt < QDateTime::currentDateTimeUtc())
            continue;
        activeKeys.append(key);
    }

    if (activeKeys.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("No Active Signing Key"),
            tr("No active trusted signing key is available. Import a signing key first."));
        return;
    }

    QStringList keyLabels;
    for (const TrustedSigningKey& key : activeKeys) {
        keyLabels.append(QStringLiteral("%1 (%2)").arg(key.label, key.fingerprint.left(12)));
    }

    bool keyOk = false;
    const QString selectedLabel = QInputDialog::getItem(
        this,
        tr("Select Signing Key"),
        tr("Signing key:"),
        keyLabels,
        0,
        false,
        &keyOk);
    if (!keyOk || selectedLabel.isEmpty())
        return;

    int selectedIndex = keyLabels.indexOf(selectedLabel);
    if (selectedIndex < 0)
        selectedIndex = 0;
    const TrustedSigningKey& selectedKey = activeKeys.at(selectedIndex);

    bool deskOk = false;
    const QString deskId = QInputDialog::getText(
        this,
        tr("Desk Identifier"),
        tr("Source desk ID:"),
        QLineEdit::Normal,
        {},
        &deskOk);
    if (!deskOk || deskId.trimmed().isEmpty())
        return;

    const QString destDir = QFileDialog::getExistingDirectory(
        this, tr("Select Export Destination (LAN share or USB)"), {});
    if (destDir.isEmpty()) return;

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token,
                     tr("Export signed sync package"), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString privateKeyName =
        QStringLiteral("sync.signing.private_key.%1").arg(selectedKey.id);
    auto privateKeyResult = m_ctx.keyStore->getSecret(privateKeyName);
    if (!privateKeyResult.isOk()) {
        QMessageBox::critical(
            this,
            tr("Private Key Missing"),
            tr("No private key is stored for '%1'. Re-import this key with a matching private key.")
                .arg(selectedKey.label));
        return;
    }

    auto exportResult = m_ctx.syncService->exportPackage(
        destDir,
        deskId.trimmed(),
        selectedKey.id,
        privateKeyResult.value(),
        m_ctx.session.userId,
        dlg.stepUpWindowId());
    if (!exportResult.isOk()) {
        QMessageBox::critical(this, tr("Export Failed"), exportResult.errorMessage());
        return;
    }

    QMessageBox::information(
        this,
        tr("Export Complete"),
        tr("Package %1 exported to:\n%2")
            .arg(exportResult.value().id, exportResult.value().packageFilePath));

    onRefreshPackages();
}

void SyncWindow::onImportPackage()
{
    if (!m_ctx.syncService) return;

    const QString pkgDir = QFileDialog::getExistingDirectory(
        this, tr("Select Sync Package Folder"), {});
    if (pkgDir.isEmpty()) return;

    auto res = m_ctx.syncService->importPackage(pkgDir, m_ctx.session.userId);
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Import Failed"),
                               tr("Could not import package:\n%1").arg(res.errorMessage()));
        return;
    }

    const QString status = (res.value().status == SyncPackageStatus::Partial)
        ? tr("Package imported with conflicts requiring review.")
        : tr("Package applied successfully.");
    QMessageBox::information(this, tr("Import Complete"), status);

    onRefreshPackages();
    if (res.value().status == SyncPackageStatus::Partial)
        onRefreshConflicts();
}

void SyncWindow::onResolveConflict()
{
    const int row = m_conflictsTable->currentRow();
    if (row < 0) return;

    const QString conflictId = m_conflictsTable->item(row, 0)->text();

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token,
                     tr("Resolve sync conflict"), this);
    if (dlg.exec() != QDialog::Accepted) return;

    bool ok = false;
    const QString resolutionStr = QInputDialog::getItem(
        this, tr("Resolve Conflict"),
        tr("Choose resolution for conflict %1:").arg(conflictId),
        {tr("Accept Local"), tr("Accept Incoming"), tr("Skip (local wins)")},
        0, false, &ok);
    if (!ok) return;

    ConflictStatus resolution = ConflictStatus::ResolvedAcceptLocal;
    if (resolutionStr.contains(tr("Incoming"))) resolution = ConflictStatus::ResolvedAcceptIncoming;
    else if (resolutionStr.contains(tr("Skip")))    resolution = ConflictStatus::Skipped;

    auto res = m_ctx.syncService->resolveConflict(conflictId, resolution,
                                                    m_ctx.session.userId,
                                                    dlg.stepUpWindowId());
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Error"), res.errorMessage());
        return;
    }

    onRefreshConflicts();
}

void SyncWindow::onImportSigningKey()
{
    if (!m_ctx.syncService) return;

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token,
                     tr("Import signing key"), this);
    if (dlg.exec() != QDialog::Accepted) return;

    bool ok = false;
    const QString label = QInputDialog::getText(
        this, tr("Import Signing Key"), tr("Key label:"), QLineEdit::Normal, {}, &ok);
    if (!ok || label.trimmed().isEmpty()) return;

    const QString pubKeyHex = QInputDialog::getText(
        this, tr("Import Signing Key"), tr("Public key (DER hex):"), QLineEdit::Normal, {}, &ok);
    if (!ok || pubKeyHex.trimmed().isEmpty()) return;

    const QString privateKeyHex = QInputDialog::getText(
        this,
        tr("Import Signing Key"),
        tr("Private key (DER hex, optional - required for package export):"),
        QLineEdit::Normal,
        {},
        &ok);
    if (!ok) return;

    QByteArray privateKeyDer;
    if (!privateKeyHex.trimmed().isEmpty()) {
        privateKeyDer = QByteArray::fromHex(privateKeyHex.trimmed().toLatin1());
        if (privateKeyDer.isEmpty()) {
            QMessageBox::critical(this, tr("Invalid Key"), tr("Private key must be valid hex."));
            return;
        }

        const QByteArray publicKeyDer = QByteArray::fromHex(pubKeyHex.trimmed().toLatin1());
        if (publicKeyDer.isEmpty()) {
            QMessageBox::critical(this, tr("Invalid Key"), tr("Public key must be valid hex."));
            return;
        }

        const QByteArray validationMessage("proctorops-sync-key-validation");
        auto signResult = Ed25519Signer::sign(validationMessage, privateKeyDer);
        if (!signResult.isOk()) {
            QMessageBox::critical(this, tr("Invalid Key"), signResult.errorMessage());
            return;
        }

        auto verifyResult = Ed25519Verifier::verify(validationMessage, signResult.value(), publicKeyDer);
        if (!verifyResult.isOk() || !verifyResult.value()) {
            QMessageBox::critical(this, tr("Key Mismatch"),
                                  tr("Private/public key pair verification failed."));
            return;
        }
    }

    auto res = m_ctx.syncService->importSigningKey(label, pubKeyHex,
                                                     QDateTime(), // no expiry
                                                     m_ctx.session.userId,
                                                     dlg.stepUpWindowId());
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Error"), res.errorMessage());
        return;
    }

    if (!privateKeyDer.isEmpty()) {
        const QString privateKeyName =
            QStringLiteral("sync.signing.private_key.%1").arg(res.value().id);
        auto storeResult = m_ctx.keyStore->storeSecret(privateKeyName, privateKeyDer);
        if (!storeResult.isOk()) {
            QMessageBox::warning(this,
                                 tr("Private Key Not Stored"),
                                 tr("Key trust import succeeded, but storing private key failed: %1")
                                     .arg(storeResult.errorMessage()));
        }
    }

    QMessageBox::information(this, tr("Key Imported"),
                              tr("Key '%1' imported.\nFingerprint: %2")
                              .arg(label, res.value().fingerprint));
    onRefreshKeys();
}

void SyncWindow::onRevokeSigningKey()
{
    const int row = m_keysTable->currentRow();
    if (row < 0) return;

    StepUpDialog dlg(*m_ctx.authService, m_ctx.session.token,
                     tr("Revoke signing key"), this);
    if (dlg.exec() != QDialog::Accepted) return;

    // Key ID is stored in UserData of the first column item
    const QString keyId = m_keysTable->item(row, 0)->data(Qt::UserRole).toString();

    auto res = m_ctx.syncService->revokeSigningKey(keyId, m_ctx.session.userId,
                                                     dlg.stepUpWindowId());
    if (!res.isOk()) {
        QMessageBox::critical(this, tr("Error"), res.errorMessage());
        return;
    }

    onRefreshKeys();
}

void SyncWindow::onPackageSelected()
{
    const int row = m_packagesTable->currentRow();
    if (row < 0) {
        m_selectedPackageId.clear();
        return;
    }
    m_selectedPackageId = m_packagesTable->item(row, 0)->text();
    onRefreshConflicts();
}

void SyncWindow::onRefreshPackages()
{
    m_packagesTable->setRowCount(0);
    if (!m_ctx.syncService) return;

    auto res = m_ctx.syncService->listPackages(m_ctx.session.userId);
    if (!res.isOk()) return;

    for (const SyncPackage& pkg : res.value()) {
        const int r = m_packagesTable->rowCount();
        m_packagesTable->insertRow(r);
        m_packagesTable->setItem(r, 0, new QTableWidgetItem(pkg.id));
        m_packagesTable->setItem(r, 1, new QTableWidgetItem(pkg.sourceDeskId));
        m_packagesTable->setItem(r, 2, new QTableWidgetItem(pkg.exportedAt.toString(Qt::ISODate)));

        QString status;
        switch (pkg.status) {
            case SyncPackageStatus::Pending:  status = tr("Pending"); break;
            case SyncPackageStatus::Verified: status = tr("Verified"); break;
            case SyncPackageStatus::Applied:  status = tr("Applied"); break;
            case SyncPackageStatus::Partial:  status = tr("Partial — conflicts"); break;
            case SyncPackageStatus::Rejected: status = tr("Rejected"); break;
        }
        m_packagesTable->setItem(r, 3, new QTableWidgetItem(status));
        m_packagesTable->setItem(r, 4, new QTableWidgetItem(pkg.packageFilePath));
    }
}

void SyncWindow::onRefreshConflicts()
{
    m_conflictsTable->setRowCount(0);
    if (m_selectedPackageId.isEmpty() || !m_ctx.syncService) return;

    auto res = m_ctx.syncService->listPendingConflicts(m_selectedPackageId, m_ctx.session.userId);
    if (!res.isOk()) return;

    m_resolveBtn->setEnabled(!res.value().isEmpty());

    for (const ConflictRecord& cr : res.value()) {
        const int r = m_conflictsTable->rowCount();
        m_conflictsTable->insertRow(r);

        QString typeStr;
        switch (cr.type) {
            case ConflictType::DoubleDeduction:        typeStr = tr("Double Deduction"); break;
            case ConflictType::MutableRecordConflict:  typeStr = tr("Record Conflict"); break;
            case ConflictType::DeleteConflict:         typeStr = tr("Delete Conflict"); break;
        }
        m_conflictsTable->setItem(r, 0, new QTableWidgetItem(cr.id));
        m_conflictsTable->setItem(r, 1, new QTableWidgetItem(typeStr));
        m_conflictsTable->setItem(r, 2, new QTableWidgetItem(cr.entityType));
        m_conflictsTable->setItem(r, 3, new QTableWidgetItem(cr.description));
        m_conflictsTable->setItem(r, 4, new QTableWidgetItem(tr("Pending")));
    }
}

void SyncWindow::onRefreshKeys()
{
    m_keysTable->setRowCount(0);
    if (!m_ctx.syncService) return;

    auto res = m_ctx.syncService->listSigningKeys(m_ctx.session.userId);
    if (!res.isOk()) return;

    for (const TrustedSigningKey& key : res.value()) {
        const int r = m_keysTable->rowCount();
        m_keysTable->insertRow(r);

        auto* labelItem = new QTableWidgetItem(key.label);
        labelItem->setData(Qt::UserRole, key.id);
        m_keysTable->setItem(r, 0, labelItem);

        m_keysTable->setItem(r, 1, new QTableWidgetItem(key.fingerprint.left(16) + QStringLiteral("…")));
        m_keysTable->setItem(r, 2, new QTableWidgetItem(key.importedAt.toString(Qt::ISODate)));
        m_keysTable->setItem(r, 3, new QTableWidgetItem(
            key.expiresAt.isNull() ? tr("No expiry") : key.expiresAt.toString(Qt::ISODate)));
        m_keysTable->setItem(r, 4, new QTableWidgetItem(key.revoked ? tr("Revoked") : tr("Active")));
    }
}
