# Delivery Acceptance and Project Architecture Audit (Static-Only)

## 1. Verdict
- Overall conclusion: **Partial Pass**

Primary reason: core architecture, modules, and many security controls are implemented and test-backed, but there are material gaps in authorization boundaries and requirement-fit of query-builder UI behavior.

---

## 2. Scope and Static Verification Boundary
- **What was reviewed**:
  - Project docs and contracts: `docs/design.md`, `docs/api-spec.md`, `docs/test-traceability.md`, `repo/README.md`
  - Build/test orchestration: `repo/docker-compose.yml`, `repo/run_tests.sh`, `repo/desktop/Dockerfile`, CMake files
  - Core implementation: auth, check-in, audit, sync/update, ingestion/scheduler, desktop windows, repositories, crypto, logging
  - Unit/integration test inventories and selected high-risk test files under `repo/desktop/unit_tests/` and `repo/desktop/api_tests/`
- **What was not reviewed**:
  - `sessions/` (intentionally excluded)
  - Runtime behavior in real Windows environment
- **What was intentionally not executed**:
  - No project run
  - No Docker run
  - No tests run
- **Manual verification required**:
  - Cold-start < 3s and 7-day memory growth < 200MB (cannot be proven statically)
  - End-to-end kiosk/tray UX behavior under native Windows shell interactions
  - Actual package import/export operator workflows in real media/file-permission environments

---

## 3. Repository / Requirement Mapping Summary
- Prompt core goal mapped: offline Windows desktop console for exam governance, check-in validation, and compliance traceability.
- Main implementation areas mapped:
  - Auth/RBAC/CAPTCHA/step-up: `repo/desktop/src/services/AuthService.cpp`
  - Entry validation and atomic deduction/duplicate guard: `repo/desktop/src/services/CheckInService.cpp`
  - Content governance/query infrastructure: `repo/desktop/src/services/QuestionService.cpp`, `repo/desktop/src/repositories/QuestionRepository.cpp`
  - Audit chain and compliance services: `repo/desktop/src/services/AuditService.cpp`, `repo/desktop/src/services/DataSubjectService.cpp`
  - Sync/update package verification paths: `repo/desktop/src/services/SyncService.cpp`, `repo/desktop/src/services/UpdateService.cpp`, `repo/desktop/src/services/PackageVerifier.cpp`
  - Scheduler/recovery/checkpoints: `repo/desktop/src/scheduler/JobScheduler.cpp`, `repo/desktop/src/services/IngestionService.cpp`

---

## 4. Section-by-section Review

### 4.1 Hard Gates

#### 4.1.1 Documentation and static verifiability
- Conclusion: **Pass**
- Rationale: startup/build/test/config docs exist and are broadly aligned with code layout and targets.
- Evidence:
  - `repo/README.md:1`
  - `repo/README.md:287`
  - `repo/desktop/CMakeLists.txt:1`
  - `repo/desktop/src/CMakeLists.txt:1`
  - `repo/run_tests.sh:1`
  - `repo/docker-compose.yml:1`

#### 4.1.2 Material deviation from prompt
- Conclusion: **Partial Pass**
- Rationale: business pillars are implemented, but key UI-facing requirement fit is weakened (query-builder completeness, authorization boundary drift).
- Evidence:
  - Query filter model supports tag/discrimination, but window filter UI omits them:
    - `repo/desktop/src/models/Question.h:79`
    - `repo/desktop/src/windows/QuestionBankWindow.cpp:118`
    - `repo/desktop/src/windows/QuestionBankWindow.cpp:214`
  - Authorization boundary drift for audit window access:
    - `repo/desktop/src/windows/MainShell.cpp:34`
    - `repo/desktop/src/windows/MainShell.cpp:155`
    - `repo/desktop/src/windows/AuditViewerWindow.cpp:296`

### 4.2 Delivery Completeness

