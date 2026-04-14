# ProctorOps — Internal Interface Contract Document

> **Project type:** Desktop_app — C++20 + Qt 6 Widgets + SQLite
> **Scope:** Internal service contracts, repository contracts, file schemas, package schemas, audit/export structures
> **Note:** This document defines internal C++ service boundaries and data contracts. There are no HTTP endpoints. All interfaces are in-process C++ or SQLite-backed.
> **Revision:** Secondary service contracts implemented (offline sync, data-subject workflows, security administration, update/rollback, ingestion monitoring)

---

## 1. Overview

`docs/api-spec.md` serves as the interface-contract document for ProctorOps. It defines:

- Internal service/module boundaries (C++ interface contracts)
- Repository contracts (SQLite data access layer)
- Import file schemas (question and roster ingestion)
- Sync package schema (`.proctorsync` bundle format)
- Update package metadata schema (`.proctorpkg` bundle format)
- Audit export structure
- Export and deletion request structures
- Internal command and event types

---

## 2. Common Types and Enumerations

### 2.1 Result Type

All service operations return a `Result<T>` sum type:

```
Result<T>
  ├── Ok(T value)
  └── Err(ErrorCode code, QString message)

ErrorCode (enum class):  // canonical source: src/utils/Result.h
  // General
  NotFound, AlreadyExists, ValidationFailed, InternalError, DbError,
  // Auth
  AuthorizationDenied, StepUpRequired, AccountLocked, CaptchaRequired, InvalidCredentials,
  // Check-in
  DuplicateCheckIn, TermCardExpired, TermCardMissing, AccountFrozen, PunchCardExhausted,
  // Crypto / packaging
  SignatureInvalid, TrustStoreMiss, PackageCorrupt,
  EncryptionFailed, DecryptionFailed, KeyNotFound,
  // Sync
  ConflictUnresolved,
  // Ingestion
  JobDependencyUnmet, CheckpointCorrupt, IoError,
  // State machine violations (export/deletion/update workflows)
  InvalidState,
  // Audit
  ChainIntegrityFailed
```

### 2.2 Roles (RBAC)

```
Role (enum class):
  FRONT_DESK_OPERATOR    // Check-in, view own session data
  PROCTOR                // Check-in, read question bank, view audit
  CONTENT_MANAGER        // Full question governance, ingestion management
  SECURITY_ADMINISTRATOR // All of the above + user management, step-up actions, key management
```

### 2.3 Event Types (Audit)

```
AuditEventType (enum class):
  LOGIN, LOGIN_FAILED, LOGIN_LOCKED, CAPTCHA_CHALLENGE, CAPTCHA_SOLVED,
  LOGOUT, CONSOLE_LOCKED, CONSOLE_UNLOCKED,
  STEP_UP_INITIATED, STEP_UP_PASSED, STEP_UP_FAILED,
  CHECKIN_ATTEMPT, CHECKIN_SUCCESS, CHECKIN_DUPLICATE_BLOCKED,
  CHECKIN_FROZEN_BLOCKED, CHECKIN_EXPIRED_BLOCKED,
  DEDUCTION_CREATED, DEDUCTION_REVERSED,
  CORRECTION_REQUESTED, CORRECTION_APPROVED, CORRECTION_REJECTED, CORRECTION_APPLIED,
  QUESTION_CREATED, QUESTION_UPDATED, QUESTION_DELETED,
  KP_CREATED, KP_UPDATED, KP_DELETED, KP_MAPPED, KP_UNMAPPED,
  TAG_CREATED, TAG_APPLIED, TAG_REMOVED,
  JOB_CREATED, JOB_STARTED, JOB_COMPLETED, JOB_FAILED, JOB_INTERRUPTED,
  SYNC_EXPORT, SYNC_IMPORT, SYNC_CONFLICT_RESOLVED,
  UPDATE_IMPORTED, UPDATE_STAGED, UPDATE_APPLIED, UPDATE_ROLLED_BACK,
  USER_CREATED, USER_UPDATED, USER_DEACTIVATED, ROLE_CHANGED,
  KEY_IMPORTED, KEY_ROTATED, KEY_REVOKED,
  EXPORT_REQUESTED, EXPORT_COMPLETED, DELETION_REQUESTED, DELETION_COMPLETED,
  CHAIN_VERIFIED, AUDIT_EXPORT
```

---

## 3. Service Contracts

### 3.1 AuthService (implemented)

Owns: user authentication, password policy, lockout, CAPTCHA, RBAC enforcement, step-up verification, session management, console lock/unlock.

**Constructor:** `AuthService(IUserRepository&, IAuditRepository&)`

```
// Authentication — full sign-in flow
Result<UserSession> signIn(QString username, QString password, QString captchaAnswer = {})
    // Flow: find user → check deactivated → check lockout window (reset if expired)
  //   → check locked → check CAPTCHA (with cool-down, fail-closed if state missing)
  //   → verify Argon2id password
    //   → on failure: increment lockout, lock if >=5, generate CAPTCHA if >=3
    //   → on success: clear lockout/CAPTCHA, create session, audit Login
    // Error: InvalidCredentials, AccountLocked, CaptchaRequired, AuthorizationDenied

Result<void> signOut(QString sessionToken)
    // Deactivates session, records Logout audit event.

// Step-up verification
Result<StepUpWindow> initiateStepUp(QString sessionToken, QString password)
    // Re-verifies password via Argon2id. Creates 2-minute single-use window.
    // Audit: StepUpInitiated on success, StepUpFailed on wrong password.
    // Error: InvalidCredentials, NotFound

Result<void> consumeStepUp(QString stepUpId)
    // Marks window as consumed. Fails if expired or already consumed.
    // Error: StepUpRequired

// Console lock (Ctrl+L)
Result<void>        lockConsole(QString sessionToken)
Result<UserSession> unlockConsole(QString sessionToken, QString password)
    // Password re-entry required to unlock. Audit: ConsoleLocked/ConsoleUnlocked.

// RBAC
static bool  hasPermission(Role userRole, Role requiredRole)
    // Uses >= comparison: SecurityAdministrator(3) >= all.
Result<void> requireRole(QString sessionToken, Role requiredRole)
    // Error: AuthorizationDenied
Result<void> requireRoleForActor(QString actorUserId, Role requiredRole)
  // Actor-based role guard used by service-layer RBAC checks.

// Security administration user lifecycle (step-up required)
Result<User> createUser(QString actorUserId,
                        QString username,
                        QString password,
                        Role role,
                        QString stepUpWindowId)
    // Atomically provisions local user + Argon2id credential.

Result<void> resetUserPassword(QString actorUserId,
                               QString targetUserId,
                               QString newPassword,
                               QString stepUpWindowId)
    // Replaces credential hash, records PASSWORD_RESET audit event.

// First-run bootstrap provisioning
Result<UserSession> bootstrapSecurityAdministrator(QString username, QString password)
  // Creates first SecurityAdministrator only when none exists, then signs in.

// Step-up requirement check
Result<void> requireStepUp(QString sessionToken)
    // Returns StepUpRequired if no valid unconsumed window exists.

// CAPTCHA generation
Result<CaptchaState> generateCaptcha(QString username)
    // Creates challenge via CaptchaGenerator, stores SHA-256 hash of answer.
```

