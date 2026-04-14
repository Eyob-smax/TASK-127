#pragma once
// PerformanceObserver.h — ProctorOps
// Instruments cold-start time and periodic process memory usage.
//
// PURPOSE: Provide observable, logged evidence for manual verification of the
// engineering targets set in docs/design.md §4:
//   - Cold-start < 3 seconds on a representative office PC
//   - Memory growth < 200 MB above baseline over 7 days of continuous operation
//
// These targets cannot be proven by static code alone. PerformanceObserver
// records timing and memory samples to performance_log (SQLite) and Logger.
// Final numeric verification requires a manual check on a representative office PC.
//
// Thread safety: must be used from the main thread only (QTimer-based sampling).

#include <QObject>
#include <QSqlDatabase>
#include <QTimer>

class PerformanceObserver : public QObject {
    Q_OBJECT

public:
    explicit PerformanceObserver(QSqlDatabase& db, QObject* parent = nullptr);

    /// Record cold-start elapsed time. Call once, immediately after Application::initialize().
    void recordColdStart(qint64 elapsedMs);

    /// Begin periodic memory sampling (default: every 60 seconds).
    void startMemoryObservation(int sampleIntervalMs = 60000);

    /// Stop periodic memory sampling (call on shutdown).
    void stopMemoryObservation();

    /// Current process RSS (Resident Set Size) in bytes.
    /// Returns 0 if the platform query is unavailable.
    [[nodiscard]] static qint64 currentRssBytes();

private slots:
    void onSampleTimer();

private:
    void recordEvent(const QString& eventType, qint64 valueMs, qint64 valueBytes);

    QSqlDatabase& m_db;
    QTimer        m_sampleTimer;
};
