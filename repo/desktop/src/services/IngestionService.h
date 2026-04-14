#pragma once
// IngestionService.h — ProctorOps
// Import pipeline logic: job creation, cancellation, and three-phase execution
// (validate → import → index) with checkpoint support for question files (JSONL)
// and member rosters (CSV).

#include "repositories/IIngestionRepository.h"
#include "repositories/IQuestionRepository.h"
#include "repositories/IMemberRepository.h"
#include "services/AuditService.h"
#include "crypto/AesGcmCipher.h"
#include "utils/Result.h"
#include "models/Ingestion.h"

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <optional>

class IKnowledgePointRepository;
class AuthService;

class IngestionService {
public:
    IngestionService(IIngestionRepository& ingestionRepo,
                     IQuestionRepository& questionRepo,
                     IKnowledgePointRepository& kpRepo,
                     IMemberRepository& memberRepo,
                     AuditService& auditService,
                     AuthService& authService,
                     AesGcmCipher& cipher);

    // ── Job lifecycle ─────────────────────────────────────────────────────

    /// Create a new ingestion job. Dependencies are validated and recorded.
    [[nodiscard]] Result<IngestionJob> createJob(
        JobType type,
        const QString& sourceFilePath,
        int priority,
        const QString& actorUserId,
        const QDateTime& scheduledAt = {},
        const QStringList& dependsOnJobIds = {});

    /// Cancel a pending job. Only Pending jobs can be cancelled.
    [[nodiscard]] Result<void> cancelJob(const QString& jobId,
                                          const QString& actorUserId);

    /// Execute all three phases of a job sequentially (validate → import → index).
    /// Called by the scheduler's worker thread.
    Result<void> executeJob(const QString& jobId,
                             const QString& workerId);

private:
    IIngestionRepository&      m_ingestionRepo;
    IQuestionRepository&       m_questionRepo;
    IKnowledgePointRepository& m_kpRepo;
    IMemberRepository&         m_memberRepo;
    AuditService&              m_auditService;
    AuthService&               m_authService;
    AesGcmCipher&              m_cipher;

    // ── Phase executors ───────────────────────────────────────────────────

    /// Validate phase: parse file, check schema, validate all rows.
    [[nodiscard]] Result<void> executeValidatePhase(
        const IngestionJob& job,
        const std::optional<JobCheckpoint>& checkpoint);

    /// Import phase: write validated rows to domain tables.
    [[nodiscard]] Result<void> executeImportPhase(
        const IngestionJob& job,
        const std::optional<JobCheckpoint>& checkpoint);

    /// Index phase: recompute any materialized data.
    [[nodiscard]] Result<void> executeIndexPhase(
        const IngestionJob& job,
        const std::optional<JobCheckpoint>& checkpoint);

    // ── Question import helpers (JSONL) ───────────────────────────────────

    [[nodiscard]] Result<void> validateQuestionFile(
        const QString& filePath,
        const std::optional<JobCheckpoint>& checkpoint,
        const QString& jobId);

    [[nodiscard]] Result<void> importQuestionFile(
        const QString& filePath,
        const std::optional<JobCheckpoint>& checkpoint,
        const QString& jobId,
        const QString& actorUserId);

    // ── Roster import helpers (CSV) ───────────────────────────────────────

    [[nodiscard]] Result<void> validateRosterFile(
        const QString& filePath,
        const std::optional<JobCheckpoint>& checkpoint,
        const QString& jobId);

    [[nodiscard]] Result<void> importRosterFile(
        const QString& filePath,
        const std::optional<JobCheckpoint>& checkpoint,
        const QString& jobId,
        const QString& actorUserId);

    // ── KP/Tag resolution helpers ─────────────────────────────────────────

    /// Resolve a knowledge-point by slash-delimited path, creating nodes as needed.
    /// Maps the question to the resolved KP.
    void resolveAndMapKP(const QString& questionId, const QString& kpPath,
                         const QString& actorUserId);

    /// Find a tag by name (or create it) and map it to the question.
    void resolveAndMapTag(const QString& questionId, const QString& tagName,
                          const QString& actorUserId);

    // ── Shared helpers ────────────────────────────────────────────────────

    /// Save a checkpoint at the current position.
    void saveProgress(const QString& jobId, JobPhase phase,
                      qint64 offsetBytes, int recordsProcessed);

    /// Batch size for checkpoint saves during file processing.
    static constexpr int CheckpointBatchSize = 100;

    /// Maximum error rate (fraction) before aborting a phase.
    static constexpr double MaxErrorRate = 0.50;
};
