// IngestionMonitorWindow.cpp — ProctorOps

#include "IngestionMonitorWindow.h"
#include "app/AppContext.h"
#include "models/Ingestion.h"
#include "repositories/IngestionRepository.h"
#include "services/IngestionService.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QMessageBox>

IngestionMonitorWindow::IngestionMonitorWindow(AppContext& ctx, QWidget* parent)
    : QWidget(parent)
    , m_ctx(ctx)
{
    setWindowTitle(tr("Ingestion Monitor"));
    setupUi();

    // Auto-refresh every 5 seconds while window is open
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(5000);
    connect(m_refreshTimer, &QTimer::timeout, this, &IngestionMonitorWindow::onRefresh);
    m_refreshTimer->start();

    QTimer::singleShot(0, this, &IngestionMonitorWindow::onRefresh);
}

IngestionMonitorWindow::~IngestionMonitorWindow()
{
    if (m_refreshTimer)
        m_refreshTimer->stop();
}

void IngestionMonitorWindow::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    auto* header = new QLabel(
        tr("<b>Ingestion Monitor</b> — Live view of ingestion scheduler jobs. "
           "Auto-refreshes every 5 seconds."), this);
    header->setWordWrap(true);
    mainLayout->addWidget(header);

    m_jobsTable = new QTableWidget(0, 7, this);
    m_jobsTable->setHorizontalHeaderLabels({
        tr("Job ID"), tr("Type"), tr("Status"), tr("Phase"), tr("Priority"),
        tr("Retries"), tr("Created At")
    });
    m_jobsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_jobsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_jobsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_jobsTable->setAlternatingRowColors(true);
    connect(m_jobsTable, &QTableWidget::itemSelectionChanged, this, [this] {
        m_cancelBtn->setEnabled(m_jobsTable->currentRow() >= 0);
    });
    mainLayout->addWidget(m_jobsTable);

    auto* btnRow = new QHBoxLayout();
    m_statusLabel = new QLabel(tr("Ready"), this);
    m_cancelBtn   = new QPushButton(tr("Cancel Selected Job"), this);
    auto* refreshBtn = new QPushButton(tr("Refresh Now"), this);

    m_cancelBtn->setEnabled(false);

    connect(m_cancelBtn, &QPushButton::clicked, this, &IngestionMonitorWindow::onCancelJob);
    connect(refreshBtn,  &QPushButton::clicked, this, &IngestionMonitorWindow::onRefresh);

    btnRow->addWidget(m_statusLabel);
    btnRow->addStretch();
    btnRow->addWidget(m_cancelBtn);
    btnRow->addWidget(refreshBtn);
    mainLayout->addLayout(btnRow);
}

void IngestionMonitorWindow::onRefresh()
{
    m_jobsTable->setRowCount(0);
    if (!m_ctx.ingestionRepo) {
        m_statusLabel->setText(tr("Ingestion service not available."));
        return;
    }

    // List all job statuses
    const QList<JobStatus> allStatuses = {
        JobStatus::Pending, JobStatus::Claimed, JobStatus::Validating,
        JobStatus::Importing, JobStatus::Indexing, JobStatus::Completed,
        JobStatus::Failed, JobStatus::Interrupted, JobStatus::Cancelled
    };

    for (JobStatus status : allStatuses) {
        auto res = m_ctx.ingestionRepo->listJobsByStatus(status);
        if (!res.isOk()) continue;

        for (const IngestionJob& job : res.value()) {
            const int r = m_jobsTable->rowCount();
            m_jobsTable->insertRow(r);

            QString jobType;
            switch (job.type) {
                case JobType::QuestionImport: jobType = tr("Question Import"); break;
                case JobType::RosterImport:   jobType = tr("Roster Import"); break;
                default:                      jobType = tr("Unknown"); break;
            }

            QString statusStr;
            switch (job.status) {
                case JobStatus::Pending:     statusStr = tr("Pending"); break;
                case JobStatus::Claimed:     statusStr = tr("Claimed"); break;
                case JobStatus::Validating:  statusStr = tr("Validating"); break;
                case JobStatus::Importing:   statusStr = tr("Importing"); break;
                case JobStatus::Indexing:    statusStr = tr("Indexing"); break;
                case JobStatus::Completed:   statusStr = tr("Completed"); break;
                case JobStatus::Failed:      statusStr = tr("Failed"); break;
                case JobStatus::Interrupted: statusStr = tr("Interrupted"); break;
                case JobStatus::Cancelled:   statusStr = tr("Cancelled"); break;
            }

            QString phaseStr;
            switch (job.currentPhase) {
                case JobPhase::Validate:  phaseStr = tr("Validate"); break;
                case JobPhase::Import:    phaseStr = tr("Import"); break;
                case JobPhase::Index:     phaseStr = tr("Index"); break;
                default:                  phaseStr = tr("—"); break;
            }

            auto* idItem = new QTableWidgetItem(job.id);
            idItem->setData(Qt::UserRole, job.id);
            m_jobsTable->setItem(r, 0, idItem);
            m_jobsTable->setItem(r, 1, new QTableWidgetItem(jobType));
            m_jobsTable->setItem(r, 2, new QTableWidgetItem(statusStr));
            m_jobsTable->setItem(r, 3, new QTableWidgetItem(phaseStr));
            m_jobsTable->setItem(r, 4, new QTableWidgetItem(QString::number(job.priority)));
            m_jobsTable->setItem(r, 5, new QTableWidgetItem(QString::number(job.retryCount)));
            m_jobsTable->setItem(r, 6, new QTableWidgetItem(job.createdAt.toString(Qt::ISODate)));
        }
    }

    const int total = m_jobsTable->rowCount();
    m_statusLabel->setText(tr("%1 job(s) total").arg(total));
}

void IngestionMonitorWindow::onCancelJob()
{
    const int row = m_jobsTable->currentRow();
    if (row < 0 || !m_ctx.ingestionService) return;

    const QString jobId = m_jobsTable->item(row, 0)->data(Qt::UserRole).toString();

    if (QMessageBox::question(this, tr("Cancel Job"),
            tr("Cancel ingestion job %1?").arg(jobId)) != QMessageBox::Yes)
        return;

    auto res = m_ctx.ingestionService->cancelJob(jobId, m_ctx.session.userId);
    if (!res.isOk()) {
        QMessageBox::warning(this, tr("Cannot Cancel"), res.errorMessage());
        return;
    }

    onRefresh();
}