#### 4.2.1 Core explicit requirements coverage
- Conclusion: **Partial Pass**
- Rationale: most core requirements are present (auth/CAPTCHA/lockout, check-in, sync/update, audit chain, scheduler pipeline), but query-builder UI and authorization mapping gaps remain.
- Evidence:
  - Auth lockout/CAPTCHA/step-up constants and service logic:
    - `repo/desktop/src/utils/Validation.h:15`
    - `repo/desktop/src/services/AuthService.cpp:66`
    - `repo/desktop/src/services/AuthService.cpp:240`
  - Duplicate guard and atomic check-in transaction:
    - `repo/desktop/src/services/CheckInService.cpp:241`
    - `repo/desktop/src/services/CheckInService.cpp:271`
  - Scheduler and 3-phase ingestion:
    - `repo/desktop/src/services/IngestionService.cpp:166`
    - `repo/desktop/src/services/IngestionService.cpp:194`
    - `repo/desktop/src/scheduler/JobScheduler.cpp:151`

#### 4.2.2 End-to-end deliverable maturity
- Conclusion: **Pass**
- Rationale: repository is full-structure product-style delivery, not a sample fragment.
- Evidence:
  - Multi-module desktop app and service/repository separation: `repo/desktop/src/CMakeLists.txt:1`
  - Unit and integration test suites present: `repo/desktop/unit_tests/CMakeLists.txt:1`, `repo/desktop/api_tests/CMakeLists.txt:1`
  - Internal contracts and design docs exist: `docs/design.md:1`, `docs/api-spec.md:1`

### 4.3 Engineering and Architecture Quality

#### 4.3.1 Structure and decomposition
- Conclusion: **Pass**
- Rationale: clear module decomposition across windows/services/repositories/crypto/scheduler.
- Evidence:
  - `repo/desktop/src/CMakeLists.txt:1`
  - `repo/desktop/src/main.cpp:61`

#### 4.3.2 Maintainability/extensibility
- Conclusion: **Partial Pass**
- Rationale: codebase is modular, but some policy handling is inconsistent between UI and service boundaries.
- Evidence:
  - Service-layer RBAC for privileged operations: `repo/desktop/src/services/AuthService.cpp:404`
  - UI role gating table in shell differs from role matrix intent: `repo/desktop/src/windows/MainShell.cpp:34`, `docs/design.md:960`

### 4.4 Engineering Details and Professionalism

#### 4.4.1 Error handling/logging/validation quality
- Conclusion: **Partial Pass**
- Rationale: strong validation/logging exists, but there are notable defects: ineffective audit event-type filter and check-in failure UI mapping loss.
- Evidence:
  - Validation constants and use: `repo/desktop/src/utils/Validation.h:11`, `repo/desktop/src/services/IngestionService.cpp:281`
  - Event-type filter not applied in audit viewer:
    - `repo/desktop/src/windows/AuditViewerWindow.cpp:274`
    - `repo/desktop/src/windows/AuditViewerWindow.cpp:280`
  - Check-in UI collapses typed failures into generic path:
    - `repo/desktop/src/windows/CheckInWindow.cpp:180`
    - `repo/desktop/src/windows/CheckInWindow.cpp:191`

#### 4.4.2 Product-level realism
- Conclusion: **Pass**
- Rationale: implementation is product-shaped with multi-window shell, tray mode, privileged dialogs, package workflows, and traceability services.
- Evidence:
  - `repo/desktop/src/windows/MainShell.cpp:50`
  - `repo/desktop/src/tray/TrayManager.cpp:1`
  - `repo/desktop/src/windows/SyncWindow.cpp:1`
  - `repo/desktop/src/windows/UpdateWindow.cpp:1`

### 4.5 Prompt Understanding and Requirement Fit

