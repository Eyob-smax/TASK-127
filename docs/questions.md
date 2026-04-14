# questions.md

## 1. Bootstrap account creation and account recovery

**The Gap**  
The prompt requires local username/password sign-in and strict lockout behavior, but it does not define how the first security administrator account is provisioned or how locked-out operators recover access in a fully offline deployment.

**The Interpretation**  
Treat bootstrap identity setup and password recovery as controlled local administrative workflows rather than self-service features. Require an initial installer/bootstrap flow that creates the first security administrator locally, and require later account unlock/reset operations to be performed only by a security administrator with step-up verification and audit logging.

**Proposed Implementation**  
Create a bootstrap setup path that runs only when no administrator exists and persists a one-time installation state flag in SQLite. Store users, credential metadata, lockout counters, and recovery events in dedicated tables. Implement unlock/reset through a security-admin dialog gated by step-up reauthentication, explicit rationale entry, and tamper-evident audit writes.

**Status:** Confirmed. `AuthService` implements the full sign-in flow with lockout and step-up, and now also provides `bootstrapSecurityAdministrator(...)` for first-run provisioning. `LoginWindow` creates the initial SecurityAdministrator only when no active admin exists and emits a normal authenticated session on success. Startup no longer opens `MainShell` through an unauthenticated bootstrap bypass.

## 2. CAPTCHA modality and storage semantics

**The Gap**  
The prompt says CAPTCHA is generated locally after the third failed attempt and clears after a 15-minute cool-down, but it does not define the CAPTCHA style, expiration mechanics, replay rules, or whether solved CAPTCHAs survive app restarts.

**The Interpretation**  
Use a lightweight local image CAPTCHA rendered inside Qt, scoped to the username plus device session, invalidated after each failed solve or successful login, and persisted only long enough to preserve lockout integrity across restarts.

**Proposed Implementation**  
Generate CAPTCHA challenges with secure random text rendered via Qt painting into an image with distortion/noise. Store a hashed challenge answer, issued-at timestamp, expiry state, and attempt counter in SQLite or a secure local cache so restart behavior is deterministic. Reset the CAPTCHA requirement after a successful login or after the 15-minute cool-down window expires.

**Status:** Confirmed. `CaptchaGenerator` renders a 5-6 char random string (charset excludes ambiguous chars I/O/0/1) with QPainter distortion and noise lines. `CaptchaGenerator::verifyAnswer()` compares SHA-256 of lowercase-trimmed input. CAPTCHA state persisted in SQLite `captcha_states` table with `issuedAt`, `expiresAt`, `solveAttempts`, `solved` fields. `AuthService.signIn()` checks CAPTCHA requirement at 3+ failures and respects the 15-minute cool-down.

## 3. Term-card and punch-card business semantics

**The Gap**  
The prompt requires term-card date validation and atomic punch-card session deduction, but it does not define how term cards and punch cards coexist for a member, what precedence applies when both are present, or what exact correction behavior should occur after a wrong deduction.

**The Interpretation**  
Treat term-card entitlement validation as an access gate and punch-card deduction as a separate consumable entitlement. When both exist, require the member to pass term-card validity first and then deduct from the specific punch-card product linked to the session or roster rule. Wrong deductions remain reversible only through a supervisor-approved correction workflow.

**Proposed Implementation**  
Model term cards, punch-card bundles, member entitlements, deduction records, and correction records as separate tables. Resolve entitlement selection through a service that evaluates session requirements, active term-card windows, frozen states, and available punch balance inside one SQLite transaction. Write immutable deduction events and compensating correction events rather than editing balances in place.

**Status:** Confirmed. `CheckInService.checkIn()` implements the full flow: (1) resolve member by barcode/memberId/mobile, (2) check soft-delete, (3) check active freeze → FrozenBlocked, (4) check term cards for today → TermCardMissing/TermCardExpired, (5) pre-transaction duplicate suppression via `findRecentSuccess`, (6) find active punch card → PunchCardExhausted, (7) `BEGIN IMMEDIATE` transaction with duplicate re-check under write lock, then `deductSession` + `insertDeduction` + `insertAttempt(Success)` + commit. Each failure branch records a `CheckInAttempt` with appropriate status and audit event. `MemberRepository.deductSession` uses `UPDATE WHERE current_balance > 0` for atomic deduction. `restoreSession` reverses it for corrections.

