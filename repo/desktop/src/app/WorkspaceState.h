#pragma once
// WorkspaceState.h — ProctorOps
// Serializes and restores the operator workspace to the workspace_state table.
//
// Tracks: open feature windows, pending workflow action markers, and interrupted
// ingestion job IDs. Application loads this after crash detection to decide
// whether to prompt for recovery. Each mutating call immediately saves to SQLite
// so the state remains consistent even if the process exits unexpectedly.

#include <QSqlDatabase>
#include <QStringList>

/// A point-in-time snapshot of the workspace state.
struct WorkspaceSnapshot {
    QStringList openWindows;           // window identifiers, e.g. "CheckInWindow"
    QStringList pendingActionMarkers;  // e.g. "correction:request:req-001"
    QStringList interruptedJobIds;     // ingestion job IDs in-progress at last save
};

class WorkspaceState {
public:
    explicit WorkspaceState(QSqlDatabase& db);

    /// Load persisted state from workspace_state (id = 1). Returns true on success.
    bool load();

    /// Persist the current snapshot to workspace_state (upsert, id = 1).
    bool save();

    /// Mark a feature window as open and save.
    void markWindowOpen(const QString& windowId);

    /// Mark a feature window as closed and save.
    void markWindowClosed(const QString& windowId);

    /// Add a pending workflow action marker and save.
    void addPendingAction(const QString& marker);

    /// Remove a pending workflow action marker on completion and save.
    void removePendingAction(const QString& marker);

    /// Record an interrupted ingestion job ID and save.
    void addInterruptedJob(const QString& jobId);

    /// Clear all interrupted job IDs after recovery has been handled and save.
    void clearInterruptedJobs();

    [[nodiscard]] const WorkspaceSnapshot& snapshot() const { return m_snapshot; }

private:
    QSqlDatabase&    m_db;
    WorkspaceSnapshot m_snapshot;

    [[nodiscard]] static QStringList jsonToList(const QString& json);
    [[nodiscard]] static QString     listToJson(const QStringList& list);
};