#### 4.5.1 Business goal and implicit constraints fit
- Conclusion: **Partial Pass**
- Rationale: broad business fit is strong; however, high-sensitivity authorization and query-builder semantics are not fully aligned.
- Evidence:
  - Prompt-fit modules exist for all three pillars in source tree and service wiring: `repo/desktop/src/main.cpp:61`
  - Query-builder omission at UI level: `repo/desktop/src/windows/QuestionBankWindow.cpp:118`
  - Audit window authorization boundary weakness: `repo/desktop/src/windows/MainShell.cpp:34`

### 4.6 Aesthetics (frontend-only/full-stack)
- Conclusion: **Not Applicable**
- Rationale: task is a native Qt desktop application audit, not a web frontend visual-delivery task.

---

## 5. Issues / Suggestions (Severity-Rated)

### Blocker / High

1. **Severity: High**
- Title: Audit log access is not role-restricted in shell/service path
- Conclusion: **Fail**
- Evidence:
  - `repo/desktop/src/windows/MainShell.cpp:34` (required role map omits `AuditViewerWindow`)
  - `repo/desktop/src/windows/MainShell.cpp:155` (`canOpenWindow` permits unlisted windows)
  - `repo/desktop/src/services/AuditService.cpp:73` (`queryEvents` has no actor/role gate)
  - `repo/desktop/src/windows/AuditViewerWindow.cpp:296` (direct query call)
- Impact:
  - Any authenticated user who can open the window can query/export/verify audit data, weakening least-privilege controls for compliance-grade traceability.
- Minimum actionable fix:
  - Add explicit role gate for `AuditViewerWindow` in shell policy and enforce authorization at audit service entry points (not UI only).

2. **Severity: High**
- Title: Query builder UI does not expose full prompt-required combined filters
- Conclusion: **Partial Fail**
- Evidence:
  - Filter model supports discrimination and tags: `repo/desktop/src/models/Question.h:79`
  - Repository query supports tag/discrimination filtering: `repo/desktop/src/repositories/QuestionRepository.cpp:233`, `repo/desktop/src/repositories/QuestionRepository.cpp:254`
  - Question bank filter UI only binds difficulty/KP/text/status: `repo/desktop/src/windows/QuestionBankWindow.cpp:118`, `repo/desktop/src/windows/QuestionBankWindow.cpp:214`
- Impact:
  - Prompt’s explicit combined query expectations (e.g., chapter + difficulty + tag, discrimination bounds) are not fully realizable from the primary UI workflow.
- Minimum actionable fix:
  - Add tag and discrimination controls in `QuestionBankWindow`, wire to `QuestionFilter.tagIds/discriminationMin/discriminationMax`, and add coverage tests.

### Medium

3. **Severity: Medium**
- Title: Check-in UI loses typed failure states and shows generic failures
- Conclusion: **Fail**
- Evidence:
  - Generic failure path always used on service error: `repo/desktop/src/windows/CheckInWindow.cpp:191`
  - Rich failure-state rendering exists but is bypassed (`DuplicateBlocked`, `FrozenBlocked`, etc.): `repo/desktop/src/windows/CheckInWindow.cpp:230`
- Impact:
  - Operators do not reliably receive scenario-specific guidance (duplicate/frozen/term-card/punch exhaustion), reducing operational correctness and UX quality.
- Minimum actionable fix:
  - Map service `ErrorCode` to `CheckInStatus` before calling `showFailure`.

4. **Severity: Medium**
- Title: RBAC policy drift between shell window gating and documented role matrix
- Conclusion: **Partial Fail**
- Evidence:
  - Shell gate rules: `repo/desktop/src/windows/MainShell.cpp:34`
  - Documented matrix: `docs/design.md:960`
- Impact:
  - Functional access may be over-restricted or over-open relative to intended role semantics, increasing support/compliance risk.
- Minimum actionable fix:
  - Establish one authoritative role-policy table and consume it in both docs and runtime enforcement.

### Low

