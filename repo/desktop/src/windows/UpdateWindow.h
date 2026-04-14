#pragma once
// UpdateWindow.h — ProctorOps
// Offline update and rollback management window.
//
// Override note: delivery does not require a signed .msi artifact.
// Update/rollback domain logic is fully in scope. See docs/design.md §2.
//
// Provides:
//   Staged packages tab  — import .proctorpkg from disk, view staged package details
//   Install history tab  — view applied updates, select rollback target
//   Rollback history tab — view rollback records
//
// Applying a staged package and rolling back require SecurityAdministrator + step-up.

#include <QWidget>

class QTableWidget;
class QTabWidget;
class QPushButton;
class QLabel;
class QGroupBox;
class AppContext;

class UpdateWindow : public QWidget {
    Q_OBJECT
public:
    static constexpr const char* WindowId = "window.update";

    explicit UpdateWindow(AppContext& ctx, QWidget* parent = nullptr);

private slots:
    void onImportPackage();
    void onApplyPackage();
    void onCancelPackage();
    void onRollback();
    void onRefreshPackages();
    void onRefreshHistory();
    void onRefreshRollbacks();

private:
    void setupUi();
    void setupPackagesTab(QWidget* tab);
    void setupHistoryTab(QWidget* tab);
    void setupRollbacksTab(QWidget* tab);

    AppContext&    m_ctx;

    // Packages tab
    QTableWidget*  m_packagesTable{nullptr};
    QPushButton*   m_importBtn{nullptr};
    QPushButton*   m_applyBtn{nullptr};
    QPushButton*   m_cancelBtn{nullptr};
    QLabel*        m_overrideNote{nullptr};

    // History tab
    QTableWidget*  m_historyTable{nullptr};
    QPushButton*   m_rollbackBtn{nullptr};

    // Rollbacks tab
    QTableWidget*  m_rollbacksTable{nullptr};
};
