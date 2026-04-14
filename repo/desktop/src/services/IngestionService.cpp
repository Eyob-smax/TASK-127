// IngestionService.cpp — ProctorOps
// Import pipeline: three-phase job execution (validate → import → index) with
// checkpoint-based resume for question files (JSONL) and member rosters (CSV).

#include "IngestionService.h"
#include "repositories/IQuestionRepository.h"
#include "services/CheckInService.h"
#include "services/AuthService.h"
#include "utils/Validation.h"
#include "utils/Logger.h"

#include <QUuid>
#include <QDateTime>
#include <QDate>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>

IngestionService::IngestionService(IIngestionRepository& ingestionRepo,
                                     IQuestionRepository& questionRepo,
                                     IKnowledgePointRepository& kpRepo,
                                     IMemberRepository& memberRepo,
                                     AuditService& auditService,
                                     AuthService& authService,
                                     AesGcmCipher& cipher)
    : m_ingestionRepo(ingestionRepo)
    , m_questionRepo(questionRepo)
    , m_kpRepo(kpRepo)
    , m_memberRepo(memberRepo)
    , m_auditService(auditService)
    , m_authService(authService)
    , m_cipher(cipher)
{
}

// ── Job lifecycle ───────────────────────────────────────────────────────────────

Result<IngestionJob> IngestionService::createJob(
    JobType type,
    const QString& sourceFilePath,
    int priority,
    const QString& actorUserId,
    const QDateTime& scheduledAt,
    const QStringList& dependsOnJobIds)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::ContentManager);
    if (authResult.isErr())
        return Result<IngestionJob>::err(authResult.errorCode(), authResult.errorMessage());

    // Validate priority range
    if (priority < 1 || priority > 10)
        return Result<IngestionJob>::err(ErrorCode::ValidationFailed,
                                          QStringLiteral("Priority must be between 1 and 10"));

    // Validate source file exists
    if (!QFile::exists(sourceFilePath))
        return Result<IngestionJob>::err(ErrorCode::ValidationFailed,
                                          QStringLiteral("Source file does not exist: %1").arg(sourceFilePath));

    // Validate dependencies exist
    for (const auto& depId : dependsOnJobIds) {
        auto depResult = m_ingestionRepo.getJob(depId);
        if (depResult.isErr())
            return Result<IngestionJob>::err(ErrorCode::NotFound,
                                              QStringLiteral("Dependency job not found: %1").arg(depId));
    }

    IngestionJob job;
    job.id              = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.type            = type;
    job.status          = JobStatus::Pending;
    job.priority        = priority;
    job.sourceFilePath  = sourceFilePath;
    job.scheduledAt     = scheduledAt;
    job.createdAt       = QDateTime::currentDateTimeUtc();
    job.retryCount      = 0;
    job.currentPhase    = JobPhase::Validate;
    job.createdByUserId = actorUserId;

    auto result = m_ingestionRepo.insertJob(job);
    if (result.isErr())
        return result;

    // Record dependencies
    for (const auto& depId : dependsOnJobIds) {
        JobDependency dep;
        dep.jobId         = job.id;
        dep.dependsOnJobId = depId;
        auto depResult = m_ingestionRepo.insertDependency(dep);
        if (depResult.isErr()) {
            Logger::instance().warn(QStringLiteral("IngestionService"),
                                     QStringLiteral("Failed to record dependency"),
                                     {{QStringLiteral("job_id"), job.id},
                                      {QStringLiteral("depends_on"), depId}});
        }
    }

    QJsonObject payload;
    payload[QStringLiteral("job_id")]   = job.id;
    payload[QStringLiteral("type")]     = (type == JobType::QuestionImport)
                                            ? QStringLiteral("QuestionImport")
                                            : QStringLiteral("RosterImport");
    payload[QStringLiteral("priority")] = priority;
    payload[QStringLiteral("source")]   = sourceFilePath;
    m_auditService.recordEvent(actorUserId, AuditEventType::JobCreated,
                                QStringLiteral("IngestionJob"), job.id,
                                {}, payload);

    Logger::instance().info(QStringLiteral("IngestionService"),
                             QStringLiteral("Job created"),
                             {{QStringLiteral("job_id"), job.id},
                              {QStringLiteral("type"), payload[QStringLiteral("type")].toString()}});

    return result;
}

