# ProctorOps — Offline Assessment Operations Console

> **Project type:** desktop
> **Platform:** Windows 11 (x86_64), offline-native
> **Stack:** C++20 · Qt 6 Widgets · SQLite · CMake · Docker (build/test)

---

## Overview

ProctorOps is a Windows 11 desktop application for training centers and corporate testing rooms. It operates entirely offline and is designed for front-desk operators, proctors, content managers, and security administrators.

Three business pillars:
1. **Exam content governance** — question bank, KnowledgePoint trees, ingestion scheduler, query builder
2. **On-site entry validation** — barcode/member/mobile check-in, term-card validation, punch-card deduction, 30-second duplicate suppression
3. **Compliance-grade traceability** — SHA-256 hash-chained audit log, RBAC, GDPR/MLPS export, update/rollback

---

## Quickstart (Canonical)

Use this path for verification and CI. It is the primary and recommended workflow.

```bash
# From TASK-127 workspace root
./repo/run_tests.sh
```

Expected outcome:
- Docker builds test image
- CMake configures and builds tests in-container
- CTest executes unit + integration suites
- Script prints target/function summary and exits non-zero on failure

Use native Windows run/build commands only for production deployment validation.

---

## Signed `.msi` Delivery Override

> **Override in effect:** Delivery of this project does **not** require a signed `.msi` installer artifact.

This override was established at project inception. The following remain **fully in scope**:
- Update/rollback logic and domain model
- Signed update package verification (Ed25519 over manifest + component digests)
- Install history, rollback records, and update management UI
- All update-related tests and documentation

The absence of a shipped signed `.msi` file at delivery is not a scope failure. See `docs/design.md §2` for the full override rationale.

**To build a signed `.msi` installer:** see `docs/design.md §2.1` for step-by-step instructions using WiX Toolset and `signtool`. This is an optional post-build step for environments that require Authenticode-signed installer distribution.

---

## Technology Stack

| Component | Technology | Version |
|---|---|---|
| Language | C++20 | C++20 standard |
| Build system | CMake + Ninja | CMake 3.28+ |
| UI framework | Qt 6 Widgets | Qt 6.5+ |
| Qt modules | Core, Widgets, Sql, Concurrent, StateMachine, Test | Qt 6.5+ |
| Persistence | SQLite (WAL mode) | 3.39+ |
| Password hashing | Argon2id | libargon2 |
| Field encryption | AES-256-GCM | OpenSSL 3.x |
| Package signing | Ed25519 | OpenSSL 3.x |
| Key derivation | HKDF-SHA256 | OpenSSL 3.x |
| Test runner | CTest + Qt Test | CMake 3.28+ |
| Container | Docker + docker-compose | Build/test only |

---

## Repository Structure

```
TASK-127/
├── docs/
│   ├── design.md          # Architecture, module map, traceability table
│   ├── api-spec.md        # Internal service contracts, schemas, repository contracts
│   └── questions.md       # Ambiguity log — blocker-level assumptions only
├── repo/
│   ├── README.md          # This file
│   ├── docker-compose.yml # Docker build/test orchestration
│   ├── run_tests.sh       # Docker-first test runner
│   └── desktop/
│       ├── Dockerfile         # Build and test container definition
│       ├── CMakeLists.txt     # Root CMake for the desktop app
│       ├── src/               # Application source (C++20)
│       │   ├── app/           # Application shell, startup, crash recovery
│       │   ├── windows/       # Top-level windows (MainShell, LoginWindow, etc.)
│       │   ├── dialogs/       # Feature dialogs (StepUp, CommandPalette, etc.)
│       │   ├── widgets/       # Reusable widgets (MaskedField, BarcodeInput, etc.)
│       │   ├── services/      # Business logic services
│       │   ├── repositories/  # SQLite data access
│       │   ├── crypto/        # Argon2id, AES-GCM, Ed25519, KeyStore
│       │   ├── scheduler/     # Ingestion job scheduler, worker pool
│       │   ├── tray/          # System tray / kiosk mode
│       │   ├── audit/         # Hash-chained audit infrastructure
│       │   └── utils/         # Logger, ClipboardGuard, Migration runner
│       ├── resources/         # Qt resource files (.qrc, icons, fonts)
│       ├── database/
│       │   ├── migrations/    # Sequential SQL migration files (NNNN_description.sql)
│       │   └── fixtures/      # Deterministic seed data for tests
│       ├── unit_tests/        # Qt Test / CTest unit tests
│       └── api_tests/         # Integration-style tests for internal service contracts
└── metadata.json          # Project metadata
```

