# audit_report-1 Fix Check (Round 2)

## Scope
- Re-verified all issues listed in `.tmp/audit_report-1.md`.
- Static-only check (no runtime execution, no Docker, no tests run).

## Summary
- Issues reviewed: 6
- Fixed: 6
- Open: 0

---

## Detailed Re-check

### 1) High: Audit log access not role-restricted in shell/service path
- Status: **Fixed**
- Evidence:
  - Audit window is now explicitly role-gated to SecurityAdministrator in shell policy:
    - `repo/desktop/src/windows/MainShell.cpp:34`
  - Service-layer audit query authorization added:
    - `repo/desktop/src/services/AuditService.cpp:85`
    - `repo/desktop/src/services/AuditService.cpp:94`
  - Auth service wiring into audit service is present at startup:
    - `repo/desktop/src/main.cpp:121`
    - `repo/desktop/src/main.cpp:124`
  - Integration tests cover deny/allow behavior:
    - `repo/desktop/api_tests/tst_audit_integration.cpp:321`
    - `repo/desktop/api_tests/tst_audit_integration.cpp:356`

### 2) High: Query builder UI missing full combined filters (tag/discrimination)
- Status: **Fixed**
- Evidence:
  - Discrimination controls added to filter panel:
    - `repo/desktop/src/windows/QuestionBankWindow.cpp:156`
  - Tag filter control added and populated:
    - `repo/desktop/src/windows/QuestionBankWindow.cpp:176`
  - New controls mapped into `QuestionFilter`:
    - `repo/desktop/src/windows/QuestionBankWindow.cpp:248`
    - `repo/desktop/src/windows/QuestionBankWindow.cpp:258`

### 3) Medium: Check-in UI collapsed typed failures to generic failure
- Status: **Fixed**
- Evidence:
  - `ErrorCode` to `CheckInStatus` mapping now implemented before display:
    - `repo/desktop/src/windows/CheckInWindow.cpp:196`
    - `repo/desktop/src/windows/CheckInWindow.cpp:205`

### 4) Medium: RBAC policy drift between shell gating and documented role matrix
- Status: **Fixed**
- Evidence:
  - Shell window-role map now includes:
    - Audit viewer -> SecurityAdministrator
    - Question bank view -> Proctor+
    - `repo/desktop/src/windows/MainShell.cpp:34`
    - `repo/desktop/src/windows/MainShell.cpp:51`
  - Design matrix remains aligned:
    - `docs/design.md:960`
    - `docs/design.md:966`
    - `docs/design.md:969`

### 5) Low: Audit event-type filter control not applied
- Status: **Fixed**
- Evidence:
  - Event type now mapped from combo selection into `AuditFilter.eventType`:
    - `repo/desktop/src/windows/AuditViewerWindow.cpp:292`
  - Query call uses the built filter:
    - `repo/desktop/src/windows/AuditViewerWindow.cpp:309`

### 6) Low (Suspected): Retention purge may break chain continuity
- Status: **Fixed**
- Evidence:
  - Purge operation moved behind privileged service gate + step-up + minimum retention enforcement:
    - `repo/desktop/src/services/AuditService.cpp:156`
    - `repo/desktop/src/services/AuditService.cpp:167`
    - `repo/desktop/src/services/AuditService.cpp:183`
  - Repository purge now records chain anchor boundary and blocks full-chain deletion:
    - `repo/desktop/src/repositories/AuditRepository.cpp:254`
    - `repo/desktop/src/repositories/AuditRepository.cpp:268`
    - `repo/desktop/src/repositories/AuditRepository.cpp:280`

---

## Final Conclusion
All six issues from `.tmp/audit_report-1.md` are now fixed based on current static code evidence.
