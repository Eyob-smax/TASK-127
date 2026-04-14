// Migration.cpp — ProctorOps

#include "utils/Migration.h"
#include "utils/Logger.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDateTime>

Migration::Migration(QSqlDatabase& db, const QString& migrationDir)
    : m_db(db), m_migrationDir(migrationDir)
{}

MigrationResult Migration::applyPending()
{
    // Bootstrap: idempotent creation of schema_migrations table so the
    // migration tracking infrastructure exists before any migration is applied.
    {
        QSqlQuery q(m_db);
        if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schema_migrations ("
            "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  migration   TEXT    NOT NULL UNIQUE,"
            "  applied_at  TEXT    NOT NULL"
            ")"
        ))) {
            return {false, 0, q.lastError().text()};
        }
    }

    const QStringList files = discoverMigrations();
    int applied = 0;

    for (const QString& filePath : files) {
        const QString name = QFileInfo(filePath).completeBaseName();
        if (isApplied(name)) continue;

        if (!applyMigration(name, filePath)) {
            return {false, applied,
                    QStringLiteral("Migration '%1' failed — check logs").arg(name)};
        }
        ++applied;
    }

    return {true, applied, {}};
}

QStringList Migration::discoverMigrations() const
{
    QDir dir(m_migrationDir);
    const QStringList names = dir.entryList(
        {QStringLiteral("*.sql")}, QDir::Files, QDir::Name);
    QStringList result;
    result.reserve(names.size());
    for (const QString& f : names)
        result.append(dir.absoluteFilePath(f));
    return result;
}

bool Migration::isApplied(const QString& name) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT 1 FROM schema_migrations WHERE migration = ?"
    ));
    q.addBindValue(name);
    q.exec();
    return q.next();
}

bool Migration::applyMigration(const QString& name, const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Logger::instance().error("Cannot open migration file",
            {{"file", filePath}});
        return false;
    }
    const QString sql = QTextStream(&file).readAll();
    file.close();

    // Normalize SQL by stripping line comments before semicolon splitting.
    // This avoids breaking statements when comment text contains semicolons.
    QStringList normalizedLines;
    normalizedLines.reserve(256);
    for (const QString& rawLine : sql.split(QLatin1Char('\n'))) {
        const int commentAt = rawLine.indexOf(QStringLiteral("--"));
        if (commentAt >= 0) {
            normalizedLines.append(rawLine.left(commentAt));
        } else {
            normalizedLines.append(rawLine);
        }
    }
    const QString normalizedSql = normalizedLines.join(QLatin1Char('\n'));

    if (!m_db.transaction()) {
        Logger::instance().error("Cannot open transaction for migration",
            {{"migration", name}});
        return false;
    }

    // Execute statements separated by semicolons, skipping comments and blanks.
    const QStringList statements = normalizedSql.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& stmt : statements) {
        QStringList executableLines;
        executableLines.reserve(16);
        for (const QString& rawLine : stmt.split(QLatin1Char('\n'))) {
            const QString line = rawLine.trimmed();
            if (line.isEmpty() || line.startsWith(QStringLiteral("--"))) {
                continue;
            }
            executableLines.append(rawLine);
        }

        const QString trimmed = executableLines.join(QLatin1Char('\n')).trimmed();
        if (trimmed.isEmpty()) continue;

        QSqlQuery q(m_db);
        if (!q.exec(trimmed)) {
            Logger::instance().error("Migration statement failed",
                {{"migration", name}, {"error", q.lastError().text()}});
            m_db.rollback();
            return false;
        }
    }

    if (!recordApplied(name)) {
        m_db.rollback();
        return false;
    }

    return m_db.commit();
}

bool Migration::recordApplied(const QString& name)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO schema_migrations (migration, applied_at) VALUES (?, ?)"
    ));
    q.addBindValue(name);
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return q.exec();
}