## 4. Supervisor override scope for correction workflows

**The Gap**  
The prompt requires a supervisor override for sync corrections and override-sensitive actions, but it does not specify which role is the supervisor, whether the override is a separate approval or an immediate co-sign, or what evidence must be recorded.

**The Interpretation**  
Treat the supervisor as a security administrator by default. Require a two-step correction flow: the operator records the issue, and a security administrator completes the override with step-up reauthentication, explicit rationale, and a recorded before/after diff.

**Proposed Implementation**  
Implement correction requests as stateful records with statuses such as pending, approved, rejected, and applied. Require the approving administrator to re-enter the password within a 2-minute step-up window. Persist approver identity, timestamps, reason text, affected deduction IDs, and audit hash links. Surface these records in the audit viewer and correction history UI.

**Status:** Confirmed. `CheckInService` implements the full correction workflow: `requestCorrection` validates the deduction exists and is not already reversed, creates a Pending `CorrectionRequest`, and writes a `CorrectionRequested` audit event. `approveCorrection` enforces that the approver holds SecurityAdministrator role and has a valid step-up window (2-minute, single-use), captures before/after snapshots (deduction event + punch-card balance), inserts a `CorrectionApproval` with approver identity, reason, and diff payload, and writes `CorrectionApproved` audit. `applyCorrection` now also requires SecurityAdministrator + valid step-up, then calls `MemberRepository.restoreSession` to atomically restore the punch-card balance, marks the deduction as reversed, and writes `CorrectionApplied` audit. `rejectCorrection` also requires SecurityAdministrator + step-up and transitions to Rejected with `CorrectionRejected` audit. All state transitions and audit writes are tested in `tst_checkin_service` and `tst_correction_flow`.

## 5. Signed LAN-share / USB package trust bootstrap

**The Gap**  
The prompt requires signed sync packages and signed update imports, but it does not define how signing keys are provisioned, rotated, revoked, or trusted across multiple desks in a disconnected environment.

**The Interpretation**  
Assume each site has a local offline trust root and one or more approved signing keys distributed during installation or security administration. Package trust verification must succeed entirely offline and must fail closed when a key is missing, revoked, expired, or mismatched.

**Proposed Implementation**  
Store trusted public keys, key metadata, and revocation flags in SQLite. Use detached signatures over a canonical manifest plus file digests. Implement a trust-store management module for security administrators, including key import, fingerprint display, activation/deactivation, and audit logging. Verify sync and update packages against the local trust store before import.

**Status:** Confirmed. `TrustedSigningKey` model stores Ed25519 DER public keys, SHA-256 fingerprints, import metadata, expiry, and revocation flags in `trusted_signing_keys` table. `SyncRepository` provides CRUD. `PackageVerifier` checks key revocation and expiry before delegating to `Ed25519Verifier.verify()`. `Ed25519Verifier.computeFingerprint()` provides SHA-256 fingerprinting for display/confirmation.

## 6. Encryption key custody and rotation in an offline Windows deployment

**The Gap**  
The prompt requires AES-256 encryption for sensitive fields, but it does not define where encryption keys live, how they are rotated, or how the app restores access after restart without network-based secret delivery.

**The Interpretation**  
Use a locally protected master key strategy suitable for an offline Windows deployment. Keep key rotation operator-driven and auditable. Do not depend on remote KMS or cloud secret storage.

**Proposed Implementation**  
Generate a per-installation master key on first-run, protect it with OS-backed local secure storage where available, and derive per-record or per-field data-encryption keys using HKDF or an equivalent derivation method. Version all ciphertext payloads and implement a key-rotation job that re-encrypts records in batches with checkpointing and audit logging. Document any OS-specific fallback in `docs/questions.md` and `docs/design.md`.

