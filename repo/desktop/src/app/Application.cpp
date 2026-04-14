// Application.cpp — ProctorOps

#include "app/Application.h"
#include "app/AppSettings.h"
#include "app/WorkspaceState.h"
#include "utils/Migration.h"
#include "utils/PerformanceObserver.h"
#include "utils/Logger.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv)
{
    m_startTimer.start();

    setApplicationName(QStringLiteral("ProctorOps"));
    setApplicationVersion(QStringLiteral("0.1.0"));
    setOrganizationName(QStringLiteral("ProctorOps"));
    setOrganizationDomain(QStringLiteral("proctorops.local"));

    m_settings = std::make_unique<AppSettings>();
}

Application::~Application()
{
    if (m_lifecycleId > 0)
        recordCleanShutdown();

    if (m_perfObserver)
        m_perfObserver->stopMemoryObservation();

    if (m_db.isOpen())
        m_db.close();

    QSqlDatabase::removeDatabase(QStringLiteral("proctorops_main"));
}

bool Application::initialize()
{
    if (!openDatabase())    return false;
    if (!runMigrations())   return false;

    m_crashDetected = detectCrash();
    m_lifecycleId   = recordSessionStart();

    m_workspaceState = std::make_unique<WorkspaceState>(m_db);
    m_workspaceState->load();

    m_perfObserver = std::make_unique<PerformanceObserver>(m_db);

    m_coldStartMs = m_startTimer.elapsed();
    m_perfObserver->recordColdStart(m_coldStartMs);

    Logger::instance().info(
        QStringLiteral("Application"),
        QStringLiteral("Application initialized"),
        {{QStringLiteral("cold_start_ms"),  QString::number(m_coldStartMs)},
         {QStringLiteral("crash_detected"), m_crashDetected
                                             ? QStringLiteral("true")
                                             : QStringLiteral("false")}});
    return true;
}

bool Application::openDatabase()
{
    const QString dbPath = m_settings->databasePath();

    // Ensure the parent directory exists
    QDir().mkpath(QFileInfo(dbPath).absolutePath());

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("proctorops_main"));
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        Logger::instance().error(
            QStringLiteral("Application"),
            QStringLiteral("Failed to open database"),
            {{QStringLiteral("path"),  dbPath},
             {QStringLiteral("error"), m_db.lastError().text()}});
        return false;
    }

    // Apply performance and safety pragmas on every connection open
    auto pragma = [&](const char* sql) {
        QSqlQuery q(m_db);
        q.exec(QLatin1String(sql));
    };
    pragma("PRAGMA journal_mode=WAL");
    pragma("PRAGMA foreign_keys=ON");
    pragma("PRAGMA synchronous=NORMAL");

    return true;
}

bool Application::runMigrations()
{
    Migration migration(m_db, m_settings->migrationDir());
    const auto result = migration.applyPending();
    if (!result.success) {
        Logger::instance().error(
            QStringLiteral("Application"),
            QStringLiteral("Migration failed"),
            {{QStringLiteral("error"), result.errorMessage}});
        return false;
    }
    if (result.applied > 0) {
        Logger::instance().info(
            QStringLiteral("Application"),
            QStringLiteral("Migrations applied"),
            {{QStringLiteral("count"), QString::number(result.applied)}});
    }
    return true;
}

bool Application::detectCrash()
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral(
        "SELECT id FROM app_lifecycle "
        "WHERE clean_shutdown_at IS NULL "
        "ORDER BY id DESC LIMIT 1"
    ));
    return q.next();
}

int Application::recordSessionStart()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO app_lifecycle (started_at, app_version) VALUES (?, ?)"
    ));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.addBindValue(applicationVersion());
    if (!q.exec()) return -1;
    return static_cast<int>(q.lastInsertId().toLongLong());
}

void Application::recordCleanShutdown()
{
    if (m_lifecycleId <= 0) return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE app_lifecycle SET clean_shutdown_at = ? WHERE id = ?"
    ));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.addBindValue(m_lifecycleId);
    q.exec();
    m_lifecycleId = -1; // prevent double-write in destructor
}
