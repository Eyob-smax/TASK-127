#pragma once
// Application.h — ProctorOps
// QApplication subclass that owns startup sequencing:
//   1. Opens the SQLite database with WAL mode
//   2. Applies pending schema migrations
//   3. Detects crash from unclosed app_lifecycle record
//   4. Records the current session start in app_lifecycle
//   5. Loads workspace state for crash-recovery prompts
//   6. Records cold-start time via PerformanceObserver
//
// Destructor records a clean shutdown. If the destructor does not run (crash),
// the next launch detects the unclosed record and sets crashDetected() = true.

#include <QApplication>
#include <QElapsedTimer>
#include <QSqlDatabase>
#include <memory>

class AppSettings;
class WorkspaceState;
class PerformanceObserver;

class Application : public QApplication {
    Q_OBJECT

public:
    Application(int& argc, char** argv);
    ~Application() override;

    /// Run the full initialization sequence. Returns false on fatal failure.
    bool initialize();

    /// Write the clean_shutdown_at timestamp to app_lifecycle.
    /// Called automatically by the destructor; also callable before graceful exit.
    void recordCleanShutdown();

    // ── Accessors ─────────────────────────────────────────────────────────────
    /// True if the previous session ended without a clean shutdown record.
    [[nodiscard]] bool crashDetected() const { return m_crashDetected; }

    /// Milliseconds from process start to the end of initialize().
    [[nodiscard]] qint64 coldStartMs() const { return m_coldStartMs; }

    /// The primary SQLite database connection.
    [[nodiscard]] QSqlDatabase& database() { return m_db; }

    AppSettings&        settings()            { return *m_settings; }
    WorkspaceState&     workspaceState()      { return *m_workspaceState; }
    PerformanceObserver& performanceObserver() { return *m_perfObserver; }

private:
    bool openDatabase();
    bool runMigrations();
    bool detectCrash();
    int  recordSessionStart();

    QSqlDatabase                        m_db;
    bool                                m_crashDetected{false};
    int                                 m_lifecycleId{-1};
    qint64                              m_coldStartMs{0};
    QElapsedTimer                       m_startTimer;

    std::unique_ptr<AppSettings>        m_settings;
    std::unique_ptr<WorkspaceState>     m_workspaceState;
    std::unique_ptr<PerformanceObserver> m_perfObserver;
};
