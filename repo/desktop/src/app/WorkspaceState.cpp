// WorkspaceState.cpp — ProctorOps

#include "app/WorkspaceState.h"
#include <QSqlQuery>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>

WorkspaceState::WorkspaceState(QSqlDatabase& db)
    : m_db(db)
{}

bool WorkspaceState::load()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT open_windows, pending_actions, interrupted_job_ids "
        "FROM workspace_state WHERE id = 1"
    ));
    if (!q.exec() || !q.next()) {
        // No saved state — start with empty snapshot.
        m_snapshot = {};
        return true;
    }
    m_snapshot.openWindows          = jsonToList(q.value(0).toString());
    m_snapshot.pendingActionMarkers = jsonToList(q.value(1).toString());
    m_snapshot.interruptedJobIds    = jsonToList(q.value(2).toString());
    return true;
}

bool WorkspaceState::save()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO workspace_state "
        "  (id, open_windows, pending_actions, interrupted_job_ids, updated_at) "
        "VALUES (1, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "  open_windows        = excluded.open_windows, "
        "  pending_actions     = excluded.pending_actions, "
        "  interrupted_job_ids = excluded.interrupted_job_ids, "
        "  updated_at          = excluded.updated_at"
    ));
    q.addBindValue(listToJson(m_snapshot.openWindows));
    q.addBindValue(listToJson(m_snapshot.pendingActionMarkers));
    q.addBindValue(listToJson(m_snapshot.interruptedJobIds));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return q.exec();
}

void WorkspaceState::markWindowOpen(const QString& windowId)
{
    if (!m_snapshot.openWindows.contains(windowId))
        m_snapshot.openWindows.append(windowId);
    save();
}

void WorkspaceState::markWindowClosed(const QString& windowId)
{
    m_snapshot.openWindows.removeAll(windowId);
    save();
}

void WorkspaceState::addPendingAction(const QString& marker)
{
    if (!m_snapshot.pendingActionMarkers.contains(marker))
        m_snapshot.pendingActionMarkers.append(marker);
    save();
}

void WorkspaceState::removePendingAction(const QString& marker)
{
    m_snapshot.pendingActionMarkers.removeAll(marker);
    save();
}

void WorkspaceState::addInterruptedJob(const QString& jobId)
{
    if (!m_snapshot.interruptedJobIds.contains(jobId))
        m_snapshot.interruptedJobIds.append(jobId);
    save();
}

void WorkspaceState::clearInterruptedJobs()
{
    m_snapshot.interruptedJobIds.clear();
    save();
}

QStringList WorkspaceState::jsonToList(const QString& json)
{
    QStringList result;
    const auto doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isArray()) return result;
    for (const auto& val : doc.array())
        result.append(val.toString());
    return result;
}

QString WorkspaceState::listToJson(const QStringList& list)
{
    QJsonArray arr;
    for (const auto& s : list) arr.append(s);
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
