#pragma once
// AuditViewerWindow.h — ProctorOps
// Read-only tamper-evident audit log viewer.
//
// Top filter bar:
//   Date range (from/to DateTimeEdit), actor user ID, event type combo,
//   entity type, entity ID, Apply button
//
// QTableView showing AuditEntry rows (id, timestamp, actor, event type, entity).
// Selecting a row expands a detail pane below showing:
//   - Entry hash and previous-entry hash
//   - Before payload (JSON, decrypted display)
//   - After payload (JSON)
//
// Export button: writes selected or all visible entries to a JSON-lines file.
// Verify Chain button: calls AuditService::verifyChain() and shows result dialog.

#include <QWidget>
#include "models/Audit.h"

class QTableView;
class QStandardItemModel;
class QDateTimeEdit;
class QLineEdit;
class QComboBox;
class QPushButton;
class QTextEdit;
class QLabel;
class QSplitter;
struct AppContext;

class AuditViewerWindow : public QWidget {
    Q_OBJECT

public:
    explicit AuditViewerWindow(AppContext& ctx, QWidget* parent = nullptr);

    static constexpr const char* WindowId = "window.audit_viewer";

private slots:
    void onApplyFilter();
    void onSelectionChanged();
    void onExport();
    void onVerifyChain();
    void onLoadMore();

private:
    void setupUi();
    void buildFilter(AuditFilter& filter) const;
    void loadEntries(bool append = false);
    void populateTable(const QList<AuditEntry>& entries, bool append);
    void showEntryDetail(const AuditEntry& entry);

    AppContext&          m_ctx;
    QList<AuditEntry>    m_entries;
    int                  m_currentOffset{0};

    // Filter controls
    QDateTimeEdit*       m_fromEdit{nullptr};
    QDateTimeEdit*       m_toEdit{nullptr};
    QLineEdit*           m_actorEdit{nullptr};
    QComboBox*           m_eventTypeCombo{nullptr};
    QLineEdit*           m_entityTypeEdit{nullptr};
    QLineEdit*           m_entityIdEdit{nullptr};

    // Table
    QTableView*          m_tableView{nullptr};
    QStandardItemModel*  m_model{nullptr};

    // Detail pane
    QTextEdit*           m_detailPane{nullptr};

    // Footer
    QLabel*              m_statusLabel{nullptr};
    QPushButton*         m_loadMoreBtn{nullptr};
};
