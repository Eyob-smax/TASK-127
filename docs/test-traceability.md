# Test Traceability Matrix — ProctorOps

Requirement-to-test mapping for static audit review.
All tests run headless (`QT_QPA_PLATFORM=offscreen`) with in-memory SQLite (`:memory:`).

**Summary:** 39 unit test files + 13 integration test files (52 CTest targets total).
`repo/run_tests.sh` reports both selected target count and aggregate QTest function count for each run.

---

## Security and Authentication

| Requirement | Unit Test(s) | Integration Test(s) | Key Assertions |
|---|---|---|---|
| Argon2id password hashing | tst_crypto (hashAndVerify, wrongPassword, saltUniqueness, timingConsistency, emptyPassword) | tst_auth_integration | Constant-time verify, unique salt per hash, wrong password rejected |
| AES-256-GCM field encryption | tst_crypto (encryptDecrypt, tamperedCiphertext, differentKeys, emptyPlaintext, versionByte, hkdfDerived) | tst_audit_integration | Round-trip decrypt, tamper detection, HKDF per-record derivation |
| Ed25519 signature verification | tst_crypto (verifyValid, verifyTampered, verifyWrongKey) | tst_package_verification | Valid sig passes, tampered data fails, wrong key fails |
| Ed25519 signing (key gen + sign) | tst_crypto (generateKeyPair, signAndVerify, signDifferentMessages, tamperedMessageFails) | tst_sync_import_flow, tst_update_flow | Key pair generation, sign→verify round-trip, different messages→different sigs |
| SHA-256 hash chain | tst_crypto (hashChain x4) | tst_audit_integration | Linkage, deterministic replay, tamper detection |
| Account lockout (5 in 10 min) | tst_auth_service (lockoutAfter5Failures, lockedAccountRejected, lockedAccountRejectsEvenWithCaptcha) | tst_auth_integration | 5th failure locks, locked account rejects even with valid credentials |
| CAPTCHA after 3 failures | tst_auth_service (captchaRequiredAfter3Failures, captchaCooldownResets, captchaMissingStateFailsClosed) | tst_auth_integration (captchaFlow_requiredAfter3, captchaFlow_missingStateFailsClosed) | CaptchaRequired after threshold, fail-closed if CAPTCHA state missing, cooldown behavior |
| Step-up re-auth (2 min, single-use) | tst_auth_service (validWithin2Minutes, consumedOnlyOnce, wrongPassword, expiredAfter2Minutes), tst_privileged_scope (expiredWindowRejected, wrongPasswordRejected, consumeOnlyOnce, windowDuration2Minutes) | tst_auth_integration, tst_correction_flow | 120s duration, consumed=1 after use, expired→StepUpRequired |
| RBAC role hierarchy | tst_auth_service (hasPermission, requireRole_denied), tst_privileged_scope (6 RBAC tests) | tst_auth_integration | Operator < Proctor < ContentManager < SecurityAdmin; requireRole enforced per session |
| Security-admin user provisioning/reset | tst_auth_service (securityAdmin_createUser, securityAdmin_resetUserPassword) | tst_auth_integration (securityAdminProvisioning_createAndResetPassword) | Step-up gated create/reset, Argon2id credential replacement, old password invalidated |
| Console lock/unlock | tst_auth_service (requiresPasswordToUnlock) | tst_auth_integration | Password required to unlock |
| Audit event recording | tst_auth_service (auditEventsRecorded), tst_audit_chain | tst_audit_integration | Events recorded for login/logout/step-up/lock |

## PII Protection and Masking

| Requirement | Unit Test(s) | Integration Test(s) | Key Assertions |
|---|---|---|---|
| MaskingPolicy field rules | tst_masked_field (generic x5, mobile x3, barcode x2, name x2, step-up x1), tst_privileged_scope (allFieldsRequireStepUp, mobileMasksAllButLast4) | — | Last-4-visible, step-up required for all field types |
| ClipboardGuard PII redaction | tst_clipboard_guard (maskValue x4, copyMasked x2, copyRedacted x2), tst_privileged_scope (neverExposesPlaintextPII, redactedWritesRedactedTag) | — | Clipboard never contains raw PII, [REDACTED] tag on copyRedacted |
| Audit payload PII encryption | tst_audit_chain (piiEncryptedInPayload includes member_id/mobile/barcode/name) | tst_audit_integration | Raw member identifiers and other PII are not stored plaintext in audit payload JSON |

