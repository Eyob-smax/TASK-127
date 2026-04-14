#pragma once
// KnowledgePointRepository.h — ProctorOps
// Concrete SQLite implementation of IKnowledgePointRepository.
// Manages the KnowledgePoint chapter tree with materialized paths.

#include "IQuestionRepository.h"   // IKnowledgePointRepository defined here
#include <QSqlDatabase>

class KnowledgePointRepository : public IKnowledgePointRepository {
public:
    explicit KnowledgePointRepository(QSqlDatabase& db);

    Result<KnowledgePoint>         insertKP(const KnowledgePoint& kp) override;
    Result<KnowledgePoint>         updateKP(const KnowledgePoint& kp) override;
    Result<void>                   softDeleteKP(const QString& kpId) override;
    Result<KnowledgePoint>         findKPById(const QString& kpId) override;
    Result<QList<KnowledgePoint>>  getTree() override;
    Result<QList<KnowledgePoint>>  getDescendants(const QString& kpId) override;

private:
    QSqlDatabase& m_db;

    static KnowledgePoint rowToKP(const QSqlQuery& q);
    Result<QString> computePath(const QString& parentId, const QString& name);
    Result<void> propagatePathChange(const QString& oldPrefix, const QString& newPrefix);
};