---

## What Exists (Implementation Status)

The repository currently includes the following implemented security foundation components:

**Crypto primitives** (`src/crypto/`):
- `SecureRandom` — OS-backed random (CryptGenRandom / /dev/urandom)
- `Argon2idHasher` — password hashing with Validation.h parameters
- `AesGcmCipher` — versioned AES-256-GCM field encryption with HKDF per-record key derivation
- `Ed25519Verifier` — detached signature verification via OpenSSL 3.x EVP
- `HashChain` — SHA-256 audit chain hashing with canonical pipe-delimited serialization
- `KeyStore` / `IKeyStore` — DPAPI (Windows) / file-based (Linux) master key management

**Shared utilities** (`src/utils/`):
- `Logger` — structured JSON-lines logging with secret redaction and deterministic identifier hashing
- `ClipboardGuard` — masks/redacts PII before OS clipboard
- `MaskingPolicy` — centralized masking rules (mobile, barcode, name, generic)
- `CaptchaGenerator` — locally rendered QPainter CAPTCHA with SHA-256 answer verification
- `ErrorFormatter` — maps ErrorCode to user-facing messages and form hints

**Concrete repositories** (`src/repositories/`):
- `UserRepository` — users, credentials, sessions, lockout, CAPTCHA, step-up (including atomic user+credential provisioning)
- `AuditRepository` — append-only audit chain with atomic transaction; export/deletion request list queries
- `SyncRepository` — sync packages, entities, conflicts, trusted signing keys
- `UpdateRepository` — update packages, components, install history, rollback records
- `QuestionRepository` — 15 methods: questions, KP mappings, tag mappings, combined query builder
- `KnowledgePointRepository` — 6 methods: KP tree CRUD with materialized path propagation
- `MemberRepository` — 17 methods: members, term cards, punch cards, freeze records, encrypted PII lookup
- `CheckInRepository` — check-in attempts, deduction events, corrections, sync import materialization helpers
- `IngestionRepository` — 18 methods: jobs, dependencies, checkpoints, worker claims

**Services** (`src/services/`):
- `AuthService` — sign-in, lockout (5 in 10 min), fail-closed CAPTCHA (after 3), step-up (2 min), RBAC, security-admin user provisioning/password reset, secure first-admin bootstrap, console lock/unlock
- `AuditService` — event recording with hash chain and PII encryption in payloads
- `PackageVerifier` — Ed25519 signature + SHA-256 digest verification for packages
- `QuestionService` — 22 methods: question CRUD with validation, KP tree, mappings, tags, query builder, service-layer ContentManager RBAC
- `CheckInService` — check-in flow (member resolution, freeze/term/dedup/deduction), transaction-time duplicate recheck, correction workflow, freeze/thaw service controls
- `IngestionService` — import pipeline (JSONL questions, CSV rosters), 3-phase validate→import→index, service-layer ContentManager RBAC for job create/cancel
- `SyncService` — offline desk-to-desk sync: export/import `.proctorsync` packages, import-time materialization of deductions/corrections, conflict detection with linked compensating-action resolution, signing key management
- `UpdateService` — offline update: import `.proctorpkg`, verify signature + digests, stage, artifact-level apply/rollback with live-file backup/restore, install history
- `DataSubjectService` — GDPR Art.15/17 and MLPS compliance: export request (watermarked JSON), deletion request (PII anonymization with audit tombstone), reject/fulfill actions gated by SecurityAdministrator step-up

**Scheduler** (`src/scheduler/`):
- `JobScheduler` — priority-aware scheduling, dependency resolution, retry backoff, crash recovery, fairness