**Status:** Confirmed. `KeyStore` (implements `IKeyStore`) uses Windows DPAPI (`CryptProtectData`/`CryptUnprotectData`) for master key protection. Linux fallback uses XOR obfuscation with a machine-specific file (documented as NOT equivalent security — acceptable only for Docker/CI). `AesGcmCipher` uses HKDF-SHA256 with OpenSSL `OSSL_PARAM` API to derive per-record DEKs from the master key + 16-byte random salt. Ciphertext format: `[version:1][salt:16][nonce:12][ciphertext][tag:16]`. Key rotation supported via `rotateMasterKey()` with atomic temp+rename and `.prev` backup.

## 7. Import file formats and validation contracts

**The Gap**  
The prompt requires importing question files and check-in rosters from the file system, but it does not define the accepted file formats, required columns, duplicate-row handling, or indexing rules.

**The Interpretation**  
Adopt explicit local file contracts for CSV and JSON as the default supported import formats. Reject malformed files deterministically, isolate failures per job, and keep row-level validation results reviewable by operators.

**Proposed Implementation**  
Define manifest schemas and per-type parsers for question imports and roster imports in `docs/api-spec.md`. Implement a validation phase that checks required columns, numeric ranges, mapping references, duplicate records, and unsupported encodings before the import phase runs. Persist row-level validation outcomes and checkpoint offsets so interrupted jobs can resume safely.

**Status:** Confirmed. `IngestionService` implements the full 3-phase import pipeline (Validate → Import → Index) for two file formats and enforces ContentManager-or-higher role at service entrypoints (`createJob`, `cancelJob`). **Question import (JSONL):** one JSON object per line with fields `body_text`, `answer_options` (array), `correct_answer_index`, `difficulty` (1–5), `discrimination` (0.00–1.00), optional `external_id`, `knowledge_point_path`, `tags`. Validation checks all constraints per `Validation.h`, rejects duplicate `external_id` values. Import phase inserts `Question` records, resolves KP paths by walking/creating tree nodes, resolves tags by name (find or create). **Roster import (CSV):** required columns `member_id`, `name`, `barcode`, `mobile`, `term_start`, `term_end`; optional `product_code`, `punch_balance`. Validation checks mobile normalization to `(###) ###-####`, date range validity, non-negative punch balance. Import phase encrypts PII (name, barcode, mobile) via `AesGcmCipher`, upserts members, creates `TermCard` records, creates `PunchCard` records when product/balance present. Both pipelines save `JobCheckpoint` every 100 records for resumable restart. Phase aborts if error rate exceeds 50%. Tested in `tst_ingestion_service`.

## 8. Multi-desk sync conflict rules beyond double deduction

**The Gap**  
The prompt explicitly mentions conflict prompts for double deductions, but it does not define how other desk-to-desk conflicts should be resolved, such as concurrent content edits, roster updates, or member-freeze changes collected offline at different desks.

**The Interpretation**  
Treat double-deduction conflicts as the highest-risk mandatory interactive conflict. For other entities, use entity-specific merge rules with fail-safe escalation: append-only audit/history data merges automatically, while conflicting mutable records require manual review before import finalization.

**Proposed Implementation**  
Version sync payload entities with source desk IDs, timestamps, and logical sequence numbers. Implement a conflict-classification service that auto-merges append-only records, flags last-writer conflicts on mutable records, and routes sensitive changes into a review queue. Record every auto-merge or manual resolution in the tamper-evident audit chain.

**Status:** Partially confirmed. The correction workflow in `CheckInService` provides the foundation for double-deduction conflict resolution: deduction events are immutable, reversals use compensating `CorrectionRequest` / `CorrectionApproval` records with before/after snapshots, and `MemberRepository.restoreSession` atomically restores punch-card balance. `SyncRepository` stores sync entities with `source_desk_id`, `received_at`, and conflict classification (`AutoMerged`, `ManualReview`, `Resolved`, `Rejected`). The conflict-classification service and interactive merge UI for non-deduction entity types will be wired in a later prompt.

