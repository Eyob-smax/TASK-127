# ProctorOps — Design Document

> **Project type:** Desktop_app
> **Revision:** Audit remediation alignment — security hardening, contract updates, and static readiness synchronization
> **Last updated:** 2026-04-14

---

## 1. Project Classification

ProctorOps is a **Windows 11 offline-native Qt desktop console**. It is classified as a **Desktop_app**. There is no web frontend, no remote backend, no SaaS dependency, no cloud queue, and no hosted identity provider of any kind.

The three business pillars that shape every design decision:

1. **Exam content governance** — question bank, KnowledgePoint chapter trees, ingestion scheduling, combined query builder
2. **On-site entry validation** — barcode/member/mobile check-in, term-card validation, punch-card deduction, 30-second duplicate suppression
3. **Compliance-grade traceability** — hash-chained audit log, RBAC, GDPR/MLPS export and deletion, update/rollback recordkeeping

---

## 2. Signed `.msi` Delivery Override

> **Override in effect:** Delivery of this project does **not** require a signed `.msi` installer artifact. This override was established at project inception and is recorded here and in `repo/README.md`.

The following remain **fully in scope and must be implemented**:

- Update/rollback domain model and workflow logic
- Signed package manifest verification (Ed25519 signature over a canonical manifest + file digests)
- Staged install records, install history, and rollback targets in SQLite
- Update management UI (operator selects package file, system verifies signature, prompts to apply, records result)
- Rollback UI (operator selects a prior install record, system reinstates prior state, records audit event)

The relaxation applies **only** to the final delivery artifact: a shipped signed `.msi` file is not required for project completion. All update logic, tests, and documentation remain required. The absence of a signed `.msi` at delivery is not a scope failure.

### 2.1 Building a Signed `.msi` Installer

For deployments that require a signed Windows installer package, follow these steps after a successful CMake build:

**Prerequisites:**
- [WiX Toolset v4+](https://wixtoolset.org/) (or WiX v3 with `candle`/`light`)
- A code-signing certificate (EV or standard, `.pfx` format) issued by a trusted CA
- `signtool.exe` from the Windows SDK

**Steps:**

1. **Build the release binary:**
   ```
   cmake --preset release -S repo/desktop -B build-release
   cmake --build build-release --config Release
   ```

2. **Run `windeployqt` to collect Qt runtime dependencies:**
   ```
   windeployqt --release build-release/ProctorOps.exe
   ```

3. **Author a WiX manifest** (`installer/ProctorOps.wxs`) listing:
   - `ProctorOps.exe` and all deployed Qt DLLs/plugins
   - `database/migrations/` folder (embedded as application data)
   - Start-menu shortcut, uninstall entry, upgrade GUID

4. **Compile and link the `.msi`:**
   ```
   wix build installer/ProctorOps.wxs -o build-release/ProctorOps.msi
   ```

5. **Sign the `.msi` with your code-signing certificate:**
   ```
   signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 ^
       /f certs/your-cert.pfx /p <password> build-release/ProctorOps.msi
   ```

6. **Verify the signature:**
   ```
   signtool verify /pa build-release/ProctorOps.msi
   ```

The resulting `ProctorOps.msi` can then be distributed and imported via the update workflow. The existing `UpdateService` verifies Ed25519 package signatures at the application level; the `.msi` Authenticode signature provides OS-level tamper protection on top of that.

---

## 3. Technology Stack

| Component | Choice | Target version |
|---|---|---|
| Language | C++ | C++20 |
| Build system | CMake + Ninja | CMake 3.28+ |
| UI framework | Qt Widgets | Qt 6.5+ |
| Qt modules | Core, Widgets, Sql, Concurrent, StateMachine, Test, SystemTray | Qt 6.5+ |
| Persistence | SQLite in WAL mode | SQLite 3.39+ |
| Password hashing | Argon2id | libargon2 (latest stable) |
| Field encryption | AES-256-GCM | OpenSSL 3.x |
| Package signing | Ed25519 detached signatures | OpenSSL 3.x |
| Key derivation | HKDF-SHA256 | OpenSSL 3.x |
| Entropy | OS-backed (CryptGenRandom on Windows; /dev/urandom in Docker) | — |
| Test runner | CTest + Qt Test | CMake 3.28+ |
| Container | Docker + docker-compose | authoring/CI readiness only |

---

## 4. Windows 11 Runtime Target

- **Platform:** Windows 11 (x86_64, 64-bit only).
- **Network:** Offline LAN/USB only. No internet connectivity. TLS is not used.
- **Hardware:** USB HID barcode scanners, standard keyboard + mouse, optional dual-monitor setup.
- **Performance targets (mandatory engineering goals):**
  - Cold-start: < 3 seconds on a representative office PC (8 GB RAM, NVMe SSD, Windows 11).
  - Continuous operation: 7 days without memory growth exceeding 200 MB above baseline.
  - Crash recovery: automatic state restoration on next launch via WAL + checkpoint state.
- **Deployment:** manual or silent command-line install from local media. No network delivery.
- **Docker role:** Docker is used for reproducible build and test execution only. The production runtime is a native Windows 11 desktop application. Docker is not the production environment.

---

## 5. Offline-Only Operating Assumptions

- No internet endpoints are accessed at any time.
- TLS is not configured or required (no internet endpoints exist to protect).
- All inter-desk data exchange uses physically transported media: UNC LAN share (`\\server\share`) or USB drive.
- Signed package files (`.proctorsync` for sync, `.proctorpkg` for updates) are the only external data ingestion paths.
- Key trust is established at installation time or through a security-administrator key-import workflow.
- All user credentials are stored locally with Argon2id hashing. No remote identity provider or SSO is used.
- Audit retention is set to 3 years by default; configurable by a security administrator.

---

## 6. Native Desktop Architecture

### 6.1 Layer Separation

```
┌─────────────────────────────────────────────────────────────┐
│  Application Shell                                          │
│  windows/ · dialogs/ · widgets/ · tray/                     │
│  Qt 6 Widgets — keyboard-first, multi-window, system tray   │
├─────────────────────────────────────────────────────────────┤
│  Feature Services  (services/)                              │
│  Business rules — no SQL, no direct Qt widget dependencies  │
├─────────────────────────────────────────────────────────────┤
│  Repositories  (repositories/)                              │
│  SQLite data access — no business logic, no UI calls        │
├─────────────────────────────────────────────────────────────┤
│  Background Workers  (scheduler/)                           │
│  Qt Concurrent — ingestion jobs, retry, resumable checkpoint│
├─────────────────────────────────────────────────────────────┤
│  Crypto / Security  (crypto/)                               │
│  Argon2id · AES-256-GCM · Ed25519 · HKDF · KeyStore        │
├─────────────────────────────────────────────────────────────┤
│  Audit Chain  (audit/)                                      │
│  Immutable SHA-256 hash-chained audit records               │
├─────────────────────────────────────────────────────────────┤
│  Shared Utilities  (utils/)                                 │
│  Logger · ClipboardGuard · MaskingPolicy · CaptchaGenerator │
│  ErrorFormatter · Migration runner                          │
└─────────────────────────────────────────────────────────────┘
              │  Qt SQL (QSQLITE driver)
              ▼
   SQLite 3.39+  (WAL mode, single DB file per installation)
```

### 6.2 Source Module Map

```
repo/desktop/src/
├── app/
│   ├── Application.h/.cpp          # ✅ QApplication subclass; cold-start timing, crash-recovery, migration runner
│   ├── AppSettings.h/.cpp          # ✅ Persistent settings (QSettings-backed, no secrets stored here)
│   ├── ActionRouter.h/.cpp         # ✅ Named action registry; shortcut dispatch; command palette backing
│   └── WorkspaceState.h/.cpp       # ✅ Workspace state persistence; crash-recovery window/job restoration
│
├── windows/
│   ├── MainShell.h/.cpp            # ✅ Workspace controller; MDI area; Ctrl+K/F/L; context menus
│   ├── LoginWindow.h/.cpp          # ✅ Authentication window; CAPTCHA integration; first-admin bootstrap provisioning
│   ├── CheckInWindow.h/.cpp        # ✅ Entry validation desk; barcode, member ID, mobile input
│   ├── QuestionBankWindow.h/.cpp   # ✅ Question bank editor; query builder; KP mapping
│   ├── AuditViewerWindow.h/.cpp    # ✅ Hash-chained audit log viewer; chain verification indicator
│   ├── SyncWindow.h/.cpp           # ✅ Offline sync: package export/import, conflict resolution, key management
│   ├── DataSubjectWindow.h/.cpp    # ✅ GDPR/MLPS export and deletion request management
│   ├── SecurityAdminWindow.h/.cpp  # ✅ Role management, account freezes, privileged audit (step-up gated)
│   ├── UpdateWindow.h/.cpp         # ✅ Update package staging, apply, rollback (.msi override banner)
│   └── IngestionMonitorWindow.h/.cpp # ✅ Ingestion job monitoring with 5-second auto-refresh
│
├── dialogs/
│   ├── CommandPaletteDialog.h/.cpp      # ✅ Ctrl+K command palette; keyboard-only fuzzy action search
│   ├── StepUpDialog.h/.cpp              # Step-up re-authentication (2-minute window)
│   ├── CaptchaDialog.h/.cpp             # Locally rendered CAPTCHA challenge
│   ├── ConflictResolutionDialog.h/.cpp  # Sync conflict review and resolution
│   ├── CorrectionRequestDialog.h/.cpp   # Supervisor override / correction approval
│   └── ExportRequestDialog.h/.cpp       # GDPR/MLPS export and deletion request initiation
│
├── widgets/
│   ├── MaskedFieldWidget.h/.cpp    # ✅ PII-masked display (last 4 digits default, step-up to reveal)
│   ├── BarcodeInput.h/.cpp         # ✅ USB HID barcode scanner via keystroke timing detection
│   └── AuditTableWidget.h/.cpp     # Audit log table with hash-chain integrity indicators
│
├── services/
│   ├── AuthService.h/.cpp          # ✅ Sign-in, lockout, CAPTCHA, RBAC, step-up, console lock
│   ├── AuditService.h/.cpp         # ✅ Audit event recording, hash chain, PII encryption
│   ├── PackageVerifier.h/.cpp      # ✅ Ed25519 signature + SHA-256 digest verification
│   ├── CheckInService.h/.cpp       # ✅ Term-card validation, punch-card deduction, dedup, corrections
│   ├── QuestionService.h/.cpp      # ✅ Question CRUD, query builder, KP mapping, tag management
│   ├── IngestionService.h/.cpp     # ✅ Import pipeline: JSONL/CSV, 3-phase, checkpoint resume
│   ├── SyncService.h/.cpp          # ✅ Sync package export/import, conflict detection, signing key management
│   ├── UpdateService.h/.cpp        # ✅ Update package import, staging, apply, rollback (.msi override applies)
│   └── DataSubjectService.h/.cpp   # ✅ GDPR/MLPS export request, deletion request, PII anonymization
│
├── repositories/
│   ├── UserRepository.h/.cpp            # ✅ Users, credentials, sessions, lockout, CAPTCHA, step-up
│   ├── AuditRepository.h/.cpp           # ✅ Append-only audit chain, export/deletion requests
│   ├── SyncRepository.h/.cpp            # ✅ Sync packages, entities, conflicts, trusted signing keys
│   ├── MemberRepository.h/.cpp          # ✅ 17 methods: members, term/punch cards, freezes, encrypted PII
│   ├── CheckInRepository.h/.cpp         # ✅ Attempts, deductions, corrections, sync materialization helpers
│   ├── QuestionRepository.h/.cpp        # ✅ 15 methods: questions, KP/tag mappings, query builder
│   ├── KnowledgePointRepository.h/.cpp  # ✅ 6 methods: KP tree, materialized path propagation
│   ├── IngestionRepository.h/.cpp       # ✅ 18 methods: jobs, deps, checkpoints, worker claims
│   └── UpdateRepository.h/.cpp         # ✅ Update packages, components, install history, rollback records
│
├── crypto/                          # ✅ All implemented
│   ├── SecureRandom.h/.cpp         # OS-backed secure random bytes (CryptGenRandom / /dev/urandom)
│   ├── Argon2idHasher.h/.cpp       # Argon2id password hashing; time=3, mem=65536, para=4
│   ├── AesGcmCipher.h/.cpp         # AES-256-GCM field encrypt/decrypt; versioned ciphertext payloads
│   ├── Ed25519Verifier.h/.cpp      # Detached Ed25519 signature verification for packages
│   ├── Ed25519Signer.h/.cpp        # ✅ Ed25519 signing for sync package export; key pair generation
│   ├── HashChain.h/.cpp            # SHA-256 audit chain hash computation; canonical serialization
│   ├── IKeyStore.h                 # Interface: master key retrieval and rotation
│   └── KeyStore.h/.cpp             # DPAPI (Windows) / file-based (Linux) master key management
│
├── scheduler/
│   └── JobScheduler.h/.cpp         # ✅ Priority-aware scheduling, deps, retry backoff, fairness, crash recovery
│
├── tray/
│   └── TrayManager.h/.cpp          # ✅ System tray icon, kiosk-mode check-in, lock state, operator-safe exit
│
└── utils/                           # ✅ All implemented
    ├── Logger.h/.cpp               # Structured JSON-lines log; auto-scrubs PII from context fields
    ├── ClipboardGuard.h/.cpp       # Intercepts clipboard writes; masks/redacts PII before OS clipboard
    ├── MaskingPolicy.h/.cpp        # Centralized masking rules: mobile, barcode, name, generic
    ├── CaptchaGenerator.h/.cpp     # Locally rendered CAPTCHA image generation with QPainter
    ├── ErrorFormatter.h/.cpp       # Maps ErrorCode to user-facing messages and form hints
    ├── Migration.h/.cpp            # ✅ Sequential SQLite migration runner; schema_migrations table
    └── PerformanceObserver.h/.cpp  # ✅ Cold-start timing; periodic RSS sampling; performance_log table
```

---

## 7. SQLite Database Design

### 7.1 Operational Constraints

- Single SQLite file per installation: `proctorops.db` (path configurable via `AppSettings`).
- WAL (Write-Ahead Logging) mode enabled on first `PRAGMA journal_mode=WAL` at startup.
- `PRAGMA foreign_keys = ON` enforced at every connection open.
- `PRAGMA synchronous = NORMAL` for WAL mode (safe and performant).
- Migrations applied sequentially at startup via `Migration` before any service initializes.
- Backup: the export workflow produces a sealed snapshot. No automatic network backup.
- Retention: audit records kept for 3 years minimum (configurable by security admin, not reducible below 1 year without explicit override).

### 7.2 Schema Domains

| Domain | Tables (planned) |
|---|---|
| Identity and auth | `users`, `credentials`, `lockout_records`, `captcha_states`, `step_up_windows`, `user_sessions` |
| Member and entitlement | `members`, `term_cards`, `punch_cards`, `member_freeze_records` |
| Check-in | `checkin_attempts`, `deduction_events`, `correction_requests`, `correction_approvals` |
| Question governance | `questions`, `knowledge_points`, `question_kp_mappings`, `question_tags`, `tags` |
| Ingestion | `ingestion_jobs`, `job_dependencies`, `job_checkpoints`, `worker_claims` |
| Sync and packages | `sync_packages`, `sync_package_entities`, `conflict_records` |
| Audit | `audit_entries`, `audit_chain_head` |
| Crypto and trust | `key_store_entries`, `trusted_signing_keys` |
| Update and rollback | `update_packages`, `install_history`, `rollback_records` |
| Export and compliance | `export_requests`, `deletion_requests`, `redaction_records` |

### 7.3 Migration Strategy

- Each migration is a numbered SQL file: `database/migrations/NNNN_description.sql`.
- The `Migration` runner applies all pending migrations in order, inside a transaction per migration.
- A `schema_migrations` table records applied migration IDs and their applied-at timestamps.
- Rollback migration files are authored alongside forward migrations wherever destructive changes occur.
- Seed fixture files for testing live in `database/fixtures/` and are applied only in test mode.

---

## 8. Multi-Window Shell

`MainShell` manages multiple top-level or docked workspace windows:

| Window | Role | Default user |
|---|---|---|
| `CheckInWindow` | Entry validation desk | Front-desk operator |
| `QuestionBankWindow` | Question governance editor | Content manager |
| `AuditViewerWindow` | Audit log browser | Proctor / security admin |

Windows can be tiled side-by-side, detached as independent top-level windows, or minimized independently. `MainShell` owns and enforces the global keyboard shortcuts:

| Shortcut | Action |
|---|---|
| `Ctrl+L` | Lock console (requires password to unlock; records audit event) |
| `Ctrl+K` | Open command palette (`CommandPaletteDialog`) |
| `Ctrl+F` | Open advanced filter panel (context-sensitive to the active window) |

Context menus are implemented per feature area:
- Question row → "Map to Knowledge Point", "Set Tag", "Set Difficulty", "Export Question"
- Member row → "Mask PII", "Export for Request", "View Check-in History"
- Audit row → "Verify Chain at This Point", "Export Audit Range"

---

## 9. Kiosk / System Tray Mode

When the console is minimized to tray:
- The main window hides; `TrayManager` shows a system tray icon.
- The tray context menu provides: "Open Check-in Desk", "Lock Console", "Quit".
- A dedicated kiosk layout strips the menu bar and toolbar, leaving only the check-in input panel.
- In kiosk mode, barcode scanner input is captured globally while the kiosk window has focus (USB HID input appears as keyboard events on the active focused widget).
- The kiosk window cannot be minimized or resized below a minimum operational size.

---

## 10. Sync Package and Export-Import Architecture

Offline multi-desk sync uses signed package files transported on LAN share or USB:

```
[Source Desk]                               [Target Desk]
SyncPackageService.exportPackage()          SyncPackageService.importPackage()
  ↓                                           ↓
Collect entity delta                        Read .proctorsync bundle
  (check-ins, corrections, edits            Verify Ed25519 signature
   since last sync watermark)                 against local trust store
  ↓                                           ↓
Build canonical JSON manifest               Validate entity payloads
  with SHA-256 digest per entity               against manifest digests
  ↓                                           ↓
Sign manifest + digests with               Conflict-classify entities:
  local Ed25519 private key                  • append-only → auto-merge
  ↓                                          • mutable conflict → flag
Write .proctorsync bundle                    ↓
  to output path (LAN/USB)               Present ConflictResolutionDialog
                                            for unresolved conflicts
                                            ↓
                                         Apply accepted entities in one
                                           SQLite transaction
                                            ↓
                                         Write audit entries for each
                                           applied entity
```

Corrections that override prior deductions require:
1. Supervisor (security administrator) step-up authentication.
2. Explicit rationale text entry.
3. Before/after diff persisted in `correction_approvals` with audit hash link.

---

## 11. Audit Chain

Each audit entry contains:

| Field | Type | Description |
|---|---|---|
| `id` | UUID (TEXT) | Unique entry identifier |
| `timestamp` | TEXT (ISO-8601 UTC) | Event time |
| `actor_user_id` | TEXT | Authenticated user who triggered the event |
| `event_type` | TEXT (enum) | LOGIN, LOGOUT, CHECKIN, CONTENT_EDIT, ADMIN_ACTION, SYNC_IMPORT, CORRECTION_APPLY, UPDATE_APPLY, EXPORT, DELETION, CHAIN_VERIFY |
| `entity_type` | TEXT | Affected entity class (Question, Member, CheckIn, etc.) |
| `entity_id` | TEXT | Affected entity identifier |
| `before_payload` | TEXT (JSON) | Field-level diff — values before change; PII fields AES-GCM encrypted |
| `after_payload` | TEXT (JSON) | Field-level diff — values after change; PII fields AES-GCM encrypted |
| `previous_entry_hash` | TEXT (hex SHA-256) | Hash of the previous audit entry's canonical form |
| `entry_hash` | TEXT (hex SHA-256) | SHA-256 of this entry's canonical form (including `previous_entry_hash`) |

`AuditChain.verifyChain()` recomputes all hashes from the earliest entry forward and flags any gap, mutation, or hash mismatch.

---

## 12. Encryption Strategy

### 12.1 Field Encryption (AES-256-GCM)

- Sensitive fields (mobile number, member identifiers, audit PII payloads) are encrypted before storage.
- A per-installation master key is generated on first run and protected using OS-backed local secret storage (Windows DPAPI on Windows; OS keyring or file-permission-protected file in Docker/CI).
- Per-record data-encryption keys (DEKs) are derived from the master key using HKDF-SHA256 with a unique 16-byte random salt per record.
- Stored ciphertext format: `version(1) | salt(16) | nonce(12) | ciphertext | tag(16)` — all hex-encoded in the TEXT column.
- Key rotation: the `KeyStore` supports a batch re-encryption job that derives new DEKs from the new master key and re-encrypts all affected records with checkpoint state.

### 12.2 Password Hashing (Argon2id)

- All passwords hashed with Argon2id. No plaintext, reversible encryption, or MD5/SHA-1 hashes.
- Default parameters: `t=3` (time cost), `m=65536` (64 MB memory), `p=4` (parallelism), `tag=32` bytes.
- Stored credential record: `{ algorithm: "argon2id", version: 19, t, m, p, salt_hex, hash_hex }`.
- Parameters are tunable by a security administrator within the settings module.

### 12.3 Clipboard Redaction

- `ClipboardGuard` subclasses or intercepts Qt clipboard write signals.
- Before any value reaches the OS clipboard, it is scanned for PII patterns (mobile number `(###) ###-####`, member ID patterns, raw UUID-like identifiers).
- Detected PII is replaced with the masked form (e.g., `(***) ***-1234`) before writing to the OS clipboard.
- Raw PII never reaches the OS clipboard under any user-triggerable action.

### 12.4 Trust Boundaries

The security architecture defines three trust boundaries:

1. **User ↔ Service boundary:** All user actions pass through `AuthService` for session validation and role checking before reaching business services. RBAC is enforced at the service layer, never only at the UI layer.

2. **Service ↔ Repository boundary:** Services own business rules (lockout logic, hash chain computation, PII encryption). Repositories handle SQL only. No raw PII or plaintext secrets cross into repository parameters without prior encryption.

3. **External package ↔ Local trust store boundary:** All imported packages (`.proctorsync`, `.proctorpkg`) must pass Ed25519 signature verification against the local `trusted_signing_keys` table. Keys are checked for revocation and expiry before any verification proceeds. `PackageVerifier` enforces this boundary for both sync and update packages.

### 12.5 Audit Chain Canonical Serialization

Each `AuditEntry` is hashed using a canonical pipe-delimited format:

```
id|timestamp(ISO8601)|actorUserId|eventType(string)|entityType|entityId|beforePayloadJson|afterPayloadJson|previousEntryHash
```

The SHA-256 of this canonical form becomes the `entry_hash`. The `previous_entry_hash` field links to the prior entry, forming a tamper-evident chain. PII fields within before/after payloads are AES-256-GCM encrypted before hashing and storage.

---

## 13. Ingestion Scheduler

The scheduler manages file-system-based import jobs:

| Property | Value |
|---|---|
| Job types | `QUESTION_IMPORT`, `ROSTER_IMPORT` |
| Triggers | Manual (operator-initiated) or scheduled (per-job-type cron schedule) |
| Concurrency | 2 workers active simultaneously (configurable, min 1, max 8) |
| Phase sequence | `VALIDATE → IMPORT → INDEX` (distinct phases; each has its own checkpoint) |
| Retry policy | Exponential backoff: 5 s → 30 s → 2 min. Maximum 5 attempts per phase. |
| Failure isolation | A failed job does not block or cancel sibling jobs in the queue. |
| Dependency chains | Each job specifies prerequisite job IDs that must reach `COMPLETED` state before it runs. |
| Checkpoint resume | Checkpoint offset persisted after each committed batch; restart resumes from last offset. |

Job state machine: `PENDING → CLAIMED → VALIDATING → IMPORTING → INDEXING → COMPLETED | FAILED`

---

## 14. Crash Recovery

- On launch, `Application` writes a startup record to SQLite with a `started_at` timestamp and no `clean_shutdown_at` value.
- On clean exit, the shutdown timestamp is written.
- On next launch: if a startup record exists without a matching shutdown record, crash recovery mode activates.
- Recovery actions:
  - Mark all `CLAIMED` or in-progress ingestion jobs as `INTERRUPTED` (not `FAILED`; retains checkpoint).
  - Reload last checkpoint offsets for interrupted jobs.
  - Release any stale worker claims.
  - Present a recovery notice in the UI listing affected jobs.
- WAL mode ensures SQLite file is consistent after abrupt process termination.

---

## 15. Update / Rollback Logic

```
[Operator action: import update package from disk]
UpdateService.importPackage(filePath)
  ↓
Verify Ed25519 signature against local trust store
  (fail closed if signature missing, key unknown, or digest mismatch)
  ↓
Parse package manifest: version, target, components, checksums
  ↓
Stage package to temporary location; verify component digests
  ↓
Prompt operator: apply, defer, or cancel
  ↓ (apply)
Snapshot current install record to install_history
Apply update components to local runtime live directory
  with per-component backup in update_runtime/history/<install_history_id>
Post-apply digest verification on deployed live files
Write update_packages record (package_id, version, applied_at, status=APPLIED)
Write audit entry (event_type=UPDATE_APPLY)
  ↓ (rollback, operator selects prior install_history record)
Restore prior component set from install_history snapshot backup paths
Write rollback_records entry (from_version, to_version, rolled_back_at, actor)
Write audit entry (event_type=UPDATE_ROLLBACK)
```

---

## 16. Performance Instrumentation

- `Application` measures wall-clock time from `main()` entry to `MainShell` first-paint and logs it as a structured JSON event at startup.
- A background memory-sampling timer logs RSS/private bytes every 10 minutes to the structured log; alert if growth exceeds 20 MB per hour.
- These logs feed a manual verification plan documented in `repo/README.md` (cold-start < 3 s, 7-day < 200 MB).
- No external profiling tool is required; log analysis on the structured log file is sufficient for acceptance.

---

## 17. Requirement-to-Module Traceability

| Requirement (from original prompt) | Primary module(s) | Test target(s) |
|---|---|---|
| Local sign-in, 12-char min password | `AuthService`, `LoginWindow`, `UserRepository` | `unit: tst_auth_service`, `api: tst_auth_integration` |
| Lockout after 5 failures in 10 min | `AuthService` | `unit: tst_auth_service`, `api: tst_auth_integration` |
| Local CAPTCHA after 3rd failure | `AuthService`, `CaptchaGenerator` | `unit: tst_auth_service`, `api: tst_auth_integration` |
| 15-min CAPTCHA cool-down | `AuthService` | `unit: tst_auth_service` |
| Global shortcuts Ctrl+F/K/L | `MainShell`, `ActionRouter` | `unit: tst_action_routing`, `unit: tst_tray_mode` |
| Multi-window layout | `MainShell`, `CheckInWindow`, `QuestionBankWindow`, `AuditViewerWindow` | `unit: tst_checkin_window`, `unit: tst_question_editor`, `unit: tst_audit_viewer` |
| Right-click context menus | All feature windows | exercised via `unit: tst_tray_mode` (MainShell wiring) |
| Clipboard PII auto-redaction | `ClipboardGuard` | `unit: tst_clipboard_guard`, `unit: tst_privileged_scope` |
| System tray / kiosk mode | `TrayManager`, `MainShell` | `unit: tst_tray_mode` |
| Barcode scanner input (USB HID) | `BarcodeInput`, `CheckInService` | `unit: tst_checkin_service`, `api: tst_checkin_flow` |
| Manual member ID entry | `CheckInWindow`, `CheckInService` | `unit: tst_checkin_service`, `api: tst_checkin_flow` |
| Mobile number entry `(###) ###-####` | `CheckInService`, `CheckInWindow` | `unit: tst_checkin_service`, `api: tst_checkin_flow` |
| Term-card date range validation | `CheckInService`, `MemberRepository` | `unit: tst_checkin_service`, `api: tst_checkin_flow` |
| Expired/frozen account blocking | `CheckInService`, `MemberRepository` | `unit: tst_checkin_service`, `api: tst_checkin_flow` |
| Atomic punch-card deduction | `CheckInService`, `CheckInRepository` | `unit: tst_checkin_service`, `api: tst_checkin_flow` |
| 30-second duplicate suppression | `CheckInService`, `CheckInRepository` | `unit: tst_checkin_service`, `api: tst_checkin_flow` |
| Signed LAN/USB sync packages | `SyncService`, `Ed25519Verifier`, `SyncRepository` | `unit: tst_sync_service`, `api: tst_sync_import_flow`, `api: tst_package_verification` |
| Conflict detection and review | `SyncService`, `ConflictResolutionDialog` | `unit: tst_sync_service`, `api: tst_sync_import_flow` |
| Supervisor override for corrections | `CheckInService`, `AuthService` | `unit: tst_checkin_service`, `api: tst_correction_flow`, `api: tst_privileged_scope_api` |
| Question bank CRUD and tags | `QuestionService`, `QuestionRepository` | `unit: tst_question_service`, `api: tst_operator_workflows` |
| KnowledgePoint chapter tree | `QuestionService`, `KnowledgePointRepository` | `unit: tst_question_service` |
| Difficulty (1–5) and discrimination (0.00–1.00) | `QuestionRepository`, `QuestionService` | `unit: tst_question_service` |
| Combined query builder | `QuestionService`, `QuestionRepository` | `unit: tst_question_service`, `api: tst_operator_workflows` |
| Ingestion scheduler (jobs, deps, priorities) | `JobScheduler`, `IngestionRepository` | `unit: tst_job_scheduler`, `unit: tst_job_checkpoint` |
| Exponential retry backoff | `JobScheduler` | `unit: tst_job_scheduler` |
| Checkpoint resume after restart | `JobScheduler`, `IngestionService` | `unit: tst_job_checkpoint`, `unit: tst_job_scheduler` |
| Failure isolation | `JobScheduler`, `IngestionService` | `unit: tst_job_scheduler` |
| AES-256-GCM field encryption | `AesGcmCipher`, `KeyStore` | `unit: tst_crypto`, `api: tst_audit_integration` |
| HKDF-SHA256 key derivation | `AesGcmCipher` | `unit: tst_crypto` |
| Masked PII display (last 4 digits) | `MaskedFieldWidget`, `MaskingPolicy` | `unit: tst_masked_field`, `unit: tst_privileged_scope` |
| RBAC (4 roles) | `AuthService`, all services | `unit: tst_auth_service`, `unit: tst_privileged_scope`, `api: tst_auth_integration` |
| Step-up verification (2-min window) | `StepUpDialog`, `AuthService` | `unit: tst_auth_service`, `unit: tst_privileged_scope`, `api: tst_auth_integration` |
| Hash-chained audit log | `HashChain`, `AuditService`, `AuditRepository` | `unit: tst_audit_chain`, `unit: tst_crypto`, `api: tst_audit_integration` |
| 3-year audit retention | `AuditService`, `AuditRepository` | `unit: tst_audit_chain` |
| GDPR/MLPS export/deletion | `DataSubjectService`, `DataSubjectWindow` | `unit: tst_data_subject_service`, `api: tst_export_flow`, `api: tst_privileged_scope_api` |
| Cold-start < 3 s | `Application` (startup timer), `PerformanceObserver` | manual verification on Windows 11 hardware |
| 7-day continuous run < 200 MB | `Application` (memory sampling), `PerformanceObserver` | manual verification on Windows 11 hardware |
| Crash recovery | `Application`, `WorkspaceState`, `JobScheduler` | `unit: tst_crash_recovery`, `unit: tst_workspace_state`, `api: tst_shell_recovery` |
| Update/rollback workflow | `UpdateService`, `UpdateRepository` | `unit: tst_update_service`, `api: tst_update_flow`, `api: tst_privileged_scope_api` |
| Signed package verification | `PackageVerifier`, `Ed25519Verifier`, `Ed25519Signer` | `unit: tst_crypto`, `api: tst_package_verification`, `api: tst_sync_import_flow` |
| Argon2id password hashing | `Argon2idHasher` | `unit: tst_crypto`, `api: tst_auth_integration` |
| SQLite WAL persistence + migrations | `Migration`, all repositories | `unit: tst_migration`, `api: tst_schema_constraints` |
| Master key custody and rotation | `KeyStore` | `unit: tst_crypto` |
| Import file validation (CSV/JSON) | `IngestionService` | `unit: tst_ingestion_service`, `api: tst_operator_workflows` |
| Bootstrap administrator provisioning | `AuthService`, `LoginWindow` | `unit: tst_auth_service`, `api: tst_auth_integration` |
| Config defaults and settings persistence | `AppSettings` | `unit: tst_app_settings` |

---

## 18. Sequence Flows

### 18.1 Sign-in → Lockout → CAPTCHA → Unlock

```
[User enters username + password]
  ↓
AuthService.login(username, password)
  → LockoutRepository: getLockoutRecord(username)
  → Check: lockedAt is set AND within 10-minute window? → Err(ACCOUNT_LOCKED)
  → Check: failedAttempts >= 3?
      → Yes: CaptchaState required (if missing, fail-closed and regenerate challenge)
          → user provided captchaSolution?
              → No: Err(CAPTCHA_REQUIRED) — CaptchaDialog shown by LoginWindow
              → Yes: verify SHA-256(solution) == answerHashHex AND !expired?
                  → No: increment solveAttempts; Err(CAPTCHA_REQUIRED)
                  → Yes: mark captchaState.solved = true
  → Argon2idHasher.verify(password, credential.hashHex, credential.saltHex)
      → Fail: increment failedAttempts; upsert LockoutRecord
              if failedAttempts >= 5: set lockedAt = now
              Err(INVALID_CREDENTIALS or ACCOUNT_LOCKED)
      → Pass: clear LockoutRecord and CaptchaState
              insert UserSession; return Ok(session)

[Unlock — security admin only]
  AdminWindow → AuthService.unlockUser(adminSession, stepUpToken, userId, rationale)
  → verify stepUpToken valid and not consumed
  → UserRepository: updateUserStatus(userId, Active)
  → LockoutRepository: clearLockoutRecord(username)
  → CaptchaRepository: clearCaptchaState(username)
  → AuditService.record(UserUnlocked, ...)
  → consume stepUpToken
```

### 18.2 Check-in Validation and Atomic Punch Deduction

```
[Operator scans barcode / enters member ID or mobile]
  ↓
CheckInService.checkIn(sessionToken, identifier, sessionId)
  → AuthService: verify sessionToken, check FRONT_DESK_OPERATOR or higher role
  → resolve member: MemberRepository.findBy*(identifier.value)
      → Not found: Err(NOT_FOUND)
  → check freeze: MemberRepository.getActiveFreezeRecord(memberId)
      → Active freeze found: record CHECKIN_FROZEN_BLOCKED; Err(ACCOUNT_FROZEN)
  → check term card: MemberRepository.getActiveTermCards(memberId, today)
      → None valid: record CHECKIN_TERM_CARD_INVALID; Err(TERM_CARD_EXPIRED or MISSING)
  → check 30-second dedup:
      CheckInRepository.findRecentSuccess(memberId, sessionId, now - 30s)
      → Found: record CHECKIN_DUPLICATE_BLOCKED; Err(DUPLICATE_CHECKIN)
  → select punch card: MemberRepository.getActivePunchCards(memberId)
      → None with balance > 0: record CHECKIN_PUNCH_CARD_EXHAUSTED; Err(PUNCH_CARD_EXHAUSTED)
  → BEGIN TRANSACTION (exclusive)
      PunchCard = deductSession(punchCardId)  ← atomic; fails if balance = 0
      DeductionEvent = insertDeduction(memberId, punchCardId, ...)
      CheckInAttempt = insertAttempt(status=Success, deductionEventId)
  → COMMIT
  → AuditService.record(DEDUCTION_CREATED + CHECKIN_SUCCESS)
  → return Ok(CheckInResult)
```

### 18.3 Question Import and Indexing (Ingestion Phases)

```
[Operator or scheduler triggers import]
  ↓
IngestionService.createJob(sessionToken, CreateJobCmd{type=QuestionImport, filePath, ...})
  → ContentManager or higher role required
  → insert IngestionJob(status=Pending)
  → AuditService.record(JOB_CREATED)

[WorkerPool picks up Pending job whose dependencies are Completed]
  JobScheduler → poll for ready jobs (priority DESC)
  → claim job: WorkerClaim inserted (unique on job_id)
  → update status: Validating

[VALIDATE phase]
  IngestionService.executeValidatePhase(jobId)
  → open source file; parse JSON Lines line-by-line from checkpoint offset
  → for each row: validate schema, field ranges (difficulty 1-5, discrimination 0-1),
    required fields, answer_options count (2-6), external_id duplicate check
  → save checkpoint after each committed batch of rows
  → on error: increment retryCount; if >= 5: status=Failed; else schedule retry with backoff
  → on success: update status=Importing; reset checkpoint phase=Import

[IMPORT phase]
  IngestionService.executeImportPhase(jobId)
  → open file; seek to checkpoint offset
  → for each valid row: insert Question; insert KP mappings; insert tag mappings
    inside per-batch transactions
  → save checkpoint after each batch
  → on success: update status=Indexing; reset checkpoint phase=Index

[INDEX phase]
  IngestionService.executeIndexPhase(jobId)
  → rebuild or update full-text search index for affected question rows
  → mark job status=Completed; AuditService.record(JOB_COMPLETED)
```

### 18.4 Desk-to-Desk Deferred Sync and Conflict Correction

```
[Source desk — export]
  SyncPackageService.exportPackage(adminSession, outputPath, sinceWatermark)
  → collect delta: checkin_attempts, deduction_events, correction_requests,
    question edits since sinceWatermark
  → build canonical manifest.json with per-entity SHA-256 digests
  → sign manifest with local Ed25519 private key (from KeyStore)
  → zip entities/ + manifest.json + manifest.json.sig → .proctorsync
  → write to outputPath (USB or LAN share)
  → AuditService.record(SYNC_EXPORT)

[Target desk — import]
  SyncPackageService.importPackage(adminSession, stepUpToken, packagePath)
  → step-up token required (security admin only)
  → read .proctorsync; extract manifest.json and manifest.json.sig
  → verify Ed25519 signature vs trusted_signing_keys → fail closed on any error
  → verify SHA-256 of each entity file vs manifest digest
  → classify entities:
      append-only (checkins, deductions) → auto-merge (insert if not exists)
      mutable (corrections, content edits) → compare vs local; flag conflicts
  → DoubleDeduction detection: same member+session already deducted locally?
      → insert ConflictRecord(type=DoubleDeduction)
  → ConflictResolutionDialog: operator reviews each conflict
      → ResolvedAcceptLocal / ResolvedAcceptIncoming / ResolvedManualMerge
  → BEGIN TRANSACTION
      apply accepted entities
      resolve conflict records
  → COMMIT
  → AuditService.record(SYNC_IMPORT + SYNC_CONFLICT_RESOLVED per conflict)

[Correction — supervisor override]
  CheckInService.requestCorrection(deductionEventId, rationale, requestedByUserId)
  → insert CorrectionRequest(status=Pending)
  AuthService.initiateStepUp(adminSession, password) → StepUpWindow (2 min)
  CheckInService.approveCorrection(requestId, rationale, approvedByUserId, stepUpWindowId)
  → service validates role, actor ownership, expiry, and single-use consumption
  AuthService.initiateStepUp(adminSession, password) → StepUpWindow (2 min)
  CheckInService.applyCorrection(requestId, actorUserId, stepUpWindowId)
  → service validates role, actor ownership, expiry, and single-use consumption
  → snapshot before/after state
  → MemberRepository.restoreSession(punchCardId)  ← restore deducted balance
  → CheckInRepository.setDeductionReversed(deductionId, correctionId)
  → update CorrectionRequest status=Applied
  → AuditService.record(CORRECTION_APPLIED)
  → consume stepUpToken
```

### 18.5 Audit Export and Deletion Request Handling

```
[Security admin — data export request]
  ExportService.createExportRequest(adminSession, memberId, rationale)
  → SecurityAdministrator role required
  → insert ExportRequest(status=PENDING)
  → AuditService.record(EXPORT_REQUESTED)

  ExportService.fulfillExportRequest(adminSession, stepUpToken, requestId, outputPath)
  → step-up required; verify token
  → collect: Member record (decrypt PII), TermCards, PunchCards, CheckInAttempts,
    DeductionEvents, AuditEntries for this memberId
  → write JSON Lines to outputPath
  → update ExportRequest(status=COMPLETED, fulfilled_at, output_file_path)
  → AuditService.record(EXPORT_COMPLETED)

[Security admin — deletion request]
  ExportService.createDeletionRequest(adminSession, memberId, rationale)
  → insert DeletionRequest(status=PENDING)

  ExportService.approveDeletion(adminSession, stepUpToken, requestId)
  → verify step-up
  → anonymize: replace mobile_encrypted, name_encrypted, barcode_encrypted
    with fixed tombstone values; clear member_id lookup value
  → update Member(deleted=1)
  → update DeletionRequest(status=COMPLETED, fields_anonymized=[...])
  → AuditService.record(DELETION_COMPLETED)  ← audit tombstone retained
```

### 18.6 Update Import and Rollback State Transitions

```
[Operator selects .proctorpkg file]
  UpdateService.importPackage(adminSession, filePath)
  → read update-manifest.json and update-manifest.json.sig
  → verify Ed25519 signature vs trusted_signing_keys → Err(SIGNATURE_INVALID) if fail
  → verify each component SHA-256 → Err(PACKAGE_CORRUPT) if mismatch
  → stage components to temp directory
  → insert UpdatePackage(status=Staged, signature_valid=true)
  → insert UpdateComponent rows
  → return UpdatePackageMetadata for operator review

  UpdateService.applyPackage(stagedPackageId, currentVersion, actorUserId, stepUpWindowId)
  → service enforces SecurityAdministrator role + step-up
  → snapshot current component state → insert InstallHistoryEntry(snapshot_payload_json)
  → apply staged components (copy to install location)
  → update UpdatePackage(status=Applied)
  → AuditService.record(UPDATE_APPLIED)
  → consume stepUpToken

  UpdateService.rollback(adminSession, stepUpToken, installHistoryId, rationale)
  → step-up required
  → load snapshot from InstallHistoryEntry.snapshot_payload_json
  → reinstate prior component set from snapshot
  → insert RollbackRecord(from_version, to_version, rationale)
  → update UpdatePackage of the rolled-back version (status=RolledBack)
  → AuditService.record(UPDATE_ROLLED_BACK)
  → consume stepUpToken
```

---

## 19. Business Invariants

All invariants are authoritative in `src/utils/Validation.h`. Reproduced here for documentation:

| Invariant | Value | Enforced by |
|---|---|---|
| Password minimum length | 12 characters | `Validation::PasswordMinLength`, `AuthService` |
| Lockout failure threshold | 5 within 10 min | `Validation::LockoutFailureThreshold`, `AuthService` |
| CAPTCHA trigger | after 3rd failure | `Validation::CaptchaAfterFailures`, `AuthService` |
| CAPTCHA cool-down | 15 minutes | `Validation::CaptchaCooldownSeconds`, `AuthService` |
| Step-up window | 2 minutes, single-use | `Validation::StepUpWindowSeconds`, `AuthService` |
| Duplicate check-in window | 30 seconds | `Validation::DuplicateWindowSeconds`, `CheckInService` |
| Difficulty range | 1–5 | `Validation::DifficultyMin/Max`, SQLite CHECK, `QuestionService` |
| Discrimination range | 0.00–1.00 | `Validation::DiscriminationMin/Max`, SQLite CHECK, `QuestionService` |
| Mobile format | `(###) ###-####` | `Validation::isMobileValid()`, `CheckInService` |
| Scheduler workers | default 2 (min 1, max 8) | `Validation::SchedulerDefaultWorkers`, `WorkerPool` |
| Retry schedule | 5 s → 30 s → 2 min (max 5) | `Validation::RetryDelay*`, `RetryPolicy` |
| Audit retention | 3 years minimum | `Validation::AuditRetentionYears`, `AuditService` |
| Argon2id parameters | t=3, m=64MB, p=4 | `Validation::Argon2*`, `Argon2idHasher` |
| Punch-card balance | ≥ 0 | SQLite CHECK, `IMemberRepository.deductSession` |
| Term-card dates | end > start | SQLite CHECK, `TermCard` invariant |

---

## 14. Desktop Shell Architecture

### 14.1 Startup Sequence

```
main()
  │
  ├─ Application::Application()     — QApplication init, AppSettings construction
  ├─ Application::initialize()
  │     ├─ openDatabase()           — SQLite QSQLITE driver, WAL/FK/synchronous pragmas
  │     ├─ runMigrations()          — Migration::applyPending() over migrations/ directory
  │     ├─ detectCrash()            — SELECT unclosed app_lifecycle row → m_crashDetected
  │     ├─ recordSessionStart()     — INSERT app_lifecycle row (crash bait)
  │     ├─ WorkspaceState::load()   — read workspace_state (id=1) from SQLite
  │     └─ PerformanceObserver::recordColdStart()  — log elapsed ms to performance_log
  │
  ├─ ActionRouter()                 — central named-action registry
  ├─ MainShell()                    — QMainWindow + QMdiArea; registers shell actions
  ├─ TrayManager()                  — system tray icon, kiosk/lock menu
  ├─ MainShell::restoreWorkspace()  — re-open windows from WorkspaceState snapshot
  └─ Application::exec()            — Qt event loop
```

### 14.2 Crash Recovery

Crash detection uses a "crash bait" pattern in `app_lifecycle`:

1. On startup: `INSERT INTO app_lifecycle (started_at, app_version)` — no `clean_shutdown_at`.
2. On clean shutdown: `UPDATE app_lifecycle SET clean_shutdown_at = ? WHERE id = ?`.
3. On next launch: query for any `app_lifecycle` row with `clean_shutdown_at IS NULL`.
   - **Found → crash detected**: `Application::crashDetected()` returns `true`.
   - **Not found → clean**: normal startup.

When a crash is detected:
- `MainShell::setWarningIndicator(true, ...)` shows the alert in the status bar and tray.
- `WorkspaceState::snapshot()` provides the list of open windows, pending action markers,
  and interrupted ingestion job IDs that were active at the time of the crash.
- `MainShell::restoreWorkspace()` reopens the saved feature windows.
- The JobScheduler independently recovers interrupted ingestion jobs on next tick
  (via `findInProgressJobIds` → `markInterrupted` → re-enqueue).

WorkspaceState persists after every mutation (immediate `save()` on each call), so
the SQLite row reflects the last known good state even if the process exits abruptly.

### 14.3 Action Routing

`ActionRouter` is the single source of truth for all named application actions.

- **Registration**: feature modules call `registerAction(RegisteredAction{id, displayName, category, shortcut, requiresAuth, handler})` at startup.
- **Dispatch**: `ActionRouter::dispatch(id)` is the only callsite for action execution. Shortcuts, command palette, and context menus all go through `dispatch()`.
- **Command palette**: `CommandPaletteDialog` calls `router.filter(query)` to build its list. Filtering is case-insensitive and matches displayName, id, or category.
- **Context menus**: `MainShell::buildContextMenu()` injects shell-level actions; feature windows may inject their own domain actions via `registerAction()`.

Shell-level actions registered at startup:

| ID | Display | Shortcut |
|---|---|---|
| `shell.lock` | Lock Console | Ctrl+L |
| `shell.command_palette` | Open Command Palette | Ctrl+K |
| `shell.advanced_filter` | Advanced Filter | Ctrl+F |
| `window.checkin` | Open Check-In Desk | — |
| `window.questionbank` | Open Question Bank | — |
| `window.auditviewer` | Open Audit Viewer | — |
| `content.map_to_kp` | Map to Knowledge Point | — |
| `member.mask_pii` | Mask PII | — |
| `member.export_request` | Export for Request | — |

### 14.4 Workspace Model

The MDI workspace uses `QMdiArea` in tabbed view mode (`QMdiArea::TabbedView`).
Feature windows open as `QMdiSubWindow` instances. Each sub-window is identified by
an `objectName` matching the window ID (e.g. `"CheckInWindow"`).

- `MainShell::openWindow(id)` — creates or activates; calls `WorkspaceState::markWindowOpen(id)`.
- `MainShell::closeWindow(id)` — closes sub-window; calls `WorkspaceState::markWindowClosed(id)`.
- Sub-window destruction (via `QMdiSubWindow::destroyed`) also triggers `markWindowClosed`.

### 14.5 System Tray and Kiosk Mode

`TrayManager` provides:
- **Normal**: standard icon; context menu: Restore / Enter Kiosk Mode / Lock / Exit.
- **Kiosk mode**: `MainShell` is hidden from the Windows taskbar (`Qt::Tool` flag), minimized.
  The tray becomes the only visible access point. Double-click restores.
- **Locked**: icon changes; Lock Console menu item disabled.
- **Warning / Error**: icon changes to warning/error icon; status bar shows tooltip.

Kiosk mode is operator-triggered (menu or `TrayManager::enterKioskMode()`). It is
appropriate for single-purpose check-in station deployments where the operator should
not switch between tasks.

### 14.6 PII Display and Clipboard Safety

`MaskedFieldWidget` ensures PII is never displayed in full without step-up authorization:
- Default view: `ClipboardGuard::maskValue(value)` (last 4 chars visible).
- Reveal: parent must complete step-up before calling `MaskedFieldWidget::reveal()`.
  The widget re-masks automatically after `Validation::StepUpWindowSeconds`.
- Copy button: always copies the masked form via `ClipboardGuard::copyMasked()`.

### 14.7 Performance Observation

`PerformanceObserver` instruments two mandatory engineering targets from §4:

| Target | Instrumentation | Verification |
|---|---|---|
| Cold-start < 3 s | `QElapsedTimer` from `Application` constructor to end of `initialize()` | Manual check on representative office PC |
| Memory growth < 200 MB (7 days) | `GetProcessMemoryInfo` (Windows) / `/proc/self/status` (Linux) sampled every 60 s | Manual check: `SELECT * FROM performance_log WHERE event_type = 'memory_sample'` |

Both results are written to `performance_log` (SQLite) and `Logger`. **These targets cannot
be proven by static code or CI alone.** Final numeric verification requires a manual
check on a representative office PC (8 GB RAM, NVMe SSD, Windows 11).

### 14.8 Migration Runner

`Migration` applies all `*.sql` files in the configured `migrations/` directory,
ordered lexicographically by filename. Each file is executed inside a single SQLite
transaction. On failure, the transaction rolls back and `Application::initialize()`
returns `false` (refusing to start with a partially applied migration).

Applied migrations are recorded in `schema_migrations` (name + applied_at). The
bootstrap `CREATE TABLE IF NOT EXISTS schema_migrations` statement is idempotent and
runs before any migration file is checked.
| Deduction balance | after = before − 1 | SQLite CHECK, `CheckInRepository` |

---

## 20. Primary Operator Windows

The core operator interface is built around four feature windows that open as MDI
sub-windows inside `MainShell`. Windows are created on demand via `MainShell::openWindow(id)`
and identified by their static `WindowId` constant.

### 20.1 AppContext

`AppContext` (declared in `src/app/AppContext.h`) is a value-type aggregate that holds
`unique_ptr` ownership of all initialized infrastructure:

- **Crypto**: `KeyStore`, `AesGcmCipher`
- **Repositories**: `UserRepository`, `AuditRepository`, `QuestionRepository`,
  `KnowledgePointRepository`, `MemberRepository`, `CheckInRepository`, `IngestionRepository`
- **Services**: `AuditService`, `AuthService`, `QuestionService`, `CheckInService`,
  `IngestionService`
- **Scheduler**: `JobScheduler`
- **Session state**: `UserSession session`, `bool authenticated` (written by login flow)

`AppContext` is constructed in `main()` after `Application::initialize()` succeeds,
following this dependency order:

```
KeyStore → AesGcmCipher
                ↓
UserRepository, AuditRepository, QuestionRepository,
KnowledgePointRepository, MemberRepository, CheckInRepository, IngestionRepository
                ↓
AuditService ← (AuditRepository + AesGcmCipher)
AuthService  ← (UserRepository + AuditRepository)
QuestionService ← (QuestionRepository + KPRepository + AuditService + AuthService)
CheckInService  ← (MemberRepository + CheckInRepository + AuthService + AuditService + AesGcmCipher + QSqlDatabase)
IngestionService ← (IngestionRepository + QuestionRepository + KPRepository + MemberRepository + AuditService + AuthService + AesGcmCipher)
JobScheduler ← (IngestionRepository + IngestionService + AuditService)
```

`MainShell` receives a non-owning `AppContext*`. Each feature window receives
`AppContext&` and accesses services directly. No service is instantiated inside a window.

### 20.2 Login Flow

`LoginWindow` is shown in a nested `QEventLoop` before `MainShell` is created.
On `loginSucceeded(token, userId)`, the session fields in `AppContext` are written and
the event loop exits. The shell then opens with a valid session context.

Bootstrap mode (first run, no active SecurityAdministrator exists) is handled inside
`LoginWindow` by calling `AuthService::bootstrapSecurityAdministrator(...)` and then
emitting a normal `loginSucceeded` signal. The shell is never opened through an
unauthenticated bootstrap bypass path.

### 20.3 CheckInWindow (`window.checkin`)

Three-tab entry-point: Barcode (BarcodeInput event filter), Member ID (manual), Mobile.
Result panel renders all `CheckInStatus` outcomes with appropriate color and text:
- **Success** — masked name, remaining balance, timestamp; Correction button visible
- **DuplicateBlocked** — 30-second window message
- **FrozenBlocked** — freeze reason
- **TermCardExpired / TermCardMissing** — term card guidance
- **PunchCardExhausted** — zero-balance message

Correction request flow: clicking "Request Correction" opens a rationale input dialog
and calls `CheckInService::requestCorrection()`. Service enforces authorization (Proctor+).

### 20.4 QuestionBankWindow (`window.question_bank`)

QTableView backed by `QuestionService::queryQuestions(QuestionFilter)`.
Filter panel: difficulty range (1–5), KP subtree combo (from `getTree()`), text search,
status dropdown. Pagination via "Load More" when result count equals page size (100).
Double-click or Edit action opens `QuestionEditorDialog`.
Right-click context menu: Edit, Delete.

### 20.5 QuestionEditorDialog

Create/edit modal with four tabs:
1. **Question** — bodyText (QTextEdit), status, difficulty, discrimination, externalId
2. **Answers** — dynamic list (2–6 options, drag reorder), correct answer combo
3. **Knowledge Points** — current KP mappings, add from KP tree combo, remove selected
4. **Tags** — current tags, add by name (creates if new), remove selected

KP and tag changes are collected in delta lists and applied via `QuestionService` after
the question is saved. Validation errors from the service are shown in a red error label.

### 20.6 AuditViewerWindow (`window.audit_viewer`)

Date-range filter (default: last 7 days), actor ID, event type dropdown, entity type/ID.
`QTableView` rows from `AuditService::queryEvents()`. Selecting a row populates a read-only
monospaced detail pane with entry hash, previous hash, and before/after payloads.
Export writes visible entries to a JSONL file and records an `AuditExport` audit event.
Verify Chain calls `AuditService::verifyChain()` and displays a pass/fail dialog with
the broken-entry ID if the chain is compromised. Records a `ChainVerified` audit event.

### 20.7 StepUpDialog

Re-authentication modal used by any window requiring `AuthService::initiateStepUp()`:
- Displays the action description
- Password entry (QLineEdit, EchoMode::Password)
- On confirm: calls `initiateStepUp(sessionToken, password)` and stores the resulting
  `StepUpWindow.id` for the caller to consume
- Returns `QDialog::Accepted` with `stepUpWindowId()` available on success

### 20.8 Role-to-Window Matrix

| Window / Action | FrontDeskOperator | Proctor | ContentManager | SecurityAdmin |
|---|---|---|---|---|
| CheckInWindow — check in | ✅ | ✅ | ✅ | ✅ |
| CheckInWindow — request correction | — | ✅ | ✅ | ✅ |
| CheckInWindow — approve correction | — | — | — | ✅ |
| QuestionBankWindow — view | — | ✅ | ✅ | ✅ |
| QuestionBankWindow — create/edit | — | — | ✅ | ✅ |
| QuestionBankWindow — delete | — | — | ✅ | ✅ |
| AuditViewerWindow — view | — | — | — | ✅ |
| AuditViewerWindow — export | — | — | — | ✅ |
| AuditViewerWindow — verify chain | — | — | — | ✅ |
| SyncWindow — view packages | — | — | — | ✅ |
| SyncWindow — import/export packages | — | — | — | ✅ |
| SyncWindow — resolve conflicts | — | — | — | ✅ (step-up) |
| SyncWindow — manage signing keys | — | — | — | ✅ (step-up) |
| DataSubjectWindow — create export request | ✅ | ✅ | ✅ | ✅ |
| DataSubjectWindow — fulfill/reject export | — | — | — | ✅ (step-up) |
| DataSubjectWindow — create deletion request | ✅ | ✅ | ✅ | ✅ |
| DataSubjectWindow — approve deletion | — | — | — | ✅ (step-up) |
| DataSubjectWindow — complete deletion | — | — | — | ✅ (step-up) |
| SecurityAdminWindow — view users/keys | — | — | — | ✅ |
| SecurityAdminWindow — change role | — | — | — | ✅ (step-up) |
| SecurityAdminWindow — freeze/thaw account | — | — | — | ✅ (step-up) |
| SecurityAdminWindow — privileged audit | — | — | — | ✅ |
| UpdateWindow — view staged packages | — | — | — | ✅ |
| UpdateWindow — apply package | — | — | — | ✅ (step-up) |
| UpdateWindow — rollback | — | — | — | ✅ (step-up) |
| IngestionMonitorWindow — view/cancel jobs | — | — | ✅ | ✅ |

Role enforcement is at the service layer. Windows call services; services enforce RBAC.

---

## 21. Secondary Service Windows

### 21.1 SyncWindow (`window.sync`)

Three-tab window for offline desk-to-desk sync operations:
- **Packages tab** — lists all imported/exported `.proctorsync` packages with status (Pending/Verified/Applied/Partial/Rejected). Buttons: Import Package (from disk/LAN/USB), Export Package (write to selected destination).
- **Conflicts tab** — lists pending `ConflictRecord` entries. Conflict resolution requires SecurityAdministrator + step-up; operator selects Accept Local / Accept Incoming / Skip.
- **Keys tab** — lists `TrustedSigningKey` entries. Import Key (SecurityAdmin + step-up, enter public key hex + label); Revoke Key (SecurityAdmin + step-up).

All privileged actions trigger `StepUpDialog` before proceeding. Import/export operations display file dialog for directory selection.

### 21.2 DataSubjectWindow (`window.data_subject`)

Two-tab window for GDPR Art.15 / MLPS Chapter 4 compliance workflows:
- **Export Requests tab** — lists all export requests with status. Any operator can create a new request (member ID + rationale). SecurityAdministrator can fulfill (triggers StepUpDialog + save file dialog for output path) or reject.
- **Deletion Requests tab** — lists all deletion requests. Any operator can initiate. SecurityAdministrator can approve (step-up), then complete (confirmation + calls `DataSubjectService::completeDeletion` → anonymizes PII). Audit tombstones are retained per `AuditRetentionYears`.

Export files contain `"WATERMARK": "AUTHORIZED_EXPORT_ONLY — NOT_FOR_REDISTRIBUTION"` and mask mobile/barcode via `MaskingPolicy`.

### 21.3 SecurityAdminWindow (`window.security_admin`)

Three-tab privileged administration window:
- **User Roles tab** — 4-column table (ID, username, role, active). Change Role (step-up required); Unlock (re-enable locked user). Buttons disabled until a row is selected.
- **Account Freezes tab** — member ID + reason inputs, Freeze Account and Thaw Account buttons (both step-up gated), freeze history table.
- **Privileged Audit tab** — filters audit log to security-relevant events (RoleChanged, KeyImported, StepUpInitiated, AccountFrozen, AccountThawed, ExportFulfilled, DeletionCompleted) for the last 30 days.

### 21.4 UpdateWindow (`window.update`)

Three-tab update management window with a yellow informational banner noting the signed `.msi` delivery override:
- **Staged Packages tab** — lists packages with status. Import Package (select directory); Apply (step-up + confirmation); Cancel.
- **Install History tab** — chronological table of applied updates with from/to versions, applied-by, applied-at.
- **Rollback Records tab** — rollback audit trail with rationale, rolled-back-by, rolled-back-at.

Apply and Rollback require SecurityAdministrator + step-up. Apply records `InstallHistoryEntry` with pre-update snapshot. Rollback reads history entry and records `RollbackRecord`.

### 21.5 IngestionMonitorWindow (`window.ingestion_monitor`)

Single-pane scheduler observability window:
- Job table: ID, type (QuestionImport/RosterImport), status, phase, priority, retry count, created-at.
- Auto-refreshes every 5 seconds via `QTimer`.
- Cancel Job button (enabled only for Pending jobs) calls `IngestionService::cancelJob`.

---

## 22. Compliance Control Matrix

ProctorOps implements controls that directly align with GDPR Chapter III (data subject rights) and MLPS 2.0 (China Multi-Level Protection Scheme, classified security requirements). The table below maps each compliance requirement to the enforcement point in the implementation.

| Requirement | Standard | Enforcement Point | Status |
|---|---|---|---|
| Data subject access (export) | GDPR Art.15 / MLPS §4 | `DataSubjectService::fulfillExportRequest` + watermark + masking | ✅ Implemented |
| Right to erasure (deletion) | GDPR Art.17 | `DataSubjectService::completeDeletion` → `MemberRepository::anonymizeMember` | ✅ Implemented |
| Deletion requires dual authorization | GDPR Art.17 / internal policy | PENDING → APPROVED (step-up) → COMPLETED; no single-step erasure | ✅ Implemented |
| Audit retention after deletion | GDPR Recital 65 / MLPS §4.1 | `AuditRetentionYears = 3`; audit entries never erased | ✅ Implemented |
| Export authorization traceability | GDPR Art.5(2) accountability | Export requests require rationale; fulfillment audit-logged with actor + step-up ID | ✅ Implemented |
| PII masking in exports | GDPR Art.5(1)(c) data minimization | `MaskingPolicy::maskMobile`, `maskBarcode` applied to export JSON | ✅ Implemented |
| Export file watermarking | Internal / MLPS §4.2 | `"WATERMARK": "AUTHORIZED_EXPORT_ONLY — NOT_FOR_REDISTRIBUTION"` field | ✅ Implemented |
| Access control (RBAC) | MLPS §4.1 / GDPR Art.32 | Service-layer RBAC; SecurityAdministrator required for all compliance actions | ✅ Implemented |
| Step-up re-authentication | MLPS §4.1 / GDPR Art.32 | `StepUpDialog` consumed before all deletion/fulfill/freeze/role-change actions | ✅ Implemented |
| Audit chain immutability | MLPS §4.1 / GDPR Art.5(2) | SHA-256 hash-chained audit log; append-only; previous_hash linkage | ✅ Implemented |
| PII at-rest encryption | MLPS §4.1 / GDPR Art.32 | AES-256-GCM for mobile, barcode, name; master key via DPAPI/KeyStore | ✅ Implemented |
| Sync package signing | MLPS §4.2 / integrity | Ed25519 detached signature + SHA-256 per entity file; rejected if invalid | ✅ Implemented |
| Update package signing | MLPS §4.2 / integrity | Ed25519 detached signature + SHA-256 per component; rejected if invalid | ✅ Implemented |
| Signing key revocation | Internal / MLPS §4.1 | `SyncService::revokeSigningKey`; revoked keys blocked at import | ✅ Implemented |
| Conflict detection for sync | Internal data integrity | `SyncService::detectAndRecordConflicts`; DoubleDeduction detection | ✅ Implemented |
| Offline-only operation | MLPS local-only security boundary | No network calls; all data stays within Windows 11 DPAPI trust boundary | ✅ Implemented |