Result<void> IngestionService::cancelJob(const QString& jobId,
                                           const QString& actorUserId)
{
    auto authResult = m_authService.requireRoleForActor(actorUserId, Role::ContentManager);
    if (authResult.isErr())
        return Result<void>::err(authResult.errorCode(), authResult.errorMessage());

    auto jobResult = m_ingestionRepo.getJob(jobId);
    if (jobResult.isErr())
        return Result<void>::err(jobResult.errorCode(), jobResult.errorMessage());

    const auto& job = jobResult.value();
    if (job.status != JobStatus::Pending)
        return Result<void>::err(ErrorCode::InvalidState,
                                  QStringLiteral("Only Pending jobs can be cancelled"));

    auto result = m_ingestionRepo.cancelJob(jobId);
    if (result.isErr())
        return result;

    QJsonObject payload;
    payload[QStringLiteral("job_id")] = jobId;
    m_auditService.recordEvent(actorUserId, AuditEventType::JobCancelled,
                                QStringLiteral("IngestionJob"), jobId,
                                payload, {});

    Logger::instance().info(QStringLiteral("IngestionService"),
                             QStringLiteral("Job cancelled"),
                             {{QStringLiteral("job_id"), jobId}});

    return result;
}

// ── Job execution ───────────────────────────────────────────────────────────────

Result<void> IngestionService::executeJob(const QString& jobId,
                                            const QString& workerId)
{
    auto jobResult = m_ingestionRepo.getJob(jobId);
    if (jobResult.isErr())
        return Result<void>::err(jobResult.errorCode(), jobResult.errorMessage());

    const auto& job = jobResult.value();

    // Audit job start
    QJsonObject startPayload;
    startPayload[QStringLiteral("job_id")]    = jobId;
    startPayload[QStringLiteral("worker_id")] = workerId;
    m_auditService.recordEvent(job.createdByUserId, AuditEventType::JobStarted,
                                QStringLiteral("IngestionJob"), jobId,
                                {}, startPayload);

    // Phase 1: Validate
    m_ingestionRepo.updateJobStatus(jobId, JobStatus::Validating);
    auto validateCp = m_ingestionRepo.loadCheckpoint(jobId, JobPhase::Validate);
    std::optional<JobCheckpoint> validateCheckpoint;
    if (validateCp.isOk())
        validateCheckpoint = validateCp.value();

    auto validateResult = executeValidatePhase(job, validateCheckpoint);
    if (validateResult.isErr()) {
        m_ingestionRepo.updateJobStatus(jobId, JobStatus::Failed, validateResult.errorMessage());
        return validateResult;
    }

    // Phase 2: Import
    m_ingestionRepo.updateJobStatus(jobId, JobStatus::Importing);
    auto importCp = m_ingestionRepo.loadCheckpoint(jobId, JobPhase::Import);
    std::optional<JobCheckpoint> importCheckpoint;
    if (importCp.isOk())
        importCheckpoint = importCp.value();

    auto importResult = executeImportPhase(job, importCheckpoint);
    if (importResult.isErr()) {
        m_ingestionRepo.updateJobStatus(jobId, JobStatus::Failed, importResult.errorMessage());
        return importResult;
    }

    // Phase 3: Index
    m_ingestionRepo.updateJobStatus(jobId, JobStatus::Indexing);
    auto indexCp = m_ingestionRepo.loadCheckpoint(jobId, JobPhase::Index);
    std::optional<JobCheckpoint> indexCheckpoint;
    if (indexCp.isOk())
        indexCheckpoint = indexCp.value();

    auto indexResult = executeIndexPhase(job, indexCheckpoint);
    if (indexResult.isErr()) {
        m_ingestionRepo.updateJobStatus(jobId, JobStatus::Failed, indexResult.errorMessage());
        return indexResult;
    }

    // All phases succeeded
    m_ingestionRepo.updateJobStatus(jobId, JobStatus::Completed);
    m_ingestionRepo.clearCheckpoints(jobId);

    Logger::instance().info(QStringLiteral("IngestionService"),
                             QStringLiteral("Job completed successfully"),
                             {{QStringLiteral("job_id"), jobId}});

    return Result<void>::ok();
}

// ── Phase dispatch ──────────────────────────────────────────────────────────────

Result<void> IngestionService::executeValidatePhase(
    const IngestionJob& job,
    const std::optional<JobCheckpoint>& checkpoint)
{
    if (job.type == JobType::QuestionImport)
        return validateQuestionFile(job.sourceFilePath, checkpoint, job.id);
    else
        return validateRosterFile(job.sourceFilePath, checkpoint, job.id);
}