**Lockout decision tree:**
1. First failure in window older than 10 minutes → reset counter to 0
2. failedAttempts >= 3 → CAPTCHA required (unless cool-down expired at 15 min)
3. failedAttempts >= 5 → account locked, user status set to Locked
4. Successful login → clear lockout record, clear CAPTCHA state, restore Active status

**Masked-view rules:** All PII fields (mobile, barcode, name) are masked by default using `MaskingPolicy`. Full reveal requires a valid unconsumed step-up window. `MaskingPolicy::requiresStepUp()` always returns `true`.

### 3.2 CheckInService (implemented)

Owns: entry validation, term-card date checks, frozen/expired blocking, atomic punch-card deduction, 30-second duplicate suppression, correction workflow (request → approve → apply/reject).

```
Interface: ICheckInService

Result<CheckInResult> checkIn(MemberIdentifier identifier,
                               QString sessionId,
                               QString operatorUserId,
                               QString punchCardId = {})
               // MemberIdentifier is one of: barcode, member ID, normalized mobile.
               // Validates term card, checks frozen/expired status.
               // Enforces 30-second duplicate window, then re-checks under write lock
               // inside BEGIN IMMEDIATE transaction to reduce duplicate races.
               // Atomically deducts one session from the applicable punch card.
               // Writes DEDUCTION_CREATED + CHECKIN_SUCCESS audit events.
               // Error: DUPLICATE_CHECKIN, TERM_CARD_EXPIRED, ACCOUNT_FROZEN,
               //        ACCOUNT_EXPIRED, PUNCH_CARD_EXHAUSTED

CheckInResult:
  member_id, member_name_masked, session_id,
  deduction_event_id, remaining_balance, checkin_timestamp

Result<CorrectionRequest> requestCorrection(QString deductionEventId,
                                            QString rationale,
                                            QString requestedByUserId)
               // Creates a PENDING correction_request record.
               // Caller identity is audit-logged on the request.

Result<void>  approveCorrection(QString correctionRequestId,
                                 QString rationale,
                                 QString approvedByUserId,
                                 QString stepUpWindowId)
               // Requires SECURITY_ADMINISTRATOR + valid unconsumed step-up window.
               // Service entrypoint enforces RBAC, actor ownership, expiry, and consumption.
               // Status: PENDING → APPROVED

Result<void>  applyCorrection(QString correctionRequestId,
                               QString actorUserId,
                               QString stepUpWindowId)
               // Requires SECURITY_ADMINISTRATOR + valid unconsumed step-up window.
               // Status: APPROVED → APPLIED

Result<void>  rejectCorrection(QString correctionRequestId,
                                QString rationale,
                                QString rejectedByUserId,
                                QString stepUpWindowId)
               // Requires SECURITY_ADMINISTRATOR + valid unconsumed step-up window.
```

### 3.3 QuestionService (implemented)

Owns: question CRUD with validation, knowledge-point tree management with materialized path propagation, tag management, difficulty (1-5) / discrimination (0.00-1.00) validation, combined query builder (KP subtree, tags, difficulty/discrimination range, text search, pagination).

Mutating operations enforce `ContentManager` or higher via `AuthService::requireRoleForActor(...)`.

```
Interface: IQuestionService

// Question management
Result<Question>       createQuestion(Question question,
                                       QString actorUserId)
Result<Question>       updateQuestion(Question question,
                                       QString actorUserId)
Result<void>           deleteQuestion(QString questionId,
                                       QString actorUserId)
Result<Question>       getQuestion(QString questionId)

// Query builder
Result<QList<Question>> queryQuestions(QuestionFilter filter)
QuestionFilter fields:
  chapter_id?, difficulty_min?, difficulty_max?, discrimination_min?,
  discrimination_max?, tags[], knowledge_point_ids[], text_search?,
  limit?, offset?

// KnowledgePoint tree
Result<KnowledgePoint> createKnowledgePoint(QString sessionToken, CreateKPCmd)
Result<KnowledgePoint> updateKnowledgePoint(QString sessionToken, UpdateKPCmd)
Result<void>           deleteKnowledgePoint(QString sessionToken, QString kpId)
Result<QList<KnowledgePoint>> getKPTree(QString sessionToken)
                        // Returns the full chapter tree ordered by position.

Result<void>           mapQuestionToKP(QString sessionToken,
                                        QString questionId, QString kpId)
Result<void>           unmapQuestionFromKP(QString sessionToken,
                                            QString questionId, QString kpId)

// Tags
Result<Tag>            createTag(QString sessionToken, QString name)
Result<void>           applyTag(QString sessionToken,
                                 QString questionId, QString tagId)
Result<void>           removeTag(QString sessionToken,
                                  QString questionId, QString tagId)

CreateQuestionCmd:
  body_text, answer_options[], correct_answer_index,
  difficulty(1–5), discrimination(0.00–1.00),
  knowledge_point_ids[], tag_ids[]

UpdateQuestionCmd:
  question_id, + any subset of CreateQuestionCmd fields
```

### 3.4 IngestionService (implemented)

Owns: import job creation/cancellation, three-phase execution (VALIDATE → IMPORT → INDEX) with checkpoint-based resume, JSONL question import (validation, KP path resolution, tag resolution), CSV roster import (PII encryption, member upsert, term/punch card creation), error rate abort threshold (50%).

Job creation and cancellation enforce `ContentManager` or higher via `AuthService::requireRoleForActor(...)`.

```
Interface: IIngestionService

Result<IngestionJob>   createJob(QString sessionToken, CreateJobCmd)
CreateJobCmd:
  job_type (QUESTION_IMPORT | ROSTER_IMPORT),
  source_file_path, priority(1–10), dependency_job_ids[],
  scheduled_at? (null = run immediately when worker available)

Result<void>           cancelJob(QString sessionToken, QString jobId)
                        // Only PENDING jobs can be cancelled.

Result<IngestionJob>   getJob(QString sessionToken, QString jobId)
Result<QList<IngestionJob>> listJobs(QString sessionToken, JobFilter filter)

// Internal — called by WorkerPool; not exposed to UI layer directly
Result<void>  executeValidatePhase(QString jobId)
Result<void>  executeImportPhase(QString jobId)
Result<void>  executeIndexPhase(QString jobId)

// Checkpoint
Result<JobCheckpointState> loadCheckpoint(QString jobId, QString phase)
Result<void>               saveCheckpoint(QString jobId, QString phase,
                                           qint64 offsetBytes)

IngestionJob:
  id, type, status, priority, source_file_path,
  dependency_job_ids[], created_at, started_at?, completed_at?, failed_at?,
  retry_count, last_error?, current_phase, checkpoint_offset
```

### 3.5 SyncService

Owns: sync package export, import, Ed25519 signature verification, conflict detection, trust-store management.