**Widgets** (`src/widgets/`):
- `BarcodeInput` — USB HID barcode scanner input via keystroke timing detection
- `MaskedFieldWidget` — PII-masked display (last 4 chars default), step-up reveal, clipboard safety

**Application shell** (`src/app/`):
- `Application` — QApplication subclass: DB open, migration runner, crash detection, cold-start timing
- `AppSettings` — QSettings-backed preferences (DB path, log level, kiosk mode, window geometry)
- `ActionRouter` — named action registry with shortcut dispatch and command palette backing
- `WorkspaceState` — workspace state persistence for crash-recovery window/job restoration

**Windows** (`src/windows/`):
- `MainShell` — primary workspace controller: MDI area, Ctrl+K/F/L shortcuts, context menus, tray wiring
- `LoginWindow` — sign-in form, CAPTCHA display (after 3 failures), secure first-admin bootstrap provisioning (no unauthenticated shell bypass)

**Application context** (`src/app/AppContext.h`):
- `AppContext` — aggregate of all initialized infrastructure (crypto, repositories, services, session state); constructed in `main()` and passed non-owning to `MainShell` and all feature windows

**Operator windows** (`src/windows/`):
- `CheckInWindow` — three-tab entry validation (Barcode/MemberID/Mobile), result panel with all `CheckInStatus` outcomes, correction request button; wired to `CheckInService`
- `QuestionBankWindow` — paginated `QTableView` of questions with filter panel (difficulty, KP subtree, text search, status); opens `QuestionEditorDialog` on edit; wired to `QuestionService`
- `AuditViewerWindow` — date-range filter, `QTableView` of `AuditEntry` rows, detail pane showing hashes and before/after payloads, chain verification dialog, JSONL export; wired to `AuditService`
- `SyncWindow` — 3-tab window: Packages (import/export `.proctorsync`), Conflicts (step-up resolution), Keys (import/revoke signing keys)
- `DataSubjectWindow` — 2-tab window: Export Requests (create/fulfill/reject), Deletion Requests (create/approve/complete with PII anonymization)
- `SecurityAdminWindow` — 3-tab privileged window: User Roles (create user, reset password, change role/unlock, step-up gated), Account Freezes (freeze/thaw, step-up gated), Privileged Audit (security-relevant event filter)
- `UpdateWindow` — 3-tab window: Staged Packages (import/apply/cancel), Install History (from/to version), Rollback Records; `.msi` delivery override banner displayed
- `IngestionMonitorWindow` — scheduler observability; auto-refreshes every 5 seconds; Cancel Job for pending jobs

**Dialogs** (`src/dialogs/`):
- `CommandPaletteDialog` — Ctrl+K fuzzy action search, keyboard navigation, action dispatch
- `StepUpDialog` — re-authentication modal for sensitive operations; returns `StepUpWindow.id` on success
- `QuestionEditorDialog` — create/edit question with four tabs (Question fields, Answers, KP Mappings, Tags); wired to `QuestionService`

**Tray** (`src/tray/`):
- `TrayManager` — system tray icon, kiosk/check-in station mode, lock state, operator-safe exit

**Utilities** (`src/utils/`):
- `Migration` — sequential SQLite migration runner (schema_migrations table, per-file transactions)
- `PerformanceObserver` — cold-start timing (< 3 s target) and memory RSS sampling (< 200 MB growth target)

**Domain models** (`src/models/`): 9 header files + `CommonTypes.cpp` (76 event types)

---

## Offline and Windows 11 Constraints

- **No internet access.** TLS is not used. No external endpoints of any kind.
- **Inter-desk data exchange** uses LAN share (`\\server\share`) or USB drive.
- **Signed packages** (`.proctorsync` for sync, `.proctorpkg` for updates) are the only external data ingestion paths.
- **Key trust** established at install time or via security-admin key-import workflow.
- **Production runtime:** Windows 11 desktop, x86_64, 64-bit only.
- **Docker** is used for reproducible build and test execution only — not the production runtime.

---

## Performance Targets