## 9. Update / rollback feature scope after relaxing the signed `.msi` artifact requirement

**The Gap**  
The original prompt mentions update/rollback via importing a signed `.msi` from disk, while the override removes the requirement to deliver a signed `.msi` artifact. The remaining feature boundary needs to stay precise so implementation does not underbuild or overbuild.

**The Interpretation**  
Treat the override as relaxing only the deliverable artifact requirement. Keep the update domain model, package-signature verification, install history, rollback tracking, and update-management UI fully in scope. Allow the repository to use documented fixture packages or abstract package adapters during testing and static review.

**Proposed Implementation**  
Build an update subsystem around a signed package manifest abstraction with package metadata, signature verification, staging records, install history, and rollback targets. Keep the file adapter capable of validating `.msi` metadata when present, but do not treat the absence of a shipped signed `.msi` file as a scope failure. Document this explicitly in `docs/design.md` and `repo/README.md`.

## 10. Performance target verification boundary

**The Gap**  
The prompt sets measurable runtime targets for cold start, 7-day memory growth, and crash recovery, but it does not define the exact measurement harness, machine profile, or acceptance procedure.

**The Interpretation**  
Treat these targets as mandatory engineering goals that require instrumentation, observable metrics, and manual verification procedures. Do not claim they are proven by static code alone.

**Proposed Implementation**  
Add startup timing hooks, memory-sampling instrumentation, structured performance logs, and a documented manual verification plan under `docs/design.md` and `repo/README.md`. Create targeted tests around state restoration and leak-prone modules where static/integration coverage is possible, and leave final numeric verification as a documented manual check on a representative office PC.

**Status:** Confirmed. `Application` measures cold-start from constructor through `initialize()` completion using `QElapsedTimer`. `PerformanceObserver` records the elapsed ms to `performance_log` (event_type = `cold_start`) and logs a verdict ("within_target" / "exceeds_target") via `Logger`. Memory RSS is sampled every 60 seconds on Windows via `GetProcessMemoryInfo(PROCESS_MEMORY_COUNTERS)` and on Linux via `/proc/self/status VmRSS`, written to `performance_log` (event_type = `memory_sample`). A `shutdown` sample is also written by `stopMemoryObservation()`. Both targets are explicitly documented as requiring manual verification on a representative office PC — static code and CI runs do not prove the numeric goals. The manual verification procedure is documented in `docs/design.md §14.7`.

## 11. Signing key custody for exported sync packages

**The Gap**  
When a desk exports a `.proctorsync` package, it signs the manifest with an Ed25519 private key. The implementation does not specify how that private key is generated, where it lives, or how it is protected at rest on each desk instance.

**The Interpretation**  
Treat the signing private key for sync package export as a site-local per-desk key that is generated by the security administrator at first configuration. Storage uses the same DPAPI / `KeyStore` trust boundary used for the AES-256-GCM master key. The corresponding public key is distributed to peer desks through the SecurityAdminWindow → SyncWindow key import workflow (step-up gated).

**Proposed Implementation**  
Private key stored in `KeyStore` under a well-known entry name (`"sync.signing.private_key"`). Key generation is triggered from SyncWindow when no signing key entry exists. The public key half is displayed in the UI (hex-encoded) so the administrator can copy it to peer desks and import it via `SyncService::importSigningKey`. This is consistent with the offline-only constraint: no PKI server, no CA, explicit manual trust establishment.

**Status:** Confirmed. `Ed25519Signer::generateKeyPair()` is implemented in `src/crypto/Ed25519Signer.h/.cpp`. The `SyncWindow` import key flow accepts the DER-hex public key and registers it via `SyncService::importSigningKey` (step-up required). The private key custody pattern (KeyStore storage, per-desk key) is documented here and in `docs/design.md §21.1`. Revocation is via `SyncService::revokeSigningKey` (step-up required); revoked keys are blocked at import time by `PackageVerifier`.

