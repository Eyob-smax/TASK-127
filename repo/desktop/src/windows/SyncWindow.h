#pragma once
// SyncWindow.h — ProctorOps
// Offline desk-to-desk sync management window.
// Operators export signed sync packages to LAN share or USB,
// import packages from other desks, review conflict records,
// and manage trusted signing keys.

#include <QWidget>
#include <QDateTime>

class QTableWidget;
class QTabWidget;
class QPushButton;
class QLabel;
class QGroupBox;
class QSplitter;
class AppContext;

class SyncWindow : public QWidget {
    Q_OBJECT
public:
    static constexpr const char* WindowId = "window.sync";

    explicit SyncWindow(AppContext& ctx, QWidget* parent = nullptr);

private slots:
    void onExportPackage();
    void onImportPackage();
    void onResolveConflict();
    void onImportSigningKey();
    void onRevokeSigningKey();
    void onRefreshPackages();
    void onRefreshConflicts();
    void onRefreshKeys();
    void onPackageSelected();

private:
    void setupUi();
    void setupPackagesTab(QWidget* tab);
    void setupConflictsTab(QWidget* tab);
    void setupKeysTab(QWidget* tab);

    AppContext&    m_ctx;

    // Packages tab
    QTableWidget*  m_packagesTable{nullptr};
    QPushButton*   m_exportBtn{nullptr};
    QPushButton*   m_importBtn{nullptr};

    // Conflicts tab
    QTableWidget*  m_conflictsTable{nullptr};
    QPushButton*   m_resolveBtn{nullptr};
    QLabel*        m_conflictsHint{nullptr};

    // Keys tab
    QTableWidget*  m_keysTable{nullptr};
    QPushButton*   m_importKeyBtn{nullptr};
    QPushButton*   m_revokeKeyBtn{nullptr};

    // Currently selected package (for conflict display)
    QString        m_selectedPackageId;
};