```
Interface: ISyncService

// Export
Result<SyncPackage>    exportPackage(QString destinationDir,
                                      QString deskId,
                                      QString signerKeyId,
                                      QByteArray signerPrivKeyDer,
                                      QString actorUserId,
                                      QString stepUpWindowId)
                        // Requires SecurityAdministrator + step-up.
                        // Collects entity delta since last desk watermark,
                        // writes manifest.json + entity JSONL files, signs manifest body.

// Import
Result<SyncPackage>    importPackage(QString packageDir,
                                      QString actorUserId)
                        // Requires SecurityAdministrator.
                        // Verifies signature, validates digests, classifies conflicts.
                        // Auto-merges append-only entities.
                        // Returns imported package record with status Applied or Partial.

Result<QList<SyncPackage>>    listPackages(QString actorUserId)
Result<QList<ConflictRecord>> listPendingConflicts(QString packageId,
                                                    QString actorUserId)

// Conflict resolution (interactive — called by ConflictResolutionDialog)
Result<void>           resolveConflict(QString conflictId,
                                        ConflictStatus resolution,
                                        QString actorUserId,
                                        QString stepUpWindowId)
                        // Persists resolution_action_type / resolution_action_id linkage.
                        // AcceptIncoming for DoubleDeduction performs compensating local
                        // correction and applies incoming deduction.

// Trust store management (security admin only)
Result<TrustedSigningKey> importSigningKey(QString label,
                                           QString publicKeyDerHex,
                                           QDateTime expiresAt,
                                           QString actorUserId,
                                           QString stepUpWindowId)
Result<void>           revokeSigningKey(QString keyId,
                                         QString actorUserId,
                                         QString stepUpWindowId)
Result<QList<TrustedSigningKey>> listSigningKeys(QString actorUserId)

SyncImportReport:
  package_id, source_desk_id, entity_count,
  applied_count, conflict_count, skipped_count,
  conflicts[]: { conflict_id, entity_type, entity_id, description }
```

### 3.6 AuditService (implemented)

Owns: writing audit events, computing hash chain, PII encryption in payloads, chain verification.

**Constructor:** `AuditService(IAuditRepository&, AesGcmCipher&)`

```
// Writing (called by all services — never by UI directly)
Result<void> recordEvent(QString actorUserId, AuditEventType eventType,
                          QString entityType, QString entityId,
                          QJsonObject beforePayload = {}, QJsonObject afterPayload = {})
    // 1. Gets chain head hash from IAuditRepository
  // 2. Encrypts PII fields in payloads (member_id/memberId, mobile, barcode, name,
  //    and *Encrypted variants)
    // 3. Computes entry hash via HashChain::computeEntryHash (pipe-delimited canonical form)
    // 4. Inserts entry atomically (repo transaction updates audit_entries + audit_chain_head)
    // On failure: logs critical error; returns Result::err

// Reading
Result<QList<AuditEntry>> queryEvents(AuditFilter filter)
    // Delegates to IAuditRepository::queryEntries

// Chain integrity verification — requires SecurityAdministrator role
Result<ChainVerifyReport> verifyChain(const QString& actorUserId,
                                       std::optional<int> limit = {})
    // Authorization: SecurityAdministrator role required (when AuthService is wired).
    // Reads all entries in chronological order, recomputes each hash, verifies:
    //   - previousEntryHash matches prior entry's entryHash
    //   - recomputed entryHash matches stored entryHash
    // Returns ChainVerifyReport with integrityOk, entriesVerified, firstBrokenEntryId

ChainVerifyReport:
  entriesVerified, firstEntryId, lastEntryId,
  integrityOk, firstBrokenEntryId (empty if ok)
```

**PII encryption:** Before storage, `encryptPayload()` scans JSON for known PII field names (`member_id`, `memberId`, `mobile`, `barcode`, `name`, `memberIdEncrypted`, `mobileEncrypted`, `barcodeEncrypted`, `nameEncrypted`). Each value is AES-256-GCM encrypted and base64-encoded. Non-PII fields are preserved as-is.

### 3.6a PackageVerifier (implemented)

Owns: unified signature and digest verification for sync and update packages.

**Constructor:** `PackageVerifier(ISyncRepository&)`

```
Result<bool> verifyPackageSignature(QByteArray manifestData, QByteArray signature,
                                     QString signerKeyId)
    // 1. Looks up key in trusted_signing_keys via ISyncRepository
    // 2. Checks revocation → TrustStoreMiss if revoked
    // 3. Checks expiry → TrustStoreMiss if expired
    // 4. Decodes DER public key from hex
    // 5. Delegates to Ed25519Verifier::verify

Result<bool> verifyFileDigest(QString filePath, QString expectedSha256Hex)
    // Reads file, computes SHA-256 via HashChain::computeSha256, compares

Result<void> verifyAllEntities(QString packageDir, QList<SyncPackageEntity> entities)
    // Iterates all entities, verifies each file digest
    // Error: PackageCorrupt on any mismatch
```

### 3.7 UpdateService

Owns: update package import, signature verification, staging, apply, rollback.

```
Interface: IUpdateService

Result<UpdatePackage> importPackage(QString packageDir,
                                     QString actorUserId)
                // Reads .proctorpkg, verifies Ed25519 signature, validates component digests.
                // Stages to temp location. Returns metadata for operator review.
                // Error: SIGNATURE_INVALID, TRUST_STORE_MISS, PACKAGE_CORRUPT

Result<void>   applyPackage(QString packageId,
                             QString currentVersion,
                             QString actorUserId,
                             QString stepUpWindowId)
                // Snapshots current install state, deploys package components to live runtime,
                // verifies deployed digests, records install history.
                // Writes UPDATE_APPLIED audit event.

Result<RollbackRecord> rollback(QString installHistoryId,
                                 QString rationale,
                                 QString actorUserId,
                                 QString stepUpWindowId)
                // Reinstates prior component set from install-history snapshot backup paths.
                // Writes rollback_records and audit event.

Result<void>          cancelPackage(QString packageId,
                                     QString actorUserId)
                // Requires SecurityAdministrator. Only Staged packages can be cancelled.

Result<QList<UpdatePackage>>       listPackages(QString actorUserId)
Result<QList<InstallHistoryEntry>> listInstallHistory(QString actorUserId)
Result<QList<RollbackRecord>>      listRollbackRecords(QString actorUserId)

UpdatePackageMetadata:
  package_id, version, target_platform, description,
  components[]: { name, version, digest_sha256 },
  signed_by_key_id, signature_valid, staged_path
```

### 3.8 DataSubjectService

Owns: GDPR/MLPS data export and deletion request workflows (Art.15 / Art.17).

```
Interface: IDataSubjectService

Result<ExportRequest>  createExportRequest(QString memberId,
                                             QString rationale,
                                             QString actorUserId)
                // Creates a PENDING export request record.
                // Requires rationale (non-empty) and active actor identity.

Result<ExportRequest>  fulfillExportRequest(QString requestId,
                                             QString outputPath,
                                             QString actorUserId,
                                             QString stepUpWindowId)
                // Transitions PENDING → COMPLETED. Writes watermarked JSON export file.
                // PII masked in export (MaskingPolicy applied). Writes EXPORT_COMPLETED audit.
                // Error: InvalidState if not PENDING.

Result<void>           rejectExportRequest(QString requestId,
                                             QString actorUserId,
                                             QString stepUpWindowId)
                // Requires SecurityAdministrator + step-up.
                // Transitions PENDING → REJECTED. Error: InvalidState if not PENDING.

Result<QList<ExportRequest>> listExportRequests(QString actorUserId,
                                                 QString statusFilter = {})
                // Requires SecurityAdministrator.

Result<DeletionRequest> createDeletionRequest(QString memberId,
                                                QString rationale,
                                                QString actorUserId)
                // Creates a PENDING deletion request. Requires rationale.

Result<DeletionRequest> approveDeletionRequest(QString requestId,
                                                QString actorUserId,
                                                QString stepUpWindowId)
                // Transitions PENDING → APPROVED. Error: InvalidState if not PENDING.

Result<void>            completeDeletion(QString requestId,
                                            QString actorUserId,
                                            QString stepUpWindowId)
                // Transitions APPROVED → COMPLETED. Anonymizes PII fields in MemberRepository.
                // Retains audit tombstones. Writes DELETION_COMPLETED audit event.
                // Error: InvalidState if not APPROVED.

Result<void>            rejectDeletionRequest(QString requestId,
                                               QString actorUserId,
                                               QString stepUpWindowId)
                // Requires SecurityAdministrator + step-up.
                // Transitions PENDING → REJECTED. Error: InvalidState if not PENDING.

Result<QList<DeletionRequest>> listDeletionRequests(QString actorUserId,
                                                     QString statusFilter = {})
                // Requires SecurityAdministrator.

ExportRequest:   id, member_id, status(PENDING|COMPLETED|REJECTED), actor_user_id, created_at, rationale
DeletionRequest: id, member_id, status(PENDING|APPROVED|COMPLETED|REJECTED), actor_user_id, created_at, rationale
```