## 12. GDPR / MLPS deletion request: single-step vs. two-step authorization

**The Gap**  
Data erasure under GDPR Art.17 is irreversible. The prompt required an approval workflow without specifying whether operator-initiation + admin-approval constitutes sufficient dual control, or whether a higher bar (e.g., SecurityAdministrator-initiated + additional witness) is required.

**The Interpretation**  
The compliance control boundary uses a two-step workflow: (1) any operator creates the deletion request with a written rationale; (2) a SecurityAdministrator approves it (step-up re-auth required); (3) the same SecurityAdministrator completes it with a confirmation dialog. No single action erases PII — three independent confirmations are required (create, approve, complete). This satisfies the principle of least surprise and provides a mandatory cooling-off period between request and execution.

**Status:** Confirmed. `DataSubjectService::completeDeletion` only transitions from `APPROVED` status; it cannot be called on a `PENDING` request. The `PENDING → APPROVED` transition requires step-up; `APPROVED → COMPLETED` requires a separate UI confirmation. All three steps are audit-logged. Tests in `tst_data_subject_service.cpp` and `tst_export_flow.cpp` verify the state machine boundaries and audit tombstone retention.

## 13. Privileged-action scope enforcement and test coverage

**The Gap**  
The prompt requires privileged-action scope boundaries (unmask, export fulfillment, deletion approval, freeze override, sync conflict resolution, role changes) to be statically auditable. The enforcement architecture spans three layers: AuthService (RBAC + step-up), services (state-machine + validation), and utilities (MaskingPolicy + ClipboardGuard). Test coverage must make these boundaries reviewer-visible without requiring the reviewer to read every service implementation.

**The Interpretation**  
Privileged-action scope is tested through dedicated `tst_privileged_scope` files in both `unit_tests/` and `api_tests/`. The unit test file systematically verifies all three enforcement layers: RBAC role hierarchy (6 tests), step-up mechanics (4 tests), PII masking defense-in-depth (4 tests), service state-machine boundaries (4 tests), and validation constraints (3 tests). The integration test file exercises full multi-step workflows across service boundaries with real database and audit chain. The full requirement-to-test mapping is documented in `docs/test-traceability.md`.

**Status:** Confirmed. Privileged services now enforce RBAC and step-up at the service boundary, including correction approval/rejection in `CheckInService`, data-subject fulfillment/rejection and deletion approval/completion/rejection in `DataSubjectService`, and compliance/sync/update workflows. Windows still gather actor identity and step-up window id, but service entrypoints validate role, actor ownership, expiry, and single-use consumption through `AuthService`.

## 14. Docker/Linux KeyStore security boundary for build and test execution

**The Gap**  
The production runtime targets Windows 11 DPAPI for master key protection and per-desk Ed25519 signing key storage. Docker runs on Linux, where DPAPI is unavailable. The KeyStore Linux fallback must be documented explicitly so reviewers understand the test-vs-production security boundary.

**The Interpretation**  
Docker is used exclusively for build verification and headless test execution — it is not the production security environment. The Linux fallback in `KeyStore` uses a file-permission-protected key file with XOR obfuscation, which is sufficient for the test corpus (all tests use ephemeral in-memory SQLite and do not exercise DPAPI-specific behavior). The security-equivalent implementation is DPAPI on Windows 11.

**Proposed Implementation**  
The distinction is explicitly documented in:
- `docs/questions.md §6` (encryption key custody)
- `repo/desktop/Dockerfile` (Docker boundary header comment)
- `repo/README.md` (Configuration → Key material storage, Building and Testing with Docker)

No runtime code change is required. Reviewers must not interpret Docker test passes as proof of DPAPI-equivalent key protection.

**Status:** Confirmed. `KeyStore` implements `CryptProtectData`/`CryptUnprotectData` on Windows and an XOR-obfuscated file fallback on Linux. The fallback is guarded by `#ifdef Q_OS_WIN` / `#else` and documented as NOT security-equivalent. The Dockerfile header and README both state this boundary explicitly.
