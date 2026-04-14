// AuditViewerWindow.cpp — ProctorOps

#include "windows/AuditViewerWindow.h"
#include "app/AppContext.h"
#include "services/AuditService.h"
#include "models/CommonTypes.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QTableView>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QSplitter>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

static constexpr int kPageSize = 100;

AuditViewerWindow::AuditViewerWindow(AppContext& ctx, QWidget* parent)
    : QWidget(parent), m_ctx(ctx)
{
    setObjectName(QLatin1String(WindowId));
    setWindowTitle(QStringLiteral("Audit Log Viewer"));
    setupUi();
    QTimer::singleShot(0, this, [this] { loadEntries(); });
}

void AuditViewerWindow::setupUi()
{
    auto* root = new QVBoxLayout(this);

    // ── Filter panel ──────────────────────────────────────────────────────────
    auto* filterBox    = new QGroupBox(QStringLiteral("Filter"), this);
    auto* filterLayout = new QHBoxLayout(filterBox);

    auto* form1 = new QFormLayout;
    m_fromEdit = new QDateTimeEdit(QDateTime::currentDateTimeUtc().addDays(-7), filterBox);
    m_fromEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    m_fromEdit->setCalendarPopup(true);
    form1->addRow(QStringLiteral("From:"), m_fromEdit);

    m_toEdit = new QDateTimeEdit(QDateTime::currentDateTimeUtc(), filterBox);
    m_toEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    m_toEdit->setCalendarPopup(true);
    form1->addRow(QStringLiteral("To:"), m_toEdit);
    filterLayout->addLayout(form1);

    auto* form2 = new QFormLayout;
    m_actorEdit = new QLineEdit(filterBox);
    m_actorEdit->setPlaceholderText(QStringLiteral("Actor user ID"));
    form2->addRow(QStringLiteral("Actor:"), m_actorEdit);

    m_eventTypeCombo = new QComboBox(filterBox);
    m_eventTypeCombo->addItem(QStringLiteral("(all event types)"), -1);
    // Item data stores the AuditEventType int value for direct mapping in buildFilter().
    auto addEventItem = [this](const QString& label, AuditEventType t) {
        m_eventTypeCombo->addItem(label, static_cast<int>(t));
    };
    addEventItem(QStringLiteral("Login"),               AuditEventType::Login);
    addEventItem(QStringLiteral("LoginFailed"),          AuditEventType::LoginFailed);
    addEventItem(QStringLiteral("LoginLocked"),          AuditEventType::LoginLocked);
    addEventItem(QStringLiteral("CheckInSuccess"),       AuditEventType::CheckInSuccess);
    addEventItem(QStringLiteral("CheckInAttempted"),     AuditEventType::CheckInAttempted);
    addEventItem(QStringLiteral("DeductionCreated"),     AuditEventType::DeductionCreated);
    addEventItem(QStringLiteral("DeductionReversed"),    AuditEventType::DeductionReversed);
    addEventItem(QStringLiteral("CorrectionRequested"),  AuditEventType::CorrectionRequested);
    addEventItem(QStringLiteral("CorrectionApproved"),   AuditEventType::CorrectionApproved);
    addEventItem(QStringLiteral("CorrectionApplied"),    AuditEventType::CorrectionApplied);
    addEventItem(QStringLiteral("CorrectionRejected"),   AuditEventType::CorrectionRejected);
    addEventItem(QStringLiteral("QuestionCreated"),      AuditEventType::QuestionCreated);
    addEventItem(QStringLiteral("QuestionUpdated"),      AuditEventType::QuestionUpdated);
    addEventItem(QStringLiteral("QuestionDeleted"),      AuditEventType::QuestionDeleted);
    addEventItem(QStringLiteral("JobCreated"),           AuditEventType::JobCreated);
    addEventItem(QStringLiteral("JobCompleted"),         AuditEventType::JobCompleted);
    addEventItem(QStringLiteral("JobFailed"),            AuditEventType::JobFailed);
    addEventItem(QStringLiteral("KeyImported"),          AuditEventType::KeyImported);
    addEventItem(QStringLiteral("KeyRevoked"),           AuditEventType::KeyRevoked);
    addEventItem(QStringLiteral("ChainVerified"),        AuditEventType::ChainVerified);
    addEventItem(QStringLiteral("AuditExport"),          AuditEventType::AuditExport);
    form2->addRow(QStringLiteral("Event type:"), m_eventTypeCombo);
    filterLayout->addLayout(form2);

    auto* form3 = new QFormLayout;
    m_entityTypeEdit = new QLineEdit(filterBox);
    m_entityTypeEdit->setPlaceholderText(QStringLiteral("e.g. Question"));
    form3->addRow(QStringLiteral("Entity type:"), m_entityTypeEdit);
    m_entityIdEdit = new QLineEdit(filterBox);
    m_entityIdEdit->setPlaceholderText(QStringLiteral("UUID"));
    form3->addRow(QStringLiteral("Entity ID:"), m_entityIdEdit);
    filterLayout->addLayout(form3);

    auto* filterButtons = new QVBoxLayout;
    auto* applyBtn  = new QPushButton(QStringLiteral("Apply"), filterBox);
    auto* exportBtn = new QPushButton(QStringLiteral("Export…"), filterBox);
    auto* verifyBtn = new QPushButton(QStringLiteral("Verify Chain"), filterBox);
    filterButtons->addWidget(applyBtn);
    filterButtons->addWidget(exportBtn);
    filterButtons->addWidget(verifyBtn);
    filterButtons->addStretch();
    filterLayout->addLayout(filterButtons);

    root->addWidget(filterBox);

    connect(applyBtn,  &QPushButton::clicked, this, &AuditViewerWindow::onApplyFilter);
    connect(exportBtn, &QPushButton::clicked, this, &AuditViewerWindow::onExport);
    connect(verifyBtn, &QPushButton::clicked, this, &AuditViewerWindow::onVerifyChain);

    // ── Splitter: table + detail ───────────────────────────────────────────────
    auto* splitter = new QSplitter(Qt::Vertical, this);

    // Table
    m_model = new QStandardItemModel(0, 5, this);
    m_model->setHorizontalHeaderLabels({
        QStringLiteral("Timestamp"),
        QStringLiteral("Actor"),
        QStringLiteral("Event Type"),
        QStringLiteral("Entity Type"),
        QStringLiteral("Entity ID"),
    });

    m_tableView = new QTableView(this);
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tableView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    splitter->addWidget(m_tableView);

    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &AuditViewerWindow::onSelectionChanged);

    // Detail pane
    m_detailPane = new QTextEdit(this);
    m_detailPane->setReadOnly(true);
    m_detailPane->setFontFamily(QStringLiteral("Courier New"));
    m_detailPane->setPlaceholderText(QStringLiteral("Select an entry to view details…"));
    splitter->addWidget(m_detailPane);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    root->addWidget(splitter, 1);

    // Footer
    auto* footer = new QHBoxLayout;
    m_statusLabel = new QLabel(QStringLiteral("0 entries"), this);
    m_loadMoreBtn = new QPushButton(QStringLiteral("Load More"), this);
    m_loadMoreBtn->setVisible(false);
    footer->addWidget(m_statusLabel);
    footer->addStretch();
    footer->addWidget(m_loadMoreBtn);
    root->addLayout(footer);

    connect(m_loadMoreBtn, &QPushButton::clicked, this, &AuditViewerWindow::onLoadMore);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void AuditViewerWindow::onApplyFilter()
{
    m_currentOffset = 0;
    loadEntries();
}

