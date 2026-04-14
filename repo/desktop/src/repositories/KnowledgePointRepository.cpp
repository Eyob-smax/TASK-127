// KnowledgePointRepository.cpp — ProctorOps
// Concrete SQLite implementation for the KnowledgePoint chapter tree.
// Maintains materialized paths for efficient subtree queries.

#include "KnowledgePointRepository.h"

#include <QSqlQuery>
#include <QSqlError>

KnowledgePointRepository::KnowledgePointRepository(QSqlDatabase& db)
    : m_db(db)
{
}

// ── Helper: row → KnowledgePoint ────────────────────────────────────────────

KnowledgePoint KnowledgePointRepository::rowToKP(const QSqlQuery& q)
{
    KnowledgePoint kp;
    kp.id        = q.value(0).toString();
    kp.name      = q.value(1).toString();
    kp.parentId  = q.value(2).toString();
    kp.position  = q.value(3).toInt();
    kp.path      = q.value(4).toString();
    kp.createdAt = QDateTime::fromString(q.value(5).toString(), Qt::ISODateWithMs);
    kp.deleted   = q.value(6).toBool();
    return kp;
}

// ── Path computation ────────────────────────────────────────────────────────

Result<QString> KnowledgePointRepository::computePath(const QString& parentId,
                                                        const QString& name)
{
    if (parentId.isEmpty())
        return Result<QString>::ok(name);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT path FROM knowledge_points WHERE id = ? AND deleted = 0"));
    q.addBindValue(parentId);

    if (!q.exec())
        return Result<QString>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<QString>::err(ErrorCode::NotFound,
                                     QStringLiteral("Parent KP not found"));

    return Result<QString>::ok(q.value(0).toString() + QStringLiteral("/") + name);
}

Result<void> KnowledgePointRepository::propagatePathChange(const QString& oldPrefix,
                                                             const QString& newPrefix)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE knowledge_points "
        "SET path = ? || substr(path, ?) "
        "WHERE path LIKE ? || '/%'"));
    q.addBindValue(newPrefix);
    q.addBindValue(oldPrefix.length() + 1);  // skip old prefix length
    q.addBindValue(oldPrefix);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());

    return Result<void>::ok();
}

// ── CRUD ─────────────────────────────────────────────────────────────────────

Result<KnowledgePoint> KnowledgePointRepository::insertKP(const KnowledgePoint& kp)
{
    auto pathResult = computePath(kp.parentId, kp.name);
    if (pathResult.isErr())
        return Result<KnowledgePoint>::err(pathResult.errorCode(), pathResult.errorMessage());

    KnowledgePoint out = kp;
    out.path = pathResult.value();

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO knowledge_points "
        "(id, name, parent_id, position, path, created_at, deleted) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(out.id);
    q.addBindValue(out.name);
    q.addBindValue(out.parentId.isEmpty() ? QVariant() : out.parentId);
    q.addBindValue(out.position);
    q.addBindValue(out.path);
    q.addBindValue(out.createdAt.toString(Qt::ISODateWithMs));
    q.addBindValue(out.deleted ? 1 : 0);

    if (!q.exec())
        return Result<KnowledgePoint>::err(ErrorCode::DbError, q.lastError().text());

    return Result<KnowledgePoint>::ok(std::move(out));
}

Result<KnowledgePoint> KnowledgePointRepository::updateKP(const KnowledgePoint& kp)
{
    // Get old path for propagation
    auto oldResult = findKPById(kp.id);
    if (oldResult.isErr())
        return oldResult;

    const QString oldPath = oldResult.value().path;

    auto pathResult = computePath(kp.parentId, kp.name);
    if (pathResult.isErr())
        return Result<KnowledgePoint>::err(pathResult.errorCode(), pathResult.errorMessage());

    KnowledgePoint out = kp;
    out.path = pathResult.value();

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE knowledge_points "
        "SET name = ?, parent_id = ?, position = ?, path = ? "
        "WHERE id = ?"));
    q.addBindValue(out.name);
    q.addBindValue(out.parentId.isEmpty() ? QVariant() : out.parentId);
    q.addBindValue(out.position);
    q.addBindValue(out.path);
    q.addBindValue(out.id);

    if (!q.exec())
        return Result<KnowledgePoint>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<KnowledgePoint>::err(ErrorCode::NotFound);

    // Propagate path change to all descendants
    if (oldPath != out.path) {
        auto propResult = propagatePathChange(oldPath, out.path);
        if (propResult.isErr())
            return Result<KnowledgePoint>::err(propResult.errorCode(), propResult.errorMessage());
    }

    return Result<KnowledgePoint>::ok(std::move(out));
}

Result<void> KnowledgePointRepository::softDeleteKP(const QString& kpId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE knowledge_points SET deleted = 1 WHERE id = ?"));
    q.addBindValue(kpId);

    if (!q.exec())
        return Result<void>::err(ErrorCode::DbError, q.lastError().text());
    if (q.numRowsAffected() == 0)
        return Result<void>::err(ErrorCode::NotFound);

    return Result<void>::ok();
}

Result<KnowledgePoint> KnowledgePointRepository::findKPById(const QString& kpId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, name, parent_id, position, path, created_at, deleted "
        "FROM knowledge_points WHERE id = ?"));
    q.addBindValue(kpId);

    if (!q.exec())
        return Result<KnowledgePoint>::err(ErrorCode::DbError, q.lastError().text());
    if (!q.next())
        return Result<KnowledgePoint>::err(ErrorCode::NotFound);

    return Result<KnowledgePoint>::ok(rowToKP(q));
}

Result<QList<KnowledgePoint>> KnowledgePointRepository::getTree()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, name, parent_id, position, path, created_at, deleted "
        "FROM knowledge_points WHERE deleted = 0 "
        "ORDER BY path, position"));

    if (!q.exec())
        return Result<QList<KnowledgePoint>>::err(ErrorCode::DbError, q.lastError().text());

    QList<KnowledgePoint> results;
    while (q.next())
        results.append(rowToKP(q));

    return Result<QList<KnowledgePoint>>::ok(std::move(results));
}

Result<QList<KnowledgePoint>> KnowledgePointRepository::getDescendants(const QString& kpId)
{
    // First get the KP's path
    auto kpResult = findKPById(kpId);
    if (kpResult.isErr())
        return Result<QList<KnowledgePoint>>::err(kpResult.errorCode(), kpResult.errorMessage());

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, name, parent_id, position, path, created_at, deleted "
        "FROM knowledge_points "
        "WHERE path LIKE ? || '/%' AND deleted = 0 "
        "ORDER BY path, position"));
    q.addBindValue(kpResult.value().path);

    if (!q.exec())
        return Result<QList<KnowledgePoint>>::err(ErrorCode::DbError, q.lastError().text());

    QList<KnowledgePoint> results;
    while (q.next())
        results.append(rowToKP(q));

    return Result<QList<KnowledgePoint>>::ok(std::move(results));
}