| Target | Requirement |
|---|---|
| Cold-start | < 3 seconds on a representative office PC |
| Continuous operation | 7 days without memory growth > 200 MB above baseline |
| Crash recovery | Automatic state restoration on next launch (WAL + checkpoints) |

These targets require instrumentation in `Application` and are verified manually on representative hardware. See `docs/design.md §16` for the instrumentation plan.

---

## Optional: Native Windows Production Build

> **Docker is the required build and test environment.** All compilation, testing, and audit verification must be done via `./repo/run_tests.sh` (see [Building and Testing with Docker](#building-and-testing-with-docker-recommended-for-civerification) below). Native prerequisites and toolchain setup are documented in `docs/design.md §2` and are not reproduced here.
>
> The cmake invocation below is provided as a reference for Windows 11 production deployment only — it is not used for development, CI, or audit verification.

```bat
:: Configure (Windows 11 production deployment only — not for CI)
cmake -B build -S repo/desktop -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=<Qt install path>

:: Build
cmake --build build --parallel

:: Run
build\ProctorOps.exe
```

> **Note:** Native build and execution are not performed during the authoring phase. The commands above are the intended invocation once all source modules are implemented.

---

## Demo Credentials (Auth Enabled)

Authentication is required. Use the following demo users for role-based verification:

| Role | Username | Password |
|---|---|---|
| `FRONT_DESK_OPERATOR` | `operator` | `Operator!234` |
| `PROCTOR` | `proctor` | `Proctor!234` |
| `CONTENT_MANAGER` | `content_manager` | `Content!234` |
| `SECURITY_ADMINISTRATOR` | `admin` | `Admin!234` |

If these users do not exist in a fresh database, create them through the first-run bootstrap flow and the Security Administration window.

---

## Verification Method (Desktop Runtime)

Use this flow after launching `build\ProctorOps.exe` to confirm core behavior:

1. Sign in as `operator` and open **Check-In Desk**.
2. Enter a session ID and attempt a known-valid member check-in.
3. Confirm a successful check-in status and reduced punch-card balance.
4. Trigger a duplicate check-in within 30 seconds and confirm duplicate-blocked status.
5. Sign out, then sign in as `admin`.
6. Open **Audit Viewer** and verify `CHECKIN_SUCCESS` and duplicate-block events appear.
7. Open **Data Subject Requests**, create an export request, and fulfill it with step-up.
8. Confirm the exported JSON contains watermarking and masked fields.

---

## Configuration

ProctorOps stores all runtime preferences via `QSettings` (Windows registry on production, INI file on Linux/Docker). No environment variables are required at runtime. No absolute developer paths are embedded.

### Settings keys and defaults

| Key | Default | Description |
|---|---|---|
| `database/path` | `%AppData%\ProctorOps\proctorops.db` | SQLite database file path |
| `database/migration_dir` | `<app dir>/migrations` | SQL migration files directory |
| `logging/level` | `info` | Log level: `debug` / `info` / `warn` / `error` |
| `ui/kiosk_mode` | `false` | Minimize to tray and hide from taskbar when idle |
| `ui/main_window_geometry` | *(empty)* | Saved window geometry blob |

### Local storage locations

- **Database:** `%LocalAppData%\ProctorOps\proctorops.db` (default on Windows 11)
- **Migration files:** `<install dir>\migrations\` — sequential SQL files applied on first run
- **Structured log:** Routed through Qt logging; configurable via `logging/level`
- **Sync packages** (`.proctorsync`): selected via file dialog — no fixed path
- **Update packages** (`.proctorpkg`): selected via file dialog — no fixed path

### Key material storage

| Key type | Storage mechanism | Notes |
|---|---|---|
| AES-256-GCM master key | Windows DPAPI (`CryptProtectData`) | Per-installation, machine-bound |
| Ed25519 signing private key | DPAPI via `KeyStore` under `sync.signing.private_key` | Generated by security admin at first config |
| Ed25519 trusted public keys | SQLite `trusted_signing_keys` table | Imported by security admin; revocable |

**Docker/Linux fallback:** On Linux (Docker, CI), `KeyStore` uses a file-permission-protected key file with XOR obfuscation. This is **not** security-equivalent to DPAPI and is acceptable only for build and test execution — not for production use. See `docs/questions.md §6`.

### Environment variables (Docker/test context only)

| Variable | Value | Where set |
|---|---|---|
| `QT_QPA_PLATFORM` | `offscreen` | Dockerfile ENV, docker-compose, CTest properties |
| `CTEST_OUTPUT_ON_FAILURE` | `1` | Dockerfile ENV, docker-compose, run_tests.sh |
| `Qt6_DIR` | `/opt/Qt/6.6.3/gcc_64/lib/cmake/Qt6` | Dockerfile ENV (inherited by run_tests.sh) |

No environment variables are required for native Windows 11 production use.

---

## Building and Testing with Docker (Recommended for CI/Verification)

Docker provides a reproducible build and headless test environment. It is the canonical way to verify the test suite.

**What Docker proves:**
- The codebase compiles cleanly with GCC 12 / Qt 6.6.3 / CMake 3.28.6
- All authored unit and integration tests pass headlessly
- No missing dependencies or hidden host-specific assumptions

**What Docker does NOT prove:**
- Native Windows 11 GUI behavior (Qt Widgets rendering, system tray, DPAPI key storage)
- Performance targets (cold-start < 3 s, 7-day < 200 MB) — these require manual verification on representative Windows hardware
- Windows DPAPI key custody — the Linux Docker path uses a non-equivalent fallback

```bash
# Build the container and run all tests (canonical entry point)
./repo/run_tests.sh

# Alternatively, use docker compose directly (uses Dockerfile CMD — requires warm build cache)
docker compose -f repo/docker-compose.yml up build-test
```

Important: do not run `docker compose ... run build-test ./run_tests.sh`.
Inside the container, `/workspace` maps to `repo/desktop`, so `./run_tests.sh`
is not present there. Use `./repo/run_tests.sh` from the host, or run
`docker compose ... run build-test` with no extra command.

> **Note:** Docker is the canonical build and test environment for this project. All test targets compile and execute cleanly under the Docker configuration defined in `repo/desktop/Dockerfile` and `repo/docker-compose.yml`.

---

## Running Tests (Docker-first)

All tests are executed inside Docker via `repo/run_tests.sh`. Tests are organized as:

| Directory | Contents | Count |
|---|---|---|
| `repo/desktop/unit_tests/` | Qt Test / CTest unit tests per module | 45 test files, ~300+ test cases |
| `repo/desktop/api_tests/` | Integration-style tests for internal service contracts | 13 test files, ~90+ test cases |

The runner prints a post-run summary with:
- `Total test targets` (CTests selected by filter; full suite currently 58)
- `Total QTest functions` (sum of `-functions` output across built test binaries)

For the complete requirement-to-test mapping, see `docs/test-traceability.md`.

```bash
# Run all tests (Docker)
./repo/run_tests.sh

# Run only unit tests
./repo/run_tests.sh --unit-only

# Run only integration tests
./repo/run_tests.sh --api-only

# Run with CTest filter
./repo/run_tests.sh --test-filter "tst_auth"

# Force full rebuild
./repo/run_tests.sh --rebuild
```

When `--unit-only` or `--api-only` is selected, the runner compiles only the
corresponding CMake test targets before executing CTest. This keeps isolated
suite runs focused and avoids unrelated target build failures.

**Unit test targets (45):**

```bash
./build/unit_tests/tst_bootstrap
./build/unit_tests/tst_app_settings
./build/unit_tests/tst_domain_validation
./build/unit_tests/tst_migration
./build/unit_tests/tst_crypto
./build/unit_tests/tst_auth_service
./build/unit_tests/tst_audit_chain
./build/unit_tests/tst_clipboard_guard
./build/unit_tests/tst_masked_field
./build/unit_tests/tst_error_formatter
./build/unit_tests/tst_key_store
./build/unit_tests/tst_logger
./build/unit_tests/tst_captcha_generator
./build/unit_tests/tst_performance_observer
./build/unit_tests/tst_question_service
./build/unit_tests/tst_checkin_service
./build/unit_tests/tst_job_scheduler
./build/unit_tests/tst_job_checkpoint
./build/unit_tests/tst_ingestion_service
./build/unit_tests/tst_action_routing
./build/unit_tests/tst_command_palette
./build/unit_tests/tst_workspace_state
./build/unit_tests/tst_crash_recovery
./build/unit_tests/tst_tray_mode
./build/unit_tests/tst_checkin_window
./build/unit_tests/tst_question_editor
./build/unit_tests/tst_audit_viewer
./build/unit_tests/tst_masked_field_widget
./build/unit_tests/tst_login_window
./build/unit_tests/tst_sync_window
./build/unit_tests/tst_update_window
./build/unit_tests/tst_data_subject_window
./build/unit_tests/tst_ingestion_monitor_window
./build/unit_tests/tst_sync_service
./build/unit_tests/tst_update_service
./build/unit_tests/tst_data_subject_service
./build/unit_tests/tst_security_admin_window
./build/unit_tests/tst_privileged_scope
./build/unit_tests/tst_main_shell
./build/unit_tests/tst_question_bank_window
./build/unit_tests/tst_application
./build/unit_tests/tst_app_bootstrap
./build/unit_tests/tst_app_context
./build/unit_tests/tst_repository_contracts
./build/unit_tests/tst_package_verifier
```

**Integration test targets (13):**

```bash
./build/api_tests/tst_api_bootstrap
./build/api_tests/tst_schema_constraints
./build/api_tests/tst_auth_integration
./build/api_tests/tst_audit_integration
./build/api_tests/tst_package_verification
./build/api_tests/tst_checkin_flow
./build/api_tests/tst_correction_flow
./build/api_tests/tst_shell_recovery
./build/api_tests/tst_operator_workflows
./build/api_tests/tst_export_flow
./build/api_tests/tst_sync_import_flow
./build/api_tests/tst_update_flow
./build/api_tests/tst_privileged_scope_api
```

---

## Security Configuration

All security behavior is governed by constants in `src/utils/Validation.h`:

| Parameter | Value | Description |
|---|---|---|
| Argon2id (t/m/p/tag/salt) | 3 / 64MB / 4 / 32B / 16B | Password hashing parameters |
| Lockout threshold | 5 failures in 600s (10 min) | Account lock trigger |
| CAPTCHA trigger | After 3 failures | Local CAPTCHA required before next attempt |
| CAPTCHA cool-down | 900s (15 min) | Cool-down resets CAPTCHA requirement |
| Step-up window | 120s (2 min), single-use | Re-auth for sensitive actions |
| AES-256-GCM (key/nonce/tag/salt) | 32B / 12B / 16B / 16B | Field encryption parameters |

**Security implementation:**
- **Argon2idHasher** — constant-time password verification, fresh random salt per hash
- **AesGcmCipher** — HKDF-SHA256 per-record key derivation, versioned ciphertext format
- **MaskingPolicy** — all PII masked by default (last 4 digits visible), step-up required to reveal
- **ClipboardGuard** — PII never reaches OS clipboard in plaintext
- **RBAC** — `Role >= required` comparison, enforced at service layer (not just UI)
- **Audit chain** — SHA-256 hash-linked, append-only, PII encrypted in payloads
- **Logger** — auto-scrubs sensitive fields (mobile, barcode, password, secret, key, hash, token)
- **PackageVerifier** — Ed25519 signature + SHA-256 digest, checks revocation/expiry, fail-closed

---

## Ambiguity Log

Open questions and implementation-shaping assumptions are recorded in `docs/questions.md`. That file is the sole ambiguity log for this project.

---

## Documentation

| Document | Purpose |
|---|---|
| `docs/design.md` | Architecture, module map, SQLite schema overview, requirement-to-module traceability |
| `docs/api-spec.md` | Internal service contracts, repository contracts, file/package schemas |
| `docs/questions.md` | Blocker-level ambiguities and proposed interpretations |
| `docs/test-traceability.md` | Requirement-to-test mapping matrix for static audit review |
| `repo/README.md` | This file — project overview, stack, constraints, commands |