5. **Severity: Low**
- Title: Audit event-type filter control is not actually applied
- Conclusion: **Fail**
- Evidence:
  - Event-type combo is populated: `repo/desktop/src/windows/AuditViewerWindow.cpp:63`
  - Build filter leaves event-type mapping unresolved: `repo/desktop/src/windows/AuditViewerWindow.cpp:280`
- Impact:
  - Filter UX does not match user expectation; operator review efficiency reduced.
- Minimum actionable fix:
  - Map combo selection to `AuditFilter.eventType` and add unit test asserting filtered result count changes.

6. **Severity: Low (Suspected Risk)**
- Title: Audit retention purge function can break historical chain continuity
- Conclusion: **Cannot Confirm Statistically**
- Evidence:
  - Purge API physically deletes entries: `repo/desktop/src/repositories/AuditRepository.cpp:254`
  - No static call-site found in reviewed code path.
- Impact:
  - If used without compensating anchoring strategy, historical chain verifiability may be fragmented.
- Minimum actionable fix:
  - Guard purge behind explicit retention policy service and record signed chain-anchor snapshots before deletion.

---

## 6. Security Review Summary

- **authentication entry points**: **Pass**
  - Evidence: sign-in/lockout/CAPTCHA/step-up flows in `repo/desktop/src/services/AuthService.cpp:21`, `repo/desktop/src/services/AuthService.cpp:66`, `repo/desktop/src/services/AuthService.cpp:205`
- **route-level authorization (desktop window/action boundary)**: **Partial Pass**
  - Evidence: role-gated windows exist in shell policy `repo/desktop/src/windows/MainShell.cpp:34`, but audit viewer is omitted.
- **object-level authorization**: **Partial Pass**
  - Evidence: many service methods enforce actor role (`requireRoleForActor`) but some sensitive audit reads rely on UI path only (`repo/desktop/src/services/AuditService.cpp:73`).
- **function-level authorization**: **Pass**
  - Evidence: privileged actions consume step-up windows and enforce actor ownership `repo/desktop/src/services/AuthService.cpp:404`, `repo/desktop/src/services/UpdateService.cpp:221`, `repo/desktop/src/services/SyncService.cpp:93`.
- **tenant/user data isolation**: **Cannot Confirm Statistically**
  - Rationale: single-site/offline model is clear, but no runtime multi-tenant execution proof is possible in static audit.
- **admin/internal/debug protection**: **Partial Pass**
  - Evidence: no debug HTTP surface exists; privileged windows mostly security-admin gated; audit viewer authorization remains weak.

---

## 7. Tests and Logging Review

- **Unit tests**: **Pass** (static presence and broad risk coverage)
  - Evidence: `repo/desktop/unit_tests/CMakeLists.txt:1`
- **API/integration tests**: **Pass** (broad workflow coverage)
  - Evidence: `repo/desktop/api_tests/CMakeLists.txt:1`
- **Logging categories/observability**: **Pass**
  - Evidence: structured levels and context scrubbing in `repo/desktop/src/utils/Logger.cpp:112`
- **Sensitive-data leakage risk in logs/responses**: **Partial Pass**
  - Evidence:
    - Positive: redaction/hash in logger context `repo/desktop/src/utils/Logger.cpp:44`
    - Risk: role-gating gaps can expose encrypted audit payload blobs and metadata to lower roles if window access is open (`repo/desktop/src/windows/AuditViewerWindow.cpp:296`).

---

## 8. Test Coverage Assessment (Static Audit)

### 8.1 Test Overview
- Unit and integration suites exist: yes.
- Framework: Qt Test + CTest.
- Test entry points:
  - `repo/desktop/unit_tests/CMakeLists.txt:1`
  - `repo/desktop/api_tests/CMakeLists.txt:1`
- Test commands documented: yes (`repo/run_tests.sh:1`, `repo/README.md:321`).
- Static caution: execution outcomes cannot be confirmed without running.

### 8.2 Coverage Mapping Table