Result<void> IngestionService::executeImportPhase(
    const IngestionJob& job,
    const std::optional<JobCheckpoint>& checkpoint)
{
    if (job.type == JobType::QuestionImport)
        return importQuestionFile(job.sourceFilePath, checkpoint, job.id, job.createdByUserId);
    else
        return importRosterFile(job.sourceFilePath, checkpoint, job.id, job.createdByUserId);
}

Result<void> IngestionService::executeIndexPhase(
    const IngestionJob& job,
    const std::optional<JobCheckpoint>& /* checkpoint */)
{
    // Index phase: KP paths are already maintained by the repository layer.
    // For question imports, no additional materialized data needs recomputing.
    // For roster imports, no indexing is required.
    // This phase is a no-op for now but exists in the pipeline for future use.

    saveProgress(job.id, JobPhase::Index, 0, 0);

    Logger::instance().info(QStringLiteral("IngestionService"),
                             QStringLiteral("Index phase completed (no-op)"),
                             {{QStringLiteral("job_id"), job.id}});

    return Result<void>::ok();
}

// ── Question file validation (JSONL) ────────────────────────────────────────────

Result<void> IngestionService::validateQuestionFile(
    const QString& filePath,
    const std::optional<JobCheckpoint>& checkpoint,
    const QString& jobId)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return Result<void>::err(ErrorCode::IoError,
                                  QStringLiteral("Cannot open file: %1").arg(filePath));

    // Resume from checkpoint
    int recordsProcessed = 0;
    qint64 startOffset = 0;
    if (checkpoint.has_value()) {
        startOffset = checkpoint->offsetBytes;
        recordsProcessed = checkpoint->recordsProcessed;
        file.seek(startOffset);
    }

    int validCount = 0;
    int errorCount = 0;
    int lineNumber = recordsProcessed;
    QTextStream stream(&file);

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        lineNumber++;

        if (line.isEmpty())
            continue;

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parseError);

        if (parseError.error != QJsonParseError::NoError) {
            errorCount++;
            Logger::instance().warn(QStringLiteral("IngestionService"),
                                     QStringLiteral("JSONL parse error at line %1: %2")
                                         .arg(lineNumber).arg(parseError.errorString()),
                                     {{QStringLiteral("job_id"), jobId}});
            continue;
        }

        if (!doc.isObject()) {
            errorCount++;
            continue;
        }

        QJsonObject obj = doc.object();

        // Check required fields
        bool hasError = false;

        if (!obj.contains(QStringLiteral("body_text")) ||
            obj[QStringLiteral("body_text")].toString().isEmpty()) {
            errorCount++;
            hasError = true;
        }

        if (!hasError && obj[QStringLiteral("body_text")].toString().length() > Validation::QuestionBodyMaxChars) {
            errorCount++;
            hasError = true;
        }

        if (!hasError) {
            QJsonArray options = obj[QStringLiteral("answer_options")].toArray();
            if (options.size() < Validation::AnswerOptionMinCount ||
                options.size() > Validation::AnswerOptionMaxCount) {
                errorCount++;
                hasError = true;
            }

            if (!hasError) {
                for (const auto& opt : options) {
                    if (opt.toString().length() > Validation::AnswerOptionMaxChars) {
                        errorCount++;
                        hasError = true;
                        break;
                    }
                }
            }

            if (!hasError) {
                int correctIdx = obj[QStringLiteral("correct_answer_index")].toInt(-1);
                if (correctIdx < 0 || correctIdx >= options.size()) {
                    errorCount++;
                    hasError = true;
                }
            }
        }

        if (!hasError) {
            int difficulty = obj[QStringLiteral("difficulty")].toInt(0);
            if (!Validation::isDifficultyValid(difficulty)) {
                errorCount++;
                hasError = true;
            }
        }

        if (!hasError) {
            double discrimination = obj[QStringLiteral("discrimination")].toDouble(-1.0);
            if (!Validation::isDiscriminationValid(discrimination)) {
                errorCount++;
                hasError = true;
            }
        }

        // Check external_id uniqueness
        if (!hasError && obj.contains(QStringLiteral("external_id"))) {
            QString extId = obj[QStringLiteral("external_id")].toString();
            if (!extId.isEmpty()) {
                auto existsResult = m_questionRepo.externalIdExists(extId);
                if (existsResult.isOk() && existsResult.value()) {
                    errorCount++;
                    hasError = true;
                }
            }
        }

        if (!hasError)
            validCount++;

        recordsProcessed++;

        // Checkpoint every N records
        if (recordsProcessed % CheckpointBatchSize == 0)
            saveProgress(jobId, JobPhase::Validate, file.pos(), recordsProcessed);

        // Abort if error rate exceeds threshold
        if (recordsProcessed > 0 &&
            static_cast<double>(errorCount) / recordsProcessed > MaxErrorRate &&
            recordsProcessed >= CheckpointBatchSize) {
            return Result<void>::err(ErrorCode::ValidationFailed,
                                      QStringLiteral("Error rate %1%% exceeds maximum %2%% at line %3")
                                          .arg((static_cast<double>(errorCount) / recordsProcessed) * 100.0,
                                               0, 'f', 1)
                                          .arg(MaxErrorRate * 100.0, 0, 'f', 0)
                                          .arg(lineNumber));
        }
    }

    // Final checkpoint
    saveProgress(jobId, JobPhase::Validate, file.pos(), recordsProcessed);

    Logger::instance().info(QStringLiteral("IngestionService"),
                             QStringLiteral("Question validation complete"),
                             {{QStringLiteral("job_id"), jobId},
                              {QStringLiteral("valid"), QString::number(validCount)},
                              {QStringLiteral("errors"), QString::number(errorCount)},
                              {QStringLiteral("total"), QString::number(recordsProcessed)}});

    if (recordsProcessed == 0)
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("File contains no records"));

    if (static_cast<double>(errorCount) / recordsProcessed > MaxErrorRate)
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("Too many validation errors: %1 of %2 records")
                                      .arg(errorCount).arg(recordsProcessed));

    return Result<void>::ok();
}