void AuditViewerWindow::onSelectionChanged()
{
    const auto selection = m_tableView->selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        m_detailPane->clear();
        return;
    }
    const int row = selection.first().row();
    if (row < 0 || row >= static_cast<int>(m_entries.size())) return;
    showEntryDetail(m_entries[row]);
}

void AuditViewerWindow::onExport()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export Audit Log"),
        QStringLiteral("audit_export.jsonl"),
        QStringLiteral("JSON Lines (*.jsonl);;All Files (*)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this,
            QStringLiteral("Export Failed"),
            QStringLiteral("Could not open file for writing: ") + path);
        return;
    }

    QTextStream out(&file);
    for (const auto& entry : m_entries) {
        QJsonObject obj;
        obj[QStringLiteral("id")]               = entry.id;
        obj[QStringLiteral("timestamp")]         = entry.timestamp.toString(Qt::ISODate);
        obj[QStringLiteral("actor")]             = entry.actorUserId;
        obj[QStringLiteral("event_type")]        = auditEventTypeToString(entry.eventType);
        obj[QStringLiteral("entity_type")]       = entry.entityType;
        obj[QStringLiteral("entity_id")]         = entry.entityId;
        obj[QStringLiteral("before_payload")]    = entry.beforePayloadJson;
        obj[QStringLiteral("after_payload")]     = entry.afterPayloadJson;
        obj[QStringLiteral("previous_hash")]     = entry.previousEntryHash;
        obj[QStringLiteral("entry_hash")]        = entry.entryHash;
        out << QJsonDocument(obj).toJson(QJsonDocument::Compact) << '\n';
    }

    QMessageBox::information(this,
        QStringLiteral("Export Complete"),
        QStringLiteral("Exported %1 entries to:\n%2").arg(m_entries.size()).arg(path));

    // Record export audit event
    auto exportAudit = m_ctx.auditService->recordEvent(
        m_ctx.session.userId,
        AuditEventType::AuditExport,
        QStringLiteral("AuditLog"),
        path);
    if (!exportAudit.isOk()) {
        QMessageBox::warning(this,
            QStringLiteral("Audit Warning"),
            QStringLiteral("Export completed, but audit recording failed: ")
                + exportAudit.errorMessage());
    }
}