### 3.9 JobScheduler (implemented)

Owns: priority-aware job scheduling, dependency resolution, retry backoff (5s/30s/2min, max 5 retries), starvation-safe fairness (3× avg completion time boost), crash recovery, concurrent dispatch (2 workers).

**Constructor:** `JobScheduler(IIngestionRepository&, IngestionService&, AuditService&)`

```
void start()       // Recover from crash, begin scheduling loop via QTimer
void stop()        // Gracefully stop, release claims, stop timer

Result<IngestionJob> scheduleJob(JobType type, QString sourceFilePath,
                                  int priority, QString actorUserId,
                                  QDateTime scheduledAt = {},
                                  QStringList dependsOnJobIds = {})
    // Delegates to IngestionService::createJob, triggers immediate tick

int activeWorkerCount() const   // Current concurrent workers

// Internal tick() logic:
// 1. Get ready jobs (pending, deps met, priority DESC, created_at ASC)
// 2. Skip if retryCount >= 5 → permanent failure
// 3. Skip if failedAt + backoff > now → not yet retriable
// 4. Skip if scheduledAt > now → deferred
// 5. Claim → dispatch via QtConcurrent::run on thread pool (max 2)
// 6. On completion: release claim, audit, record completion time
// 7. On failure: release claim, increment retry, audit

// Crash recovery:
// findInProgressJobIds → markInterrupted → releaseAllClaims
// Re-enqueue eligible (retryCount < 5) by resetting to Pending
```

---

## 4. Repository Contracts

Repositories handle SQLite access only. They accept and return domain structs. They contain no business logic.

### 4.1 UserRepository

```
Result<User>         insert(UserRecord)
Result<User>         findByUsername(QString username)
Result<User>         findById(QString userId)
Result<void>         updateCredential(QString userId, CredentialRecord)
Result<void>         updateStatus(QString userId, UserStatus)
Result<LockoutRecord> getLockoutRecord(QString userId)
Result<void>          upsertLockoutRecord(LockoutRecord)
Result<void>          upsertCaptchaState(CaptchaState)
Result<CaptchaState>  getCaptchaState(QString username)
```

### 4.2 MemberRepository

```
Result<Member>           insert(MemberRecord)
Result<Member>           findByBarcode(QString barcode)
Result<Member>           findByMemberId(QString memberId)
Result<Member>           findByMobileNormalized(QString mobileNormalized)
Result<QList<TermCard>>  getActiveTermCards(QString memberId)
Result<QList<PunchCard>> getActivePunchCards(QString memberId)
Result<MemberFreezeRecord?> getFreezeRecord(QString memberId)
Result<void>             insertFreezeRecord(MemberFreezeRecord)
Result<void>             clearFreezeRecord(QString memberId)
```

### 4.3 CheckInRepository

```
Result<CheckInAttempt>       insert(CheckInAttempt)
Result<CheckInAttempt?>      findDuplicateWithin30s(QString memberId, QString sessionId)
Result<DeductionEvent>       insertDeduction(DeductionEvent)
Result<void>                 reverseDeduction(QString deductionEventId, QString correctionId)
Result<CorrectionRequest>    insertCorrectionRequest(CorrectionRequest)
Result<CorrectionRequest>    getCorrectionRequest(QString id)
Result<void>                 updateCorrectionStatus(QString id, CorrectionStatus status,
                                                     CorrectionApproval approval)
```

### 4.4 QuestionRepository

```
Result<Question>              insert(Question)
Result<Question>              update(Question)
Result<void>                  softDelete(QString questionId)
Result<Question>              findById(QString questionId)
Result<QList<Question>>       query(QuestionFilter)
Result<void>                  insertKPMapping(QString questionId, QString kpId)
Result<void>                  deleteKPMapping(QString questionId, QString kpId)
Result<void>                  insertTagMapping(QString questionId, QString tagId)
Result<void>                  deleteTagMapping(QString questionId, QString tagId)
```

### 4.5 KnowledgePointRepository

```
Result<KnowledgePoint>        insert(KnowledgePoint)
Result<KnowledgePoint>        update(KnowledgePoint)
Result<void>                  softDelete(QString kpId)
Result<QList<KnowledgePoint>> getTree()    // ordered by parent_id, position
Result<KnowledgePoint>        findById(QString kpId)
```

### 4.6 IngestionRepository

```
Result<IngestionJob>          insert(IngestionJob)
Result<IngestionJob>          update(IngestionJob)
Result<IngestionJob>          findById(QString jobId)
Result<QList<IngestionJob>>   findByStatus(JobStatus)
Result<void>                  claimWorker(QString jobId, QString workerId)
Result<void>                  releaseWorker(QString jobId)
Result<void>                  saveCheckpoint(JobCheckpointState)
Result<JobCheckpointState?>   loadCheckpoint(QString jobId, QString phase)
Result<void>                  markInterrupted(QList<QString> jobIds)   // crash recovery
```

### 4.7 AuditRepository

```
Result<AuditEntry>            insert(AuditEntry)
Result<AuditEntry?>           getChainHead()
Result<QList<AuditEntry>>     query(AuditFilter)
Result<AuditEntry>            findById(QString entryId)
Result<qint64>                countEntriesOlderThan(QDateTime threshold)
Result<void>                  purgeOlderThan(QDateTime threshold)  // only if admin + step-up
```

### 4.8 SyncRepository

```
Result<SyncPackage>           insert(SyncPackage)
Result<SyncPackage>           findById(QString packageId)
Result<ConflictRecord>        insertConflict(ConflictRecord)
Result<void>                  updateConflictResolution(QString conflictId,
                                                        ConflictResolution resolution)
Result<QList<ConflictRecord>> getPendingConflicts(QString packageId)
```

### 4.9 UpdateRepository

```
Result<UpdatePackage>         insert(UpdatePackage)
Result<UpdatePackage>         findById(QString packageId)
Result<InstallHistoryEntry>   insertInstallHistory(InstallHistoryEntry)
Result<QList<InstallHistoryEntry>> listInstallHistory()
Result<RollbackRecord>        insertRollbackRecord(RollbackRecord)
```

---

## 5. Import File Schemas

### 5.1 Question Import File

**Format:** JSON Lines (`.jsonl`) — one question object per line.

```json
{
  "body_text": "What is the minimum safe operating distance from a live conductor at 11kV?",
  "answer_options": [
    "0.3 m", "0.6 m", "1.0 m", "3.0 m"
  ],
  "correct_answer_index": 2,
  "difficulty": 4,
  "discrimination": 0.72,
  "knowledge_point_paths": ["Safety/Electrical/Clearances"],
  "tags": ["Safety", "Electrical", "Level-3"],
  "external_id": "Q-2025-0441"
}
```

