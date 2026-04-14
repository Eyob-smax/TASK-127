#pragma once
// DataSubjectWindow.h — ProctorOps
// GDPR / China MLPS data-subject workflow window.
//
// Provides two workflow tabs:
//   Export Requests — data subject access requests (Art. 15 GDPR / MLPS §8)
//   Deletion Requests — erasure requests (Art. 17 GDPR / MLPS §8)
//
// Authorization:
//   - Any authenticated operator may create requests.
//   - SecurityAdministrator + step-up required to fulfill exports or approve/complete deletions.
//   - Completion records are retained for compliance evidence.

#include <QWidget>

class QTableWidget;
class QTabWidget;
class QPushButton;
class QLabel;
class AppContext;

class DataSubjectWindow : public QWidget {
    Q_OBJECT
public:
    static constexpr const char* WindowId = "window.data_subject";

    explicit DataSubjectWindow(AppContext& ctx, QWidget* parent = nullptr);

public slots:
    /// Invoked by MainShell router action "member.mask_pii".
    /// Applies PII masking to clipboard for the selected member row.
    Q_INVOKABLE void maskSelectedPii();

    /// Invoked by MainShell router action "member.export_request".
    /// Creates an export request for the selected member row.
    Q_INVOKABLE void exportSelectedForRequest();

private slots:
    // Export tab
    void onCreateExportRequest();
    void onFulfillExportRequest();
    void onRejectExportRequest();
    void onRefreshExportRequests();

    // Deletion tab
    void onCreateDeletionRequest();
    void onApproveDeletionRequest();
    void onCompleteDeletion();
    void onRejectDeletionRequest();
    void onRefreshDeletionRequests();

private:
    void setupUi();
    void setupExportTab(QWidget* tab);
    void setupDeletionTab(QWidget* tab);

    AppContext&    m_ctx;

    // Export tab
    QTableWidget*  m_exportTable{nullptr};
    QPushButton*   m_createExportBtn{nullptr};
    QPushButton*   m_fulfillExportBtn{nullptr};
    QPushButton*   m_rejectExportBtn{nullptr};

    // Deletion tab
    QTableWidget*  m_deletionTable{nullptr};
    QPushButton*   m_createDeletionBtn{nullptr};
    QPushButton*   m_approveDeletionBtn{nullptr};
    QPushButton*   m_completeDeletionBtn{nullptr};
    QPushButton*   m_rejectDeletionBtn{nullptr};
};
