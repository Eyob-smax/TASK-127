#pragma once
// IngestionMonitorWindow.h — ProctorOps
// Scheduler and ingestion job observability window.
//
// Displays running, pending, completed, and failed ingestion jobs with their
// current phase (Validating / Importing / Indexing), retry count, and last status.
// Provides manual job cancellation and priority inspection for administrators.

#include <QWidget>

class QTableWidget;
class QPushButton;
class QLabel;
class QTimer;
class AppContext;

class IngestionMonitorWindow : public QWidget {
    Q_OBJECT
public:
    static constexpr const char* WindowId = "window.ingestion_monitor";

    explicit IngestionMonitorWindow(AppContext& ctx, QWidget* parent = nullptr);
    ~IngestionMonitorWindow() override;

private slots:
    void onRefresh();
    void onCancelJob();

private:
    void setupUi();

    AppContext&    m_ctx;
    QTableWidget*  m_jobsTable{nullptr};
    QPushButton*   m_cancelBtn{nullptr};
    QLabel*        m_statusLabel{nullptr};
    QTimer*        m_refreshTimer{nullptr};
};