// ── Question file import (JSONL) ────────────────────────────────────────────────

Result<void> IngestionService::importQuestionFile(
    const QString& filePath,
    const std::optional<JobCheckpoint>& checkpoint,
    const QString& jobId,
    const QString& actorUserId)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return Result<void>::err(ErrorCode::IoError,
                                  QStringLiteral("Cannot open file: %1").arg(filePath));

    int recordsProcessed = 0;
    qint64 startOffset = 0;
    if (checkpoint.has_value()) {
        startOffset = checkpoint->offsetBytes;
        recordsProcessed = checkpoint->recordsProcessed;
        file.seek(startOffset);
    }

    int importedCount = 0;
    int errorCount = 0;
    int lineNumber = recordsProcessed;
    QTextStream stream(&file);

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        lineNumber++;

        if (line.isEmpty())
            continue;

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            errorCount++;
            recordsProcessed++;
            continue;
        }

        QJsonObject obj = doc.object();

        // Build Question struct
        Question q;
        q.id                = QUuid::createUuid().toString(QUuid::WithoutBraces);
        q.bodyText          = obj[QStringLiteral("body_text")].toString();
        q.correctAnswerIndex = obj[QStringLiteral("correct_answer_index")].toInt();
        q.difficulty        = obj[QStringLiteral("difficulty")].toInt();
        q.discrimination    = obj[QStringLiteral("discrimination")].toDouble();
        q.status            = QuestionStatus::Draft;
        q.createdAt         = QDateTime::currentDateTimeUtc();
        q.updatedAt         = q.createdAt;
        q.createdByUserId   = actorUserId;
        q.updatedByUserId   = actorUserId;

        if (obj.contains(QStringLiteral("external_id")))
            q.externalId = obj[QStringLiteral("external_id")].toString();

        // Parse answer options
        QJsonArray optionsArr = obj[QStringLiteral("answer_options")].toArray();
        for (const auto& opt : optionsArr)
            q.answerOptions.append(opt.toString());

        // Validate (skip invalid rows — already counted in validation phase)
        if (q.bodyText.isEmpty() || q.answerOptions.size() < Validation::AnswerOptionMinCount ||
            !Validation::isDifficultyValid(q.difficulty) ||
            !Validation::isDiscriminationValid(q.discrimination)) {
            errorCount++;
            recordsProcessed++;
            continue;
        }

        // Insert question
        auto insertResult = m_questionRepo.insertQuestion(q);
        if (insertResult.isErr()) {
            errorCount++;
            Logger::instance().warn(QStringLiteral("IngestionService"),
                                     QStringLiteral("Failed to import question at line %1: %2")
                                         .arg(lineNumber).arg(insertResult.errorMessage()),
                                     {{QStringLiteral("job_id"), jobId}});
            recordsProcessed++;
            continue;
        }

        const auto& inserted = insertResult.value();

        // Resolve/create knowledge point by path
        if (obj.contains(QStringLiteral("knowledge_point_path"))) {
            QString kpPath = obj[QStringLiteral("knowledge_point_path")].toString();
            if (!kpPath.isEmpty()) {
                resolveAndMapKP(inserted.id, kpPath, actorUserId);
            }
        }

        // Resolve/create tags
        if (obj.contains(QStringLiteral("tags"))) {
            QJsonArray tagsArr = obj[QStringLiteral("tags")].toArray();
            for (const auto& tagVal : tagsArr) {
                QString tagName = tagVal.toString().trimmed();
                if (tagName.isEmpty())
                    continue;
                resolveAndMapTag(inserted.id, tagName, actorUserId);
            }
        }

        importedCount++;
        recordsProcessed++;

        if (recordsProcessed % CheckpointBatchSize == 0)
            saveProgress(jobId, JobPhase::Import, file.pos(), recordsProcessed);
    }

    saveProgress(jobId, JobPhase::Import, file.pos(), recordsProcessed);

    Logger::instance().info(QStringLiteral("IngestionService"),
                             QStringLiteral("Question import complete"),
                             {{QStringLiteral("job_id"), jobId},
                              {QStringLiteral("imported"), QString::number(importedCount)},
                              {QStringLiteral("errors"), QString::number(errorCount)}});

    return Result<void>::ok();
}