**Validation rules:**
- `body_text`: required, non-empty, max 4000 chars.
- `answer_options`: required, 2–6 elements, each max 500 chars.
- `correct_answer_index`: required, integer in [0, answer_options.length - 1].
- `difficulty`: required, integer in [1, 5].
- `discrimination`: required, decimal in [0.00, 1.00].
- `knowledge_point_paths`: optional, each path as slash-delimited chapter hierarchy.
- `tags`: optional, each tag max 64 chars.
- `external_id`: optional, used for duplicate detection across imports.

**Duplicate handling:** If `external_id` matches an existing question, the import row is flagged as a duplicate and skipped (not rejected as an error). The per-row validation report records the disposition.

### 5.2 Roster Import File

**Format:** CSV (`.csv`) — UTF-8, comma-delimited, with header row.

**Required columns:** `member_id`, `barcode`, `mobile`, `name`, `term_start`, `term_end`, `punch_balance`

```csv
member_id,barcode,mobile,name,term_start,term_end,punch_balance
M-10041,BC-98712,(021) 555-1234,Zhang Wei,2025-09-01,2026-08-31,20
M-10042,BC-98713,(021) 555-5678,Li Fang,2025-09-01,2026-08-31,10
```

**Validation rules:**
- `member_id`: required, unique within file, alphanumeric+hyphen, max 32 chars.
- `barcode`: required, alphanumeric+hyphen, max 64 chars.
- `mobile`: required, format `(###) ###-####` after stripping spaces; stored normalized.
- `name`: required, max 128 chars (stored encrypted).
- `term_start`, `term_end`: required, ISO-8601 date (`YYYY-MM-DD`); `term_end > term_start`.
- `punch_balance`: required, non-negative integer.

---

## 6. Sync Package Schema (`.proctorsync`)

A `.proctorsync` file is a ZIP archive containing:

```
proctorsync/
├── manifest.json          # Canonical manifest — signing input
├── manifest.json.sig      # Ed25519 detached signature (raw 64 bytes, hex-encoded)
└── entities/
    ├── checkins.jsonl     # Check-in attempt records (append-only)
    ├── deductions.jsonl   # Deduction event records (append-only)
    ├── corrections.jsonl  # Correction request + approval records
    └── content_edits.jsonl # Question/KP edit records
```

**`manifest.json` structure:**

```json
{
  "schema_version": "1.0",
  "package_id": "uuid",
  "source_desk_id": "string",
  "export_timestamp": "ISO-8601 UTC",
  "since_watermark": "ISO-8601 UTC",
  "signer_key_id": "string",
  "entities": [
    {
      "type": "checkins",
      "file": "entities/checkins.jsonl",
      "record_count": 42,
      "sha256": "hex digest of file bytes"
    }
  ]
}
```

**Verification steps (in order):**
1. Decode `manifest.json.sig` (hex → 64 bytes).
2. Verify Ed25519 signature over the canonical UTF-8 bytes of `manifest.json` using the key identified by `signer_key_id` in the local trust store.
3. For each entity file listed in `manifest.entities`, compute SHA-256 and compare to the `sha256` field.
4. If any step fails → reject the entire package (fail closed).

---

## 7. Update Package Schema (`.proctorpkg`)

A `.proctorpkg` file is a ZIP archive containing:

```
proctorpkg/
├── update-manifest.json          # Update metadata — signing input
├── update-manifest.json.sig      # Ed25519 detached signature
└── components/
    ├── proctorops.exe            # (or other platform binaries / data files)
    └── ...
```

**`update-manifest.json` structure:**

```json
{
  "schema_version": "1.0",
  "package_id": "uuid",
  "version": "1.2.0",
  "min_version": "1.0.0",
  "target_platform": "windows-x86_64",
  "release_date": "ISO-8601 date",
  "description": "string",
  "signer_key_id": "string",
  "components": [
    {
      "name": "proctorops.exe",
      "file": "components/proctorops.exe",
      "version": "1.2.0",
      "sha256": "hex digest"
    }
  ],
  "rollback_supported": true,
  "rollback_instructions": "string"
}
```

---

## 8. Audit Export Structure

Produced by `DataSubjectService.fulfillExportRequest()` or `AuditService.exportAuditRange()`.

**Format:** JSON Lines (`.jsonl`) — one audit entry per line.

```json
{
  "id": "uuid",
  "timestamp": "ISO-8601 UTC",
  "actor_user_id": "string",
  "actor_username_masked": "string",
  "event_type": "CHECKIN_SUCCESS",
  "entity_type": "CheckIn",
  "entity_id": "uuid",
  "before_payload": { "...": "..." },
  "after_payload": { "...": "..." },
  "previous_entry_hash": "sha256 hex",
  "entry_hash": "sha256 hex",
  "_pii_note": "PII fields remain encrypted unless explicit decryption was authorized"
}
```

---

## 9. Export and Deletion Request Structures

### 9.1 ExportRequest

```json
{
  "id": "uuid",
  "member_id": "string",
  "requester_user_id": "string",
  "status": "PENDING | COMPLETED | REJECTED",
  "rationale": "string",
  "created_at": "ISO-8601 UTC",
  "fulfilled_at": "ISO-8601 UTC | null",
  "output_file_path": "string | null"
}
```

### 9.2 DeletionRequest

```json
{
  "id": "uuid",
  "member_id": "string",
  "requester_user_id": "string",
  "approver_user_id": "string | null",
  "status": "PENDING | APPROVED | COMPLETED | REJECTED",
  "rationale": "string",
  "created_at": "ISO-8601 UTC",
  "approved_at": "ISO-8601 UTC | null",
  "completed_at": "ISO-8601 UTC | null",
  "anonymization_record": {
    "fields_anonymized": ["mobile", "name", "barcode"],
    "audit_tombstone_retained": true
  }
}
```

---

## 10. Internal Command Types

Command objects used to pass validated input from UI layer to service layer:

```
CreateJobCmd:        job_type, source_file_path, priority, dependency_job_ids[], scheduled_at?
CreateQuestionCmd:   body_text, answer_options[], correct_answer_index, difficulty,
                     discrimination, knowledge_point_ids[], tag_ids[]
UpdateQuestionCmd:   question_id, + subset of CreateQuestionCmd
CreateKPCmd:         name, parent_id?, position
UpdateKPCmd:         kp_id, name?, parent_id?, position?
```

---

## 11. Requirement-to-Contract Traceability

