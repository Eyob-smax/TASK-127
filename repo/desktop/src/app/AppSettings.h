#pragma once
// AppSettings.h — ProctorOps
// QSettings-backed application preferences. Never stores secrets here —
// only paths, UI modes, log levels, and window geometry.

#include <QByteArray>
#include <QString>

class AppSettings {
public:
    AppSettings();

    // ── Database ──────────────────────────────────────────────────────────────
    /// Absolute path to the SQLite database file.
    [[nodiscard]] QString databasePath() const;
    void setDatabasePath(const QString& path);

    /// Directory containing SQL migration files (*.sql), ordered by name.
    [[nodiscard]] QString migrationDir() const;

    // ── Logging ───────────────────────────────────────────────────────────────
    /// Log level: "debug" | "info" | "warn" | "error"
    [[nodiscard]] QString logLevel() const;
    void setLogLevel(const QString& level);

    // ── UI / shell preferences ─────────────────────────────────────────────────
    /// Kiosk mode: minimize to tray and hide from taskbar when idle.
    [[nodiscard]] bool kioskMode() const;
    void setKioskMode(bool enabled);

    /// Main window geometry blob (QMainWindow::saveGeometry / restoreGeometry).
    [[nodiscard]] QByteArray mainWindowGeometry() const;
    void setMainWindowGeometry(const QByteArray& geometry);

private:
    class QSettings* m_settings{nullptr};
    class QSettings& settings();
};