void AuditViewerWindow::onVerifyChain()
{
    auto result = m_ctx.auditService->verifyChain(m_ctx.session.userId);
    if (!result.isOk()) {
        QMessageBox::critical(this,
            QStringLiteral("Chain Verification Failed"),
            QStringLiteral("Could not complete verification: ") + result.errorMessage());
        return;
    }

    const auto& report = result.value();
    if (report.integrityOk) {
        QMessageBox::information(this,
            QStringLiteral("Chain Integrity OK"),
            QStringLiteral("Verified %1 entries.\nFirst: %2\nLast: %3\n\nThe audit chain is intact.")
                .arg(report.entriesVerified)
                .arg(report.firstEntryId)
                .arg(report.lastEntryId));
    } else {
        QMessageBox::critical(this,
            QStringLiteral("Chain Integrity Broken"),
            QStringLiteral("Verified %1 entries before finding a break.\n"
                           "First broken entry: %2\n\n"
                           "The audit chain has been tampered with or corrupted.")
                .arg(report.entriesVerified)
                .arg(report.firstBrokenEntryId));
    }

    auto verifyAudit = m_ctx.auditService->recordEvent(
        m_ctx.session.userId,
        AuditEventType::ChainVerified,
        QStringLiteral("AuditLog"),
        QString::number(report.entriesVerified));
    if (!verifyAudit.isOk()) {
        QMessageBox::warning(this,
            QStringLiteral("Audit Warning"),
            QStringLiteral("Chain verification finished, but audit recording failed: ")
                + verifyAudit.errorMessage());
    }
}

void AuditViewerWindow::onLoadMore()
{
    loadEntries(true);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void AuditViewerWindow::buildFilter(AuditFilter& f) const
{
    f.fromTimestamp = m_fromEdit->dateTime().toUTC();
    f.toTimestamp   = m_toEdit->dateTime().toUTC();
    f.actorUserId   = m_actorEdit->text().trimmed();
    f.entityType    = m_entityTypeEdit->text().trimmed();
    f.entityId      = m_entityIdEdit->text().trimmed();
    f.limit  = kPageSize;
    f.offset = m_currentOffset;
    // Item data stores the AuditEventType int value; index 0 is "(all)" with data -1.
    if (m_eventTypeCombo->currentIndex() > 0)
        f.eventType = static_cast<AuditEventType>(m_eventTypeCombo->currentData().toInt());
}

void AuditViewerWindow::loadEntries(bool append)
{
    AuditFilter filter;
    buildFilter(filter);

    auto result = m_ctx.auditService->queryEvents(m_ctx.session.userId, filter);
    if (!result.isOk()) {
        m_statusLabel->setText(QStringLiteral("Error: ") + result.errorMessage());
        return;
    }

    populateTable(result.value(), append);
    m_statusLabel->setText(QStringLiteral("%1 entries").arg(m_entries.size()));
    m_loadMoreBtn->setVisible(static_cast<int>(result.value().size()) == kPageSize);
    if (append)
        m_currentOffset += result.value().size();
    else
        m_currentOffset = result.value().size();
}

void AuditViewerWindow::populateTable(const QList<AuditEntry>& entries, bool append)
{
    if (!append) {
        m_model->removeRows(0, m_model->rowCount());
        m_entries.clear();
    }

    for (const auto& entry : entries) {
        m_entries.append(entry);
        QList<QStandardItem*> row;
        row << new QStandardItem(entry.timestamp.toLocalTime().toString(Qt::ISODate));
        row << new QStandardItem(entry.actorUserId);
        row << new QStandardItem(auditEventTypeToString(entry.eventType));
        row << new QStandardItem(entry.entityType);
        row << new QStandardItem(entry.entityId.left(8) + QStringLiteral("…"));
        for (auto* item : row) item->setEditable(false);
        m_model->appendRow(row);
    }
}

void AuditViewerWindow::showEntryDetail(const AuditEntry& entry)
{
    const QString text = QStringLiteral(
        "ID:             %1\n"
        "Timestamp:      %2\n"
        "Actor:          %3\n"
        "Event:          %4\n"
        "Entity:         %5 / %6\n"
        "\nEntry hash:\n%7\n"
        "\nPrevious hash:\n%8\n"
        "\nBefore payload:\n%9\n"
        "\nAfter payload:\n%10")
        .arg(entry.id)
        .arg(entry.timestamp.toString(Qt::ISODate))
        .arg(entry.actorUserId)
        .arg(auditEventTypeToString(entry.eventType))
        .arg(entry.entityType)
        .arg(entry.entityId)
        .arg(entry.entryHash)
        .arg(entry.previousEntryHash)
        .arg(entry.beforePayloadJson.isEmpty()
             ? QStringLiteral("(none)") : entry.beforePayloadJson)
        .arg(entry.afterPayloadJson.isEmpty()
             ? QStringLiteral("(none)") : entry.afterPayloadJson);

    m_detailPane->setPlainText(text);
}