| Requirement | Contract / schema |
|---|---|
| Local sign-in, lockout, CAPTCHA | `AuthService.signIn`, `AuthService.generateCaptcha`, `UserRepository` (lockout/CAPTCHA records) |
| RBAC (4 roles, >= comparison) | `AuthService.hasPermission`, `AuthService.requireRole` |
| Step-up verification (2-min window) | `AuthService.initiateStepUp`, `AuthService.consumeStepUp` |
| Console lock (Ctrl+L) | `AuthService.lockConsole`, `AuthService.unlockConsole` |
| Check-in validation (barcode/member/mobile) | `ICheckInService.checkIn`, `MemberRepository.findBy*` |
| 30-second duplicate suppression | `CheckInRepository.findDuplicateWithin30s` |
| Atomic punch-card deduction | `CheckInRepository.insertDeduction` (inside SQLite transaction) |
| Term-card / frozen blocking | `ICheckInService.checkIn` preconditions |
| Supervisor correction | `ICheckInService.requestCorrection`, `ICheckInService.approveCorrection` |
| Question CRUD, query builder | `IQuestionService`, `QuestionRepository.query(QuestionFilter)` |
| KnowledgePoint tree | `IQuestionService`, `KnowledgePointRepository.getTree` |
| Tags, difficulty, discrimination | `IQuestionService`, `CreateQuestionCmd` schema |
| Ingestion scheduler | `IIngestionService`, `IngestionRepository`, `JobCheckpointState` |
| Retry backoff + checkpoint | `IngestionRepository.saveCheckpoint`, `JobScheduler` (exponential backoff) |
| Sync package export/import | `ISyncService`, `.proctorsync` schema (§6) |
| Ed25519 signature verification | `PackageVerifier.verifyPackageSignature`, `Ed25519Verifier.verify` |
| Conflict detection and resolution | `ISyncService.resolveConflict`, `ConflictRecord` |
| AES-256-GCM field encryption | `AesGcmCipher.encrypt/decrypt`, `KeyStore.getMasterKey`, HKDF per-record |
| PII masking (last 4 digits) | `MaskingPolicy.maskMobile/maskBarcode/maskName/maskGeneric` |
| Clipboard PII redaction | `ClipboardGuard.copyMasked/copyRedacted` |
| Audit hash chain | `AuditService.recordEvent`, `AuditRepository.insertEntry` (atomic chain head update) |
| Audit chain verification | `AuditService.verifyChain`, `HashChain.computeEntryHash` |
| Structured secret-safe logging | `Logger` (auto-scrubs PII fields from context, JSON-lines output) |
| Error handling infrastructure | `ErrorFormatter.toUserMessage/toFormHint/isSecurityError` |
| GDPR/MLPS export/deletion | `IDataSubjectService`, export/deletion request structures (§9) |
| Update/rollback | `IUpdateService`, `.proctorpkg` schema (§7) |
| Import file validation | Question import schema (§5.1), Roster import schema (§5.2) |

---

## 12. Domain Model Types

All service and repository contracts use the concrete domain types defined in `src/models/`. Key types and their locations:

| Type | Header | Description |
|---|---|---|
| `User`, `Credential`, `LockoutRecord`, `CaptchaState`, `UserSession`, `StepUpWindow` | `models/User.h` | Auth domain |
| `Member`, `TermCard`, `PunchCard`, `MemberFreezeRecord`, `MemberIdentifier` | `models/Member.h` | Member domain |
| `CheckInAttempt`, `DeductionEvent`, `CorrectionRequest`, `CorrectionApproval`, `CheckInResult` | `models/CheckIn.h` | Check-in domain |
| `Question`, `KnowledgePoint`, `Tag`, `QuestionKPMapping`, `QuestionTagMapping`, `QuestionFilter` | `models/Question.h` | Question domain |
| `IngestionJob`, `JobDependency`, `JobCheckpoint`, `WorkerClaim` | `models/Ingestion.h` | Scheduler domain |
| `SyncPackage`, `SyncPackageEntity`, `ConflictRecord`, `TrustedSigningKey` | `models/Sync.h` | Sync domain |
| `AuditEntry`, `AuditFilter`, `ChainVerifyReport`, `ExportRequest`, `DeletionRequest` | `models/Audit.h` | Audit domain |
| `UpdatePackage`, `UpdateComponent`, `InstallHistoryEntry`, `RollbackRecord` | `models/Update.h` | Update domain |
| `ErrorCode` | `utils/Result.h` | Error classification |
| `Role`, `AuditEventType` | `models/CommonTypes.h` | Shared enums |
| All business invariants | `utils/Validation.h` | Authoritative constants |

## 13. Business Invariants (canonical reference)

Authoritative source: `src/utils/Validation.h`. All services, repositories, and tests **must** use these constants — never hardcode.

| Constant | Value | Context |
|---|---|---|
| `PasswordMinLength` | 12 | Auth |
| `LockoutFailureThreshold` | 5 | Auth |
| `LockoutWindowSeconds` | 600 (10 min) | Auth |
| `CaptchaAfterFailures` | 3 | Auth |
| `CaptchaCooldownSeconds` | 900 (15 min) | Auth |
| `StepUpWindowSeconds` | 120 (2 min) | Auth |
| `DuplicateWindowSeconds` | 30 | Check-in |
| `DifficultyMin / DifficultyMax` | 1 / 5 | Question |
| `DiscriminationMin / DiscriminationMax` | 0.00 / 1.00 | Question |
| `SchedulerDefaultWorkers` | 2 | Ingestion |
| `SchedulerMaxRetries` | 5 | Ingestion |
| `RetryDelay1/2/3Seconds` | 5 / 30 / 120 | Ingestion |
| `AuditRetentionYears` | 3 | Audit |
| `Argon2TimeCost / MemoryCost / Parallelism` | 3 / 65536 KiB / 4 | Crypto |
| `AesGcmKeyBytes / NonceBytes / TagBytes` | 32 / 12 / 16 | Crypto |

## 14. SQLite Schema Summary

Migrations and their domains:

| Migration | Domain | Key tables |
|---|---|---|
| `0001_initial_schema` | Infrastructure | `schema_migrations`, `app_lifecycle` |
| `0002_identity_schema` | Auth | `users`, `credentials`, `user_sessions`, `lockout_records`, `captcha_states`, `step_up_windows` |
| `0003_member_schema` | Members | `members`, `term_cards`, `punch_cards`, `member_freeze_records` |
| `0004_checkin_schema` | Check-in | `checkin_attempts`, `deduction_events`, `correction_requests`, `correction_approvals` |
| `0005_question_schema` | Questions | `questions`, `knowledge_points`, `tags`, `question_kp_mappings`, `question_tag_mappings` |
| `0006_ingestion_schema` | Ingestion | `ingestion_jobs`, `job_dependencies`, `job_checkpoints`, `worker_claims` |
| `0007_sync_schema` | Sync | `sync_packages`, `sync_package_entities`, `conflict_records`, `trusted_signing_keys` |
| `0008_audit_schema` | Audit | `audit_entries`, `audit_chain_head`, `export_requests`, `deletion_requests` |
| `0009_crypto_trust_schema` | Crypto | `key_store_entries`, `key_rotation_jobs` |
| `0010_update_schema` | Update | `update_packages`, `update_components`, `install_history`, `rollback_records` |
| `0011_export_compliance_schema` | Compliance | `redaction_rules`, `sync_watermarks` |
| `0012_workspace_schema` | Runtime | `workspace_state` (singleton), `performance_log` |

---

## 15. Operator Window Contracts

### 15.1 AppContext Initialization Contract

`AppContext` is the single point of infrastructure ownership. Its initialization order
is fixed to respect dependency relationships:

1. `KeyStore(storageDir)` → `getMasterKey()` → `AesGcmCipher(masterKey)`
2. All concrete repositories constructed against the shared `QSqlDatabase&`
3. `AuditService(AuditRepository, AesGcmCipher)`
4. `AuthService(UserRepository, AuditRepository)`
5. `QuestionService(QuestionRepository, KPRepository, AuditService, AuthService)`
6. `CheckInService(MemberRepository, CheckInRepository, AuthService, AuditService, AesGcmCipher, QSqlDatabase)`
7. `IngestionService(IngestionRepository, QuestionRepository, KPRepository, MemberRepository, AuditService, AuthService, AesGcmCipher)`
8. `JobScheduler(IngestionRepository, IngestionService, AuditService)`