// ── Roster file validation (CSV) ────────────────────────────────────────────────

Result<void> IngestionService::validateRosterFile(
    const QString& filePath,
    const std::optional<JobCheckpoint>& checkpoint,
    const QString& jobId)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return Result<void>::err(ErrorCode::IoError,
                                  QStringLiteral("Cannot open file: %1").arg(filePath));

    QTextStream stream(&file);

    // Parse header line
    if (stream.atEnd())
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("CSV file is empty"));

    QString headerLine = stream.readLine();
    QStringList headers = headerLine.split(QLatin1Char(','));
    for (auto& h : headers)
        h = h.trimmed().toLower();

    // Required columns
    QStringList required = {
        QStringLiteral("member_id"), QStringLiteral("name"),
        QStringLiteral("barcode"), QStringLiteral("mobile"),
        QStringLiteral("term_start"), QStringLiteral("term_end")
    };

    for (const auto& req : required) {
        if (!headers.contains(req))
            return Result<void>::err(ErrorCode::ValidationFailed,
                                      QStringLiteral("Missing required column: %1").arg(req));
    }

    // Column indices
    int colMemberId  = headers.indexOf(QStringLiteral("member_id"));
    int colName      = headers.indexOf(QStringLiteral("name"));
    int colMobile    = headers.indexOf(QStringLiteral("mobile"));
    int colTermStart = headers.indexOf(QStringLiteral("term_start"));
    int colTermEnd   = headers.indexOf(QStringLiteral("term_end"));
    int colPunchBalance = headers.indexOf(QStringLiteral("punch_balance"));

    // Resume from checkpoint
    int recordsProcessed = 0;
    if (checkpoint.has_value()) {
        file.seek(checkpoint->offsetBytes);
        recordsProcessed = checkpoint->recordsProcessed;
    }

    int validCount = 0;
    int errorCount = 0;
    int lineNumber = recordsProcessed + 1; // +1 for header

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        lineNumber++;

        if (line.isEmpty())
            continue;

        QStringList fields = line.split(QLatin1Char(','));

        bool hasError = false;

        // Check field count
        if (fields.size() < required.size()) {
            errorCount++;
            hasError = true;
        }

        // Validate member_id
        if (!hasError && fields.value(colMemberId).trimmed().isEmpty()) {
            errorCount++;
            hasError = true;
        }

        // Validate name
        if (!hasError && fields.value(colName).trimmed().isEmpty()) {
            errorCount++;
            hasError = true;
        }

        // Validate mobile format (after normalization)
        if (!hasError) {
            QString mobile = CheckInService::normalizeMobile(fields.value(colMobile).trimmed());
            if (mobile.isEmpty()) {
                errorCount++;
                hasError = true;
            }
        }

        // Validate date ranges
        if (!hasError) {
            QDate termStart = QDate::fromString(fields.value(colTermStart).trimmed(), Qt::ISODate);
            QDate termEnd   = QDate::fromString(fields.value(colTermEnd).trimmed(), Qt::ISODate);
            if (!termStart.isValid() || !termEnd.isValid() || termEnd <= termStart) {
                errorCount++;
                hasError = true;
            }
        }

        // Validate punch balance if present
        if (!hasError && colPunchBalance >= 0 && colPunchBalance < fields.size()) {
            QString balStr = fields.value(colPunchBalance).trimmed();
            if (!balStr.isEmpty()) {
                bool ok = false;
                int balance = balStr.toInt(&ok);
                if (!ok || balance < 0) {
                    errorCount++;
                    hasError = true;
                }
            }
        }

        if (!hasError)
            validCount++;

        recordsProcessed++;

        if (recordsProcessed % CheckpointBatchSize == 0)
            saveProgress(jobId, JobPhase::Validate, file.pos(), recordsProcessed);

        // Abort if error rate too high
        if (recordsProcessed >= CheckpointBatchSize &&
            static_cast<double>(errorCount) / recordsProcessed > MaxErrorRate) {
            return Result<void>::err(ErrorCode::ValidationFailed,
                                      QStringLiteral("Error rate %1%% exceeds maximum %2%% at line %3")
                                          .arg((static_cast<double>(errorCount) / recordsProcessed) * 100.0,
                                               0, 'f', 1)
                                          .arg(MaxErrorRate * 100.0, 0, 'f', 0)
                                          .arg(lineNumber));
        }
    }

    saveProgress(jobId, JobPhase::Validate, file.pos(), recordsProcessed);

    Logger::instance().info(QStringLiteral("IngestionService"),
                             QStringLiteral("Roster validation complete"),
                             {{QStringLiteral("job_id"), jobId},
                              {QStringLiteral("valid"), QString::number(validCount)},
                              {QStringLiteral("errors"), QString::number(errorCount)},
                              {QStringLiteral("total"), QString::number(recordsProcessed)}});

    if (recordsProcessed == 0)
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("CSV file contains no data rows"));

    if (static_cast<double>(errorCount) / recordsProcessed > MaxErrorRate)
        return Result<void>::err(ErrorCode::ValidationFailed,
                                  QStringLiteral("Too many validation errors: %1 of %2 records")
                                      .arg(errorCount).arg(recordsProcessed));

    return Result<void>::ok();
}