| Requirement / Risk Point | Mapped Test Case(s) | Key Assertion / Fixture / Mock | Coverage Assessment | Gap | Minimum Test Addition |
|---|---|---|---|---|---|
| Lockout 5/10min and CAPTCHA after 3 failures | `repo/desktop/unit_tests/tst_auth_service.cpp:282`, `repo/desktop/unit_tests/tst_auth_service.cpp:326` | Auth flow failures trigger lockout/CAPTCHA thresholds | sufficient | None observed statically | Add explicit cool-down boundary time-edge test (exact 900s boundary) |
| Step-up single-use and privileged action gating | `repo/desktop/unit_tests/tst_privileged_scope.cpp:220`, `repo/desktop/src/services/AuthService.cpp:404` | consume step-up in `authorizePrivilegedAction` | sufficient | None observed statically | Add service-level negative test for cross-user step-up id reuse |
| Check-in duplicate suppression and atomicity | `repo/desktop/unit_tests/tst_checkin_service.cpp:470`, `repo/desktop/src/services/CheckInService.cpp:241` | duplicate checks before and after write-lock with DB-side guard | basically covered | No direct multi-threaded race stress proof | Add API test with concurrent dual check-in attempts same member/session |
| Sync conflict handling and compensating correction linkage | `repo/desktop/api_tests/tst_sync_import_flow.cpp:546` | conflict resolution links compensating action metadata | sufficient | None observed statically | Add rejected/skip conflict path assertions for audit payload completeness |
| Update apply/rollback history integrity | `repo/desktop/api_tests/tst_privileged_scope.cpp:730`, `repo/desktop/src/services/UpdateService.cpp:221` | install history creation and rollback record paths | basically covered | No static evidence of malformed snapshot rollback hardening tests | Add corrupted snapshot rollback test expecting fail-safe abort |
| Audit hash-chain tamper detection | `repo/desktop/unit_tests/tst_audit_chain.cpp:215` | direct tamper update followed by verifyChain failure | sufficient | None observed statically | Add test for chain-head mismatch + retention anchor behavior |
| Query builder combined filters (tag + discrimination + chapter) | `repo/desktop/unit_tests/tst_question_service.cpp` (service-level), UI path in `repo/desktop/src/windows/QuestionBankWindow.cpp:118` | Service supports filter fields; UI lacks controls | insufficient | Prompt-required combined filter not fully exposed in primary UI | Add UI test for tag/discrimination controls and end-to-end filtering |
| Audit access authorization boundaries | No direct test found for low-role denial of audit window/service | Current tests focus on structure/chain behaviors | missing | High-risk authorization path not asserted | Add integration test: FrontDesk/Proctor denied audit query/export when policy requires |

### 8.3 Security Coverage Audit
- authentication: **sufficiently covered** (unit + integration files present for lockout/CAPTCHA/step-up)
- route authorization: **insufficiently covered** (no explicit test found for audit-view authorization denial)
- object-level authorization: **insufficiently covered** (audit query path lacks explicit actor-enforcement test)
- tenant/data isolation: **cannot confirm** (single-site architecture; no runtime tenant model exercised statically)
- admin/internal protection: **basically covered** for update/sync/data-subject privileged paths, but with the audit-window gap

### 8.4 Final Coverage Judgment
- **Partial Pass**
- Covered major risks:
  - Auth controls, check-in core flow, sync/update signature and conflict flows, audit-chain tamper detection.
- Uncovered risks that could allow severe defects while tests still pass:
  - Authorization regressions around audit viewing/query/export boundaries.
  - UI-level query-builder completeness for prompt-critical combined filters.

---

## 9. Final Notes
- This audit is static-only; no runtime success is claimed.
- Conclusions are evidence-based from reviewed files and cited lines.
- Major residual acceptance risk is concentrated in authorization boundary enforcement consistency and UI requirement fit for content query composition.