## Entry Validation and Check-In

| Requirement | Unit Test(s) | Integration Test(s) | Key Assertions |
|---|---|---|---|
| Barcode check-in | tst_checkin_service (byBarcode) | tst_checkin_flow | USB HID barcode resolves member, deduction occurs |
| Member ID check-in | tst_checkin_service (byMemberId) | tst_checkin_flow | Manual member ID resolves to member |
| Mobile check-in | tst_checkin_service (byMobile, normalization x4) | tst_checkin_flow | Normalized to (###) ###-####, encrypted lookup |
| Term card validation | tst_checkin_service (expired, missing, frozen) | tst_checkin_flow | Expired→TermCardExpired, missing→TermCardMissing, frozen→blocked |
| Punch-card atomic deduction | tst_checkin_service (deduction, exhausted) | tst_checkin_flow | Balance decremented atomically, 0-balance→exhausted |
| 30-second duplicate suppression | tst_checkin_service (duplicateBlocked) | tst_checkin_flow | Second check-in within 30s→DuplicateBlocked |
| Correction workflow | tst_checkin_service (corrections x4) | tst_correction_flow, tst_privileged_scope (fullAuthorization, doubleReversalBlocked) | Request→approve→apply→balance restored; double reversal blocked |

## Content Governance

| Requirement | Unit Test(s) | Integration Test(s) | Key Assertions |
|---|---|---|---|
| Question CRUD | tst_question_service (create, update, delete, getById, list, duplicate-externalId, deactivate, reactivate, count, listByStatus, difficulty validation) | tst_operator_workflows | Full lifecycle with validation |
| Query builder | tst_question_service (byDifficulty, byKpSubtree, byText, byStatus, combined) | tst_operator_workflows | Multi-criteria filter with pagination |
| KnowledgePoint tree | tst_question_service (kpCreate, kpChildren, kpMove, kpDelete) | — | Materialized path propagation, cascade |
| KP-to-question mappings | tst_question_service (mapKp, unmapKp) | tst_operator_workflows | Many-to-many mapping/unmapping |
| Tags | tst_question_service (createTag, listTags, applyTag, removeTag) | — | Tag CRUD, apply/remove |

## Scheduler and Ingestion

| Requirement | Unit Test(s) | Integration Test(s) | Key Assertions |
|---|---|---|---|
| Priority ordering | tst_job_scheduler (highFirst, tieBreakByCreatedAt) | — | Higher priority first, FIFO tie-break |
| Fairness (no starvation) | tst_job_scheduler (fairness_lowPriorityEventuallyReady) | — | Low-priority jobs remain in ready list, not starved |
| Dependency resolution | tst_job_scheduler (blocksUntilMet) | — | Dependent job excluded until prereq completed |
| Retry backoff | tst_job_scheduler (retryBackoff_schedule, maxRetriesPermanentFailure) | — | Exponential delay, permanent failure after max retries |
| Worker cap | tst_job_scheduler (max2) | — | Max 2 concurrent workers |
| Crash recovery | tst_job_scheduler (interruptedJobsReenqueued) | — | In-progress jobs marked Interrupted on crash |
| Scheduled jobs | tst_job_scheduler (deferredUntilTime) | — | Future scheduledAt deferred |
| Job checkpoints | tst_job_checkpoint (~7 tests) | — | Checkpoint save/restore across phases |
| JSONL question import | tst_ingestion_service (jsonl x6) | tst_operator_workflows | 3-phase validate→import→index |
| CSV roster import | tst_ingestion_service (csv x6) | tst_operator_workflows | 3-phase validate→import→index |

## Privileged-Action Scope Enforcement

| Requirement | Unit Test(s) | Integration Test(s) | Key Assertions |
|---|---|---|---|
| RBAC denials (all role pairs) | tst_privileged_scope (operatorDeniedContentManager, operatorDeniedSecurityAdmin, proctorDeniedSecurityAdmin, contentManagerDeniedSecurityAdmin, securityAdminHasAllPermissions) | — | hasPermission returns false for insufficient roles |
| requireRole enforcement | tst_privileged_scope (requireRole_deniesLowerRole) | tst_auth_integration | Session-based role check → AuthorizationDenied |
| Step-up expired window | tst_privileged_scope (expiredWindowRejected) | — | Expired step-up → StepUpRequired |
| Step-up wrong password | tst_privileged_scope (wrongPasswordRejected) | — | Bad password → InvalidCredentials |
| Step-up single-use | tst_privileged_scope (consumeOnlyOnce) | — | Second consume → StepUpRequired |
| Step-up 120s duration | tst_privileged_scope (windowDuration_2Minutes) | — | expiresAt - grantedAt == 120 |
| Export state machine | tst_privileged_scope (cannotFulfillFromRejected, cannotFulfillFromCompleted) | tst_privileged_scope_api (cannotFulfillRejected, fullFlow_withAudit) | PENDING→COMPLETED only, rejected/completed→fulfill fails |
| Deletion state machine | tst_privileged_scope (cannotApproveFromRejected, cannotCompleteFromPending) | tst_privileged_scope_api (cannotCompleteWithoutApproval, fullThreeStepFlow) | PENDING→APPROVED→COMPLETED only |
| Rollback rationale required | tst_privileged_scope (rollback_emptyRationale_rejected) | — | Empty/whitespace rationale → ValidationFailed |
| Export rationale required | tst_privileged_scope (exportRequest_emptyRationale_rejected) | — | Whitespace-only rationale → ValidationFailed |
| Deletion rationale required | tst_privileged_scope (deletionRequest_emptyRationale_rejected) | — | Empty rationale → ValidationFailed |
| Update/Data-subject list RBAC | tst_update_service (listInstallHistory/listRollbackRecords/cancelPackage denied for operator), tst_data_subject_service (listExportRequests/listDeletionRequests denied for operator) | — | Sensitive list/cancel methods return AuthorizationDenied for insufficient role |
| Audit chain verify authorization | — | tst_privileged_scope (verifyChain_deniedForNonAdmin, verifyChain_allowedForSecurityAdmin) | Non-admin → AuthorizationDenied; SecurityAdmin → allowed |
| Export request member validation | — | tst_privileged_scope (exportRequest_rejectsNonExistentMember) | Non-existent member ID → rejected at creation |

## Compliance (GDPR / MLPS)

| Requirement | Unit Test(s) | Integration Test(s) | Key Assertions |
|---|---|---|---|
| Export request lifecycle | tst_data_subject_service (create, fulfill, reject, requiresRationale) | tst_export_flow, tst_privileged_scope_api (fullFlow_withAudit, maskedFieldsInExportFile) | Watermarked JSON output, masked PII, audit events |
| Deletion request lifecycle | tst_data_subject_service (create, approve, complete, reject, requiresRationale) | tst_privileged_scope_api (fullThreeStepFlow, auditTombstonesRetainedAfterDeletion) | 3-step flow, member anonymized (deleted=1), audit retained |
| Masked fields in export | — | tst_privileged_scope_api (maskedFieldsInExportFile) | Raw mobile digits not in export file |
| Audit tombstone retention | — | tst_privileged_scope_api (auditTombstonesRetainedAfterDeletion) | Audit entries retained after member deletion |
| Export file watermark | tst_data_subject_service (writesFile) | tst_export_flow, tst_privileged_scope_api | AUTHORIZED_EXPORT_ONLY in output |

## Sync and Update

| Requirement | Unit Test(s) | Integration Test(s) | Key Assertions |
|---|---|---|---|
| Signing key import/revoke | tst_sync_service (importKey, revokeKey, listKeys, expiredKey) | tst_sync_import_flow | Key registered in trust store, revocation blocks verification |
| Sync package export + import | tst_sync_service (importVerified, importConflict, importTampered) | tst_sync_import_flow (exportThenImport_roundTripAcrossDesks, importPackage_appliesIncomingDeduction) | Export produces signed package artifacts and import verifies signature/digests then materializes non-conflicting deductions |
| Sync conflict resolution linkage | — | tst_sync_import_flow (resolveConflict_linksCompensatingAction) | AcceptIncoming on double-deduction links compensating correction action metadata and applies incoming deduction |
| Key revocation blocks import | — | tst_sync_import_flow, tst_privileged_scope_api (syncKeyRevocation_blocksImport) | Revoked key → SignatureInvalid on package import |
| Update package staging | tst_update_service (import x4) | tst_update_flow | Manifest parsed, signature verified, status=Staged |
| Update apply + history | tst_update_service (apply x2, applyPackage_deploysArtifacts) | tst_update_flow, tst_privileged_scope_api (updateApply_recordsInstallHistory) | InstallHistoryEntry with from/to version, audit events, live component deployment verified |
| Rollback records | tst_update_service (rollback x2, rollback_restoresArtifacts, list) | tst_update_flow | RollbackRecord created, rationale required, package→RolledBack, live artifacts restored from snapshot backup |
| Package verification (Ed25519+SHA-256) | tst_crypto (ed25519 verifier+signer) | tst_package_verification | Signature valid, digest match, tamper detected |

## Shell, Recovery, and UI

| Requirement | Unit Test(s) | Integration Test(s) | Key Assertions |
|---|---|---|---|
| AppSettings config defaults + paths | tst_app_settings (~9 tests) | — | Default paths non-empty, no hard-coded dev paths, round-trip mutations |
| Workspace state persistence | tst_workspace_state (12 tests) | tst_shell_recovery | Save/restore window list, active index, geometry |
| Crash detection | tst_crash_recovery | tst_shell_recovery | Lock file detection, state restoration |
| Action routing | tst_action_routing (advancedFilterDispatchNoRecursion) | — | Named action registry, shortcut dispatch, Ctrl+F non-recursive dispatch modelling full MainShell handler→shortcut→handler path |
| Command palette (Ctrl+K) | tst_command_palette | — | Fuzzy search, keyboard navigation, action dispatch |
| System tray / kiosk mode | tst_tray_mode (state transitions x5, kiosk x6) | — | Mode transitions, lock state, operator-safe exit |
| CheckInWindow structure | tst_checkin_window | — | 3-tab layout, result panel, correction button |
| QuestionBankWindow structure | tst_question_bank_window | — | Paginated table, filter panel, initial data load, activateFilter focus |
| AuditViewerWindow structure | tst_audit_viewer | — | Date filter, table, detail pane, chain verify |
| LoginWindow structure | tst_login_window | — | Sign-in form layout, CAPTCHA hidden by default, bootstrap-mode transition |
| SyncWindow structure | tst_sync_window | — | Packages/Conflicts/Signing Keys tabs, row-scoped actions disabled until selection |
| UpdateWindow structure | tst_update_window | — | Staged/History/Rollback tabs, apply/cancel/rollback gated by row selection |
| DataSubjectWindow structure | tst_data_subject_window | — | Export/Deletion tabs, fulfill/approve/complete actions disabled until selection |
| IngestionMonitorWindow structure | tst_ingestion_monitor_window | — | Job table schema, unavailable-service status message, cancel button gating |
| SecurityAdminWindow structure | tst_security_admin_window (7 tests) | — | 3-tab layout, button states, step-up gates |
| MainShell MDI workspace | tst_main_shell | — | Window deduplication by ID, workspace state sync on close, crash-restore reopens tracked windows, lock/unlock signals, shell action registration |
| Application startup lifecycle | tst_application | — | DB open after initialize(), cold-start ms >= 0, app_lifecycle row written, clean shutdown closes record |
| AppContext ownership and defaults | tst_app_context | — | Unauthenticated by default, empty session userId, owned component pointers retained |
| Schema constraints | tst_domain_validation | tst_schema_constraints | UNIQUE, CHECK, FK, default values |
| Migration infrastructure | tst_migration | — | Sequential ordering, schema_migrations tracking |

---

## Test Infrastructure

| Component | Details |
|---|---|
| Framework | Qt Test + CTest |
| Database | In-memory SQLite (`:memory:`) per test or per suite |
| Headless UI | `QT_QPA_PLATFORM=offscreen` |
| Docker | `repo/run_tests.sh` — docker-first execution |
| Flags | `--rebuild`, `--test-filter`, `--unit-only`, `--api-only` |
| Fixtures | `database/fixtures/` — deterministic seed data |
| Migrations | `database/migrations/` — full schema applied per test |