// ── Roster file import (CSV) ────────────────────────────────────────────────────

Result<void> IngestionService::importRosterFile(
    const QString& filePath,
    const std::optional<JobCheckpoint>& checkpoint,
    const QString& jobId,
    const QString& actorUserId)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return Result<void>::err(ErrorCode::IoError,
                                  QStringLiteral("Cannot open file: %1").arg(filePath));

    QTextStream stream(&file);

    // Parse header
    QString headerLine = stream.readLine();
    QStringList headers = headerLine.split(QLatin1Char(','));
    for (auto& h : headers)
        h = h.trimmed().toLower();

    int colMemberId     = headers.indexOf(QStringLiteral("member_id"));
    int colName         = headers.indexOf(QStringLiteral("name"));
    int colBarcode      = headers.indexOf(QStringLiteral("barcode"));
    int colMobile       = headers.indexOf(QStringLiteral("mobile"));
    int colTermStart    = headers.indexOf(QStringLiteral("term_start"));
    int colTermEnd      = headers.indexOf(QStringLiteral("term_end"));
    int colProductCode  = headers.indexOf(QStringLiteral("product_code"));
    int colPunchBalance = headers.indexOf(QStringLiteral("punch_balance"));

    // Resume from checkpoint
    int recordsProcessed = 0;
    if (checkpoint.has_value()) {
        file.seek(checkpoint->offsetBytes);
        recordsProcessed = checkpoint->recordsProcessed;
    }

    int importedCount = 0;
    int errorCount = 0;
    int lineNumber = recordsProcessed + 1;

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        lineNumber++;

        if (line.isEmpty())
            continue;

        QStringList fields = line.split(QLatin1Char(','));

        QString memberId = fields.value(colMemberId).trimmed();
        QString name     = fields.value(colName).trimmed();
        QString barcode  = fields.value(colBarcode).trimmed();
        QString mobile   = CheckInService::normalizeMobile(fields.value(colMobile).trimmed());

        // Skip invalid rows
        if (memberId.isEmpty() || name.isEmpty() || mobile.isEmpty()) {
            errorCount++;
            recordsProcessed++;
            continue;
        }

        QDate termStart = QDate::fromString(fields.value(colTermStart).trimmed(), Qt::ISODate);
        QDate termEnd   = QDate::fromString(fields.value(colTermEnd).trimmed(), Qt::ISODate);
        if (!termStart.isValid() || !termEnd.isValid() || termEnd <= termStart) {
            errorCount++;
            recordsProcessed++;
            continue;
        }

        // Encrypt PII fields
        auto nameEnc = m_cipher.encrypt(name, QByteArrayLiteral("member.name"));
        if (nameEnc.isErr()) {
            errorCount++;
            recordsProcessed++;
            continue;
        }

        auto barcodeEnc = m_cipher.encrypt(barcode, QByteArrayLiteral("member.barcode"));
        if (barcodeEnc.isErr()) {
            errorCount++;
            recordsProcessed++;
            continue;
        }

        auto mobileEnc = m_cipher.encrypt(mobile, QByteArrayLiteral("member.mobile"));
        if (mobileEnc.isErr()) {
            errorCount++;
            recordsProcessed++;
            continue;
        }

        // Check if member already exists (update) or insert new
        auto existingResult = m_memberRepo.findMemberByMemberId(memberId);
        QString memberUuid;

        if (existingResult.isOk()) {
            // Update existing member
            Member existing = existingResult.value();
            existing.nameEncrypted    = QString::fromLatin1(nameEnc.value().toBase64());
            existing.barcodeEncrypted = QString::fromLatin1(barcodeEnc.value().toBase64());
            existing.mobileEncrypted  = QString::fromLatin1(mobileEnc.value().toBase64());
            existing.updatedAt        = QDateTime::currentDateTimeUtc();
            auto updateResult = m_memberRepo.updateMember(existing);
            if (updateResult.isErr()) {
                errorCount++;
                recordsProcessed++;
                continue;
            }
            memberUuid = existing.id;
        } else {
            // Insert new member
            Member member;
            member.id               = QUuid::createUuid().toString(QUuid::WithoutBraces);
            member.memberId         = memberId;
            member.nameEncrypted    = QString::fromLatin1(nameEnc.value().toBase64());
            member.barcodeEncrypted = QString::fromLatin1(barcodeEnc.value().toBase64());
            member.mobileEncrypted  = QString::fromLatin1(mobileEnc.value().toBase64());
            member.deleted          = false;
            member.createdAt        = QDateTime::currentDateTimeUtc();
            member.updatedAt        = member.createdAt;

            auto insertResult = m_memberRepo.insertMember(member);
            if (insertResult.isErr()) {
                errorCount++;
                recordsProcessed++;
                continue;
            }
            memberUuid = member.id;
        }

        // Insert term card
        TermCard card;
        card.id        = QUuid::createUuid().toString(QUuid::WithoutBraces);
        card.memberId  = memberUuid;
        card.termStart = termStart;
        card.termEnd   = termEnd;
        card.active    = true;
        card.createdAt = QDateTime::currentDateTimeUtc();

        auto cardResult = m_memberRepo.insertTermCard(card);
        if (cardResult.isErr()) {
            Logger::instance().warn(QStringLiteral("IngestionService"),
                                     QStringLiteral("Failed to insert term card at line %1")
                                         .arg(lineNumber),
                                     {{QStringLiteral("job_id"), jobId}});
        }

        // Insert punch card if product_code and punch_balance are present
        if (colProductCode >= 0 && colPunchBalance >= 0 &&
            colProductCode < fields.size() && colPunchBalance < fields.size()) {
            QString productCode = fields.value(colProductCode).trimmed();
            QString balStr      = fields.value(colPunchBalance).trimmed();

            if (!productCode.isEmpty() && !balStr.isEmpty()) {
                bool ok = false;
                int balance = balStr.toInt(&ok);
                if (ok && balance > 0) {
                    PunchCard punchCard;
                    punchCard.id             = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    punchCard.memberId       = memberUuid;
                    punchCard.productCode    = productCode;
                    punchCard.initialBalance = balance;
                    punchCard.currentBalance = balance;
                    punchCard.createdAt      = QDateTime::currentDateTimeUtc();
                    punchCard.updatedAt      = punchCard.createdAt;

                    auto punchResult = m_memberRepo.insertPunchCard(punchCard);
                    if (punchResult.isErr()) {
                        Logger::instance().warn(QStringLiteral("IngestionService"),
                                                 QStringLiteral("Failed to insert punch card at line %1")
                                                     .arg(lineNumber),
                                                 {{QStringLiteral("job_id"), jobId}});
                    }
                }
            }
        }

        importedCount++;
        recordsProcessed++;

        if (recordsProcessed % CheckpointBatchSize == 0)
            saveProgress(jobId, JobPhase::Import, file.pos(), recordsProcessed);
    }

    saveProgress(jobId, JobPhase::Import, file.pos(), recordsProcessed);

    Logger::instance().info(QStringLiteral("IngestionService"),
                             QStringLiteral("Roster import complete"),
                             {{QStringLiteral("job_id"), jobId},
                              {QStringLiteral("imported"), QString::number(importedCount)},
                              {QStringLiteral("errors"), QString::number(errorCount)}});

    return Result<void>::ok();
}