If step 1 fails (key store unavailable), `main()` returns 1 without opening any window.

**Session field**: `ctx.session.token` and `ctx.session.userId` are written by the login
flow before `MainShell` is shown. Windows read `ctx.session.userId` as the `actorUserId`
for audit events and RBAC queries.

### 15.2 StepUpDialog Contract

| Method | Description |
|---|---|
| `StepUpDialog(authService, sessionToken, actionDescription, parent)` | Constructs the re-auth dialog |
| `stepUpWindowId() → QString` | Returns the granted `StepUpWindow.id`; empty if cancelled or failed |
| `exec() → int` | Returns `QDialog::Accepted` on successful step-up; `QDialog::Rejected` on cancel |

Callers must pass `stepUpWindowId()` to any service method that requires a step-up window id
(e.g., `CheckInService::approveCorrection(corrId, rationale, userId, stepUpWindowId)`).
The returned id is single-use; the service calls `AuthService::consumeStepUp()` internally.

### 15.3 CheckInWindow Contract

Window ID: `"window.checkin"` (`CheckInWindow::WindowId`)

**Input routing:**

| Tab | Identifier type | Source |
|---|---|---|
| Barcode | `MemberIdentifier::Type::Barcode` | `BarcodeInput` event filter on the window |
| Member ID | `MemberIdentifier::Type::MemberId` | Manual `QLineEdit` + Enter/button |
| Mobile | `MemberIdentifier::Type::Mobile` | Manual `QLineEdit` (normalized by `CheckInService`) |

**Result panel states (all CheckInStatus values):**

| Status | Display |
|---|---|
| `Success` | Green headline, masked name, remaining balance, timestamp, Correction button |
| `DuplicateBlocked` | Red headline, 30-second window message |
| `FrozenBlocked` | Red headline, freeze reason |
| `TermCardExpired` | Red headline, expired guidance |
| `TermCardMissing` | Red headline, no-term-card guidance |
| `PunchCardExhausted` | Red headline, zero-balance guidance |
| `Failed` | Red headline, service error message |

**Correction flow:** `CheckInService::requestCorrection(deductionEventId, rationale, operatorUserId)`.
The Correction button is hidden until a successful check-in occurs in the current session.

### 15.4 QuestionBankWindow Contract

Window ID: `"window.question_bank"` (`QuestionBankWindow::WindowId`)

Filter parameters mapped to `QuestionFilter`:

| UI control | QuestionFilter field |
|---|---|
| Difficulty min/max spinboxes | `difficultyMin`, `difficultyMax` |
| KP subtree combo | `knowledgePointId` |
| Text search | `textSearch` |
| Status combo | `statusFilter` |
| Page size | `limit = 100`, `offset` incremented by Load More |

### 15.5 QuestionEditorDialog Contract

Creates or edits via `QuestionService::createQuestion` / `updateQuestion`.
KP and tag changes are deferred until after the question is saved, then applied via
`mapQuestionToKP` / `unmapQuestionFromKP` and `applyTag` / `removeTag`.
All validation errors from the service are displayed in the error label (no exception thrown).

### 15.6 AuditViewerWindow Contract

Window ID: `"window.audit_viewer"` (`AuditViewerWindow::WindowId`)

Filter parameters mapped to `AuditFilter`:

| UI control | AuditFilter field |
|---|---|
| From/To DateTimeEdit | `fromTimestamp`, `toTimestamp` |
| Actor ID | `actorUserId` |
| Entity type / ID | `entityType`, `entityId` |
| Page size | `limit = 100`, `offset` incremented by Load More |

Export writes one JSON object per line to a user-selected JSONL file, then records an
`AuditExport` event. Verify Chain records a `ChainVerified` event regardless of the outcome.

---

## 16. Secondary Service Contracts

### 16.1 SyncService Contract

Window ID: `"window.sync"` (`SyncWindow::WindowId`)

```cpp
// Constructor
SyncService(ISyncRepository&, ICheckInRepository&, IAuditRepository&,
            AuthService&, AuditService&, PackageVerifier&);

// Export a package for transport to another desk (LAN share or USB)
Result<SyncPackage>    exportPackage(const QString& destinationDir,
                                     const QString& deskId,
                                     const QString& signerKeyId,
                                     const QByteArray& signerPrivKeyDer,
                                     const QString& actorUserId,
                                     const QString& stepUpWindowId);

// Import and verify a package received from another desk
Result<SyncPackage>    importPackage(const QString& packageDir,
                                     const QString& actorUserId);

// Resolve a pending conflict (SecurityAdministrator + step-up)
Result<void>           resolveConflict(const QString& conflictId,
                                        ConflictStatus resolution,
                                        const QString& actorUserId,
                                        const QString& stepUpWindowId);
// Resolution persists linkage metadata (resolution_action_type/resolution_action_id).
// DoubleDeduction + ResolvedAcceptIncoming path applies compensating correction
// for local deduction and materializes incoming deduction.

// Signing key management (SecurityAdministrator + step-up)
Result<TrustedSigningKey> importSigningKey(const QString& label,
                                            const QString& publicKeyDerHex,
                                            const QDateTime& expiresAt,
                                            const QString& actorUserId,
                                            const QString& stepUpWindowId);
Result<void>              revokeSigningKey(const QString& keyId,
                                            const QString& actorUserId,
                                            const QString& stepUpWindowId);
Result<QList<TrustedSigningKey>> listSigningKeys(const QString& actorUserId);
Result<QList<SyncPackage>>       listPackages(const QString& actorUserId);
Result<QList<ConflictRecord>>    listPendingConflicts(const QString& packageId,
                                                      const QString& actorUserId);
```

**Package format:** Directory-based `.proctorsync` bundle. The manifest JSON body is signed with Ed25519; the signature covers the JSON-compact serialization of all fields except `"signature"`. Import rejects any package whose signature does not verify against a non-revoked registered public key.

**Conflict detection:** After import, `detectAndRecordConflicts` scans incoming deduction records for members who already have a deduction record on this desk within the same session window. Each detected conflict is stored as a `ConflictRecord` with `status = Pending`.

**Signing key fingerprint:** SHA-256 of the raw DER-encoded public key bytes, stored in `trusted_signing_keys.fingerprint` with a UNIQUE constraint.

---

### 16.2 DataSubjectService Contract

Window ID: `"window.data_subject"` (`DataSubjectWindow::WindowId`)

```cpp
// Constructor
DataSubjectService(IAuditRepository&, IMemberRepository&,
                   AuthService&, AuditService&, AesGcmCipher&);

// Export request lifecycle
Result<ExportRequest>  createExportRequest(const QString& memberId,
                                            const QString& rationale,
                                            const QString& actorUserId);
Result<ExportRequest>  fulfillExportRequest(const QString& requestId,
                                             const QString& outputFilePath,
                                             const QString& actorUserId,
                                             const QString& stepUpWindowId);
Result<void>           rejectExportRequest(const QString& requestId,
                                            const QString& actorUserId,
                                            const QString& stepUpWindowId);
Result<QList<ExportRequest>> listExportRequests(const QString& actorUserId,
                                                const QString& statusFilter = {});

// Deletion request lifecycle
Result<DeletionRequest> createDeletionRequest(const QString& memberId,
                                               const QString& rationale,
                                               const QString& actorUserId);
Result<DeletionRequest> approveDeletionRequest(const QString& requestId,
                                                const QString& actorUserId,
                                                const QString& stepUpWindowId);
Result<void>            completeDeletion(const QString& requestId,
                                          const QString& actorUserId,
                                          const QString& stepUpWindowId);
Result<void>            rejectDeletionRequest(const QString& requestId,
                                               const QString& actorUserId,
                                               const QString& stepUpWindowId);
Result<QList<DeletionRequest>> listDeletionRequests(const QString& actorUserId,
                                                    const QString& statusFilter = {});
```

