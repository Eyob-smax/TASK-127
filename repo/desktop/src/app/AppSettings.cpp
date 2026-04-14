// AppSettings.cpp — ProctorOps

#include "app/AppSettings.h"
#include <QSettings>
#include <QStandardPaths>
#include <QCoreApplication>

AppSettings::AppSettings() = default;

QSettings& AppSettings::settings()
{
    if (!m_settings) {
        m_settings = new QSettings(
            QStringLiteral("ProctorOps"),
            QStringLiteral("proctorops"),
            qApp
        );
    }
    return *m_settings;
}

QString AppSettings::databasePath() const
{
    const QString defaultPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/proctorops.db");
    return const_cast<AppSettings*>(this)->settings()
        .value(QStringLiteral("database/path"), defaultPath)
        .toString();
}

void AppSettings::setDatabasePath(const QString& path)
{
    settings().setValue(QStringLiteral("database/path"), path);
}

QString AppSettings::migrationDir() const
{
    const QString defaultDir =
        QCoreApplication::applicationDirPath() + QStringLiteral("/migrations");
    return const_cast<AppSettings*>(this)->settings()
        .value(QStringLiteral("database/migration_dir"), defaultDir)
        .toString();
}

QString AppSettings::logLevel() const
{
    return const_cast<AppSettings*>(this)->settings()
        .value(QStringLiteral("logging/level"), QStringLiteral("info"))
        .toString();
}

void AppSettings::setLogLevel(const QString& level)
{
    settings().setValue(QStringLiteral("logging/level"), level);
}

bool AppSettings::kioskMode() const
{
    return const_cast<AppSettings*>(this)->settings()
        .value(QStringLiteral("ui/kiosk_mode"), false)
        .toBool();
}

void AppSettings::setKioskMode(bool enabled)
{
    settings().setValue(QStringLiteral("ui/kiosk_mode"), enabled);
}

QByteArray AppSettings::mainWindowGeometry() const
{
    return const_cast<AppSettings*>(this)->settings()
        .value(QStringLiteral("ui/main_window_geometry"))
        .toByteArray();
}

void AppSettings::setMainWindowGeometry(const QByteArray& geometry)
{
    settings().setValue(QStringLiteral("ui/main_window_geometry"), geometry);
}