// ── KP/Tag resolution helpers ────────────────────────────────────────────────

void IngestionService::resolveAndMapKP(const QString& questionId,
                                         const QString& kpPath,
                                         const QString& actorUserId)
{
    // Walk the slash-delimited path, creating nodes as needed.
    // e.g. "Safety/Electrical/Grounding" → create Safety, then Electrical under it, etc.
    QStringList segments = kpPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.isEmpty())
        return;

    // Try to find the leaf node by looking up the full tree
    auto treeResult = m_kpRepo.getTree();
    if (treeResult.isErr())
        return;

    const auto& tree = treeResult.value();

    // Build a map of path → KP for quick lookup
    QString currentParentId;
    QString resolvedKpId;

    for (int i = 0; i < segments.size(); ++i) {
        const QString& segName = segments[i];
        QString expectedPath;
        for (int j = 0; j <= i; ++j) {
            if (j > 0) expectedPath += QLatin1Char('/');
            expectedPath += segments[j];
        }

        // Find existing node by path
        bool found = false;
        for (const auto& kp : tree) {
            if (kp.path == expectedPath && !kp.deleted) {
                currentParentId = kp.id;
                resolvedKpId = kp.id;
                found = true;
                break;
            }
        }

        if (!found) {
            // Create the missing node
            KnowledgePoint newKp;
            newKp.id       = QUuid::createUuid().toString(QUuid::WithoutBraces);
            newKp.name     = segName;
            newKp.parentId = currentParentId;
            newKp.position = 0;
            newKp.path     = expectedPath;
            newKp.createdAt = QDateTime::currentDateTimeUtc();
            newKp.deleted   = false;

            auto insertResult = m_kpRepo.insertKP(newKp);
            if (insertResult.isErr()) {
                Logger::instance().warn(QStringLiteral("IngestionService"),
                                         QStringLiteral("Failed to create KP node: %1").arg(expectedPath),
                                         {});
                return;
            }
            currentParentId = insertResult.value().id;
            resolvedKpId = insertResult.value().id;
        }
    }

    // Map question to the leaf KP
    if (!resolvedKpId.isEmpty()) {
        QuestionKPMapping mapping;
        mapping.questionId       = questionId;
        mapping.knowledgePointId = resolvedKpId;
        mapping.mappedAt         = QDateTime::currentDateTimeUtc();
        mapping.mappedByUserId   = actorUserId;
        m_questionRepo.insertKPMapping(mapping);
    }
}