**State machines:**
- Export: `PENDING` → `COMPLETED` (fulfillExportRequest, step-up) | `REJECTED` (rejectExportRequest, step-up)
- Deletion: `PENDING` → `APPROVED` (approveDeletionRequest, step-up) → `COMPLETED` (completeDeletion, step-up) | `REJECTED` (rejectDeletionRequest, step-up)

**Export file structure:**
```json
{
  "export_type": "GDPR_SUBJECT_ACCESS",
  "WATERMARK": "AUTHORIZED_EXPORT_ONLY — NOT_FOR_REDISTRIBUTION",
  "generated_at": "<ISO8601>",
  "request_id": "<uuid>",
  "member": {
    "id": "<uuid>",
    "member_id": "<plain>",
    "name": "<plain-decrypted>",
    "mobile": "<masked last-4>",
    "barcode": "<masked last-4>"
  }
}
```

**Deletion completion:** `completeDeletion` calls `MemberRepository::anonymizeMember`, which overwrites all encrypted PII fields with `[ANONYMIZED]` and sets `deleted = 1`. Audit entries for the request are **not** erased — they are retained as compliance evidence for `AuditRetentionYears` (3 years).

**Validation:** `createExportRequest` and `createDeletionRequest` require non-whitespace `rationale`; empty rationale returns `ErrorCode::ValidationFailed`. Both methods also validate member existence via `MemberRepository::findMemberById`; non-existent member IDs are rejected before the request record is created.

---

### 16.3 UpdateService Contract

Window ID: `"window.update"` (`UpdateWindow::WindowId`)

**Override note:** delivery does **not** require a signed `.msi` artifact. The full update domain model, signature verification, staging, install history, and rollback logic are in scope and implemented.

```cpp
// Constructor
UpdateService(IUpdateRepository&, ISyncRepository&, AuthService&, PackageVerifier&, AuditService&);

// Import a .proctorpkg directory; verify signature and all component digests
Result<UpdatePackage>       importPackage(const QString& pkgDir,
                                          const QString& actorUserId);

// Apply a Staged package (SecurityAdministrator + step-up)
Result<void>                applyPackage(const QString& packageId,
                                          const QString& currentVersion,
                                          const QString& actorUserId,
                                          const QString& stepUpWindowId);

// Rollback to a prior install history entry (SecurityAdministrator + step-up)
Result<RollbackRecord>      rollback(const QString& installHistoryId,
                                      const QString& rationale,
                                      const QString& actorUserId,
                                      const QString& stepUpWindowId);

// Cancel a Staged package (operator opts not to apply)
Result<void>                cancelPackage(const QString& packageId,
                                           const QString& actorUserId);

// Queries
Result<QList<UpdatePackage>>       listPackages(const QString& actorUserId);
Result<QList<InstallHistoryEntry>> listInstallHistory(const QString& actorUserId);
Result<QList<RollbackRecord>>      listRollbackRecords(const QString& actorUserId);
```

**Package status transitions:**
```
importPackage() → Staged  (signature + digests valid)
               → Rejected (invalid signature or digest mismatch)
applyPackage() → Applied  (from Staged only)
cancelPackage()→ Cancelled(from Staged only)
rollback()     → RolledBack (on the originally Applied package)
```

**`applyPackage` behavior:** Records `InstallHistoryEntry` with `fromVersion = currentVersion`, `toVersion = pkg.version`, and `snapshotPayloadJson` (serialized component state before apply). Marks the package status `Applied`.

**`rollback` behavior:** Requires non-empty `rationale` (returns `ValidationFailed` if empty). Reads the `InstallHistoryEntry` to reconstruct `fromVersion`/`toVersion`. Records a `RollbackRecord` with `toVersion = history.fromVersion` (the pre-update version being restored).

**Digest verification:** For each component in `update-manifest.json`, the service reads the actual file from the staged directory, computes its SHA-256, and compares against the manifest entry. Any mismatch returns `ErrorCode::PackageCorrupt`.

---

### 16.4 SecurityAdminWindow Contract

Window ID: `"window.security_admin"` (`SecurityAdminWindow::WindowId`)

The window exposes three tabs and does not call any service directly that is not already gated by RBAC at the service layer:

| Tab | Backing service | Step-up required |
|---|---|---|
| User Roles — Create User | `AuthService::createUser` | Yes |
| User Roles — Reset Password | `AuthService::resetUserPassword` | Yes |
| User Roles — Change Role | `AuthService::changeUserRole` | Yes |
| User Roles — Unlock | `AuthService::unlockUser` | Yes |
| User Roles — Deactivate | `AuthService::deactivateUser` | Yes |
| Account Freezes — Freeze | `CheckInService::freezeMemberAccount` | Yes |
| Account Freezes — Thaw | `CheckInService::thawMemberAccount` | Yes |
| Account Freezes — List | `CheckInService::listRecentFreezeRecords` | No |
| Privileged Audit | `AuditService::queryEvents` (filtered) | No |

**Privileged audit filter:** event types `ROLE_CHANGED`, `USER_UNLOCKED`, `USER_DEACTIVATED`, `KEY_IMPORTED`, `KEY_REVOKED`, `STEP_UP_INITIATED`, `STEP_UP_PASSED`, `MEMBER_FREEZE_APPLIED`, `MEMBER_FREEZE_THAWED`, `EXPORT_COMPLETED`, `DELETION_COMPLETED`, `UPDATE_APPLIED`, `UPDATE_ROLLED_BACK`, `SYNC_CONFLICT_RESOLVED`; last 30 days; no pagination (scrollable table).

**Widget accessibility invariants (enforced by `tst_security_admin_window`):**
- `QTabWidget` with exactly 3 tabs: "User Roles", "Account Freezes", "Privileged Audit"
- Reset Password, Change Role, and Unlock buttons are disabled until a row in the users table is selected
- At least 2 `QLineEdit` inputs present (member ID + freeze reason)
- Users table has 4 columns; audit table has 5 columns

---

### 16.5 IngestionMonitorWindow Contract

Window ID: `"window.ingestion_monitor"` (`IngestionMonitorWindow::WindowId`)

Single-pane job monitoring window backed by `IngestionService`:

| Column | Source field |
|---|---|
| Job ID | `IngestionJob.id` (truncated UUID) |
| Type | `QuestionImport` / `RosterImport` |
| Status | `Pending` / `Claimed` / `Validating` / `Importing` / `Indexing` / `Completed` / `Failed` / `Interrupted` / `Cancelled` |
| Phase | current ingestion phase |
| Priority | integer 1–10 |
| Retries | `retryCount` |
| Created At | ISO 8601 UTC |

Auto-refresh interval: 5 seconds (`QTimer`). Cancel Job button enabled only for `Pending` status jobs; calls `IngestionService::cancelJob`. No pagination — all active and recently completed jobs are shown.
