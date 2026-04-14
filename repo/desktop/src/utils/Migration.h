#pragma once
// Migration.h — ProctorOps
// Sequential SQLite migration runner.
//
// Discovers all *.sql files under the configured migration directory, ordered
// lexicographically by file name (e.g. 0001_initial_schema.sql before
// 0002_identity_schema.sql). Each unapplied migration is executed inside a
// single transaction and recorded in the schema_migrations table.
//
// On failure, the active transaction is rolled back and a MigrationResult with
// success=false is returned. The application refuses to start on migration failure.

#include <QString>
#include <QSqlDatabase>

struct MigrationResult {
    bool    success{true};
    int     applied{0};       // number of migrations applied in this run
    QString errorMessage;     // populated only when success == false
};

class Migration {
public:
    /// Construct with the open database connection and directory holding *.sql files.
    Migration(QSqlDatabase& db, const QString& migrationDir);

    /// Apply all pending migrations. Returns immediately on first failure.
    [[nodiscard]] MigrationResult applyPending();

private:
    QSqlDatabase& m_db;
    QString       m_migrationDir;

    /// Return absolute paths of all *.sql files, sorted by name.
    [[nodiscard]] QStringList discoverMigrations() const;

    /// True if the migration name already appears in schema_migrations.
    [[nodiscard]] bool isApplied(const QString& name) const;

    /// Execute all statements in the given file inside a transaction.
    [[nodiscard]] bool applyMigration(const QString& name, const QString& filePath);

    /// Insert a record into schema_migrations.
    [[nodiscard]] bool recordApplied(const QString& name);
};