void IngestionService::resolveAndMapTag(const QString& questionId,
                                          const QString& tagName,
                                          const QString& actorUserId)
{
    // Find existing tag by name
    auto tagResult = m_questionRepo.findTagByName(tagName);
    QString tagId;

    if (tagResult.isOk()) {
        tagId = tagResult.value().id;
    } else {
        // Create new tag
        Tag tag;
        tag.id        = QUuid::createUuid().toString(QUuid::WithoutBraces);
        tag.name      = tagName;
        tag.createdAt = QDateTime::currentDateTimeUtc();

        auto insertResult = m_questionRepo.insertTag(tag);
        if (insertResult.isErr()) {
            Logger::instance().warn(QStringLiteral("IngestionService"),
                                     QStringLiteral("Failed to create tag: %1").arg(tagName),
                                     {});
            return;
        }
        tagId = insertResult.value().id;
    }

    // Map question to tag
    QuestionTagMapping mapping;
    mapping.questionId      = questionId;
    mapping.tagId           = tagId;
    mapping.appliedAt       = QDateTime::currentDateTimeUtc();
    mapping.appliedByUserId = actorUserId;
    m_questionRepo.insertTagMapping(mapping);
}

// ── Shared helpers ──────────────────────────────────────────────────────────────

void IngestionService::saveProgress(const QString& jobId, JobPhase phase,
                                      qint64 offsetBytes, int recordsProcessed)
{
    JobCheckpoint cp;
    cp.jobId            = jobId;
    cp.phase            = phase;
    cp.offsetBytes      = offsetBytes;
    cp.recordsProcessed = recordsProcessed;
    cp.savedAt          = QDateTime::currentDateTimeUtc();
    m_ingestionRepo.saveCheckpoint(cp);
}
