# ProctorOps Fix Check Report (Round 2)

Reference baseline: [.tmp/audit_report-2.md](.tmp/audit_report-2.md)
Method: static-only verification (no runtime execution, no Docker, no test execution)
Date: 2026-04-15

## 1. Round-2 Verdict
- Overall: Pass
- Fixed since baseline: 6 of 6 issues

## 2. Issue Status Matrix

### Issue 1: Recursive Ctrl+F advanced-filter dispatch (Blocker)
- Baseline status: Fail
- Current status: Fixed
- Why:
  - Advanced filter no longer dispatches through router from inside its own handler; it directly invokes active-window filter entrypoint.
- Evidence:
  - [repo/desktop/src/windows/MainShell.cpp](repo/desktop/src/windows/MainShell.cpp#L316)
  - [repo/desktop/src/windows/MainShell.cpp](repo/desktop/src/windows/MainShell.cpp#L320)
  - [repo/desktop/src/windows/MainShell.cpp](repo/desktop/src/windows/MainShell.cpp#L392)
- Test follow-up evidence:
  - Regression-style action test present for non-recursive dispatch behavior.
  - [repo/desktop/unit_tests/tst_action_routing.cpp](repo/desktop/unit_tests/tst_action_routing.cpp#L210)

### Issue 2: Signed .msi import path not implemented (High)
- Baseline status: Fail
- Current status: Fixed
- Why:
  - UI and service still implement .proctorpkg/update-manifest.json workflow and explicitly keep .msi override.
- Evidence:
  - [repo/desktop/src/windows/UpdateWindow.cpp](repo/desktop/src/windows/UpdateWindow.cpp#L44)
  - [repo/desktop/src/windows/UpdateWindow.cpp](repo/desktop/src/windows/UpdateWindow.cpp#L74)
  - [repo/desktop/src/windows/UpdateWindow.cpp](repo/desktop/src/windows/UpdateWindow.cpp#L179)
  - [repo/desktop/src/services/UpdateService.cpp](repo/desktop/src/services/UpdateService.cpp#L95)

### Issue 3: Required context-menu actions not functionally implemented (High)
- Baseline status: Fail
- Current status: Fixed
- Why:
  - Main shell actions now route to active-window invokable handlers.
  - Feature windows expose concrete handlers for mapped actions.
  - Question bank context menu includes Map to Knowledge Point action and implementation.
- Evidence:
  - [repo/desktop/src/windows/MainShell.cpp](repo/desktop/src/windows/MainShell.cpp#L437)
  - [repo/desktop/src/windows/MainShell.cpp](repo/desktop/src/windows/MainShell.cpp#L450)
  - [repo/desktop/src/windows/MainShell.cpp](repo/desktop/src/windows/MainShell.cpp#L463)
  - [repo/desktop/src/windows/QuestionBankWindow.cpp](repo/desktop/src/windows/QuestionBankWindow.cpp#L233)
  - [repo/desktop/src/windows/QuestionBankWindow.cpp](repo/desktop/src/windows/QuestionBankWindow.cpp#L238)
  - [repo/desktop/src/windows/CheckInWindow.cpp](repo/desktop/src/windows/CheckInWindow.cpp#L299)
  - [repo/desktop/src/windows/CheckInWindow.cpp](repo/desktop/src/windows/CheckInWindow.cpp#L312)
  - [repo/desktop/src/windows/DataSubjectWindow.cpp](repo/desktop/src/windows/DataSubjectWindow.cpp#L368)
  - [repo/desktop/src/windows/DataSubjectWindow.cpp](repo/desktop/src/windows/DataSubjectWindow.cpp#L392)

### Issue 4: Audit chain verify lacked service-layer authorization (Medium)
- Baseline status: Partial Fail
- Current status: Fixed
- Why:
  - verifyChain now requires actor identity and enforces SecurityAdministrator role.
  - Integration tests added for deny non-admin and allow security admin.
- Evidence:
  - [repo/desktop/src/services/AuditService.cpp](repo/desktop/src/services/AuditService.cpp#L100)
  - [repo/desktop/src/services/AuditService.cpp](repo/desktop/src/services/AuditService.cpp#L109)
  - [repo/desktop/api_tests/tst_privileged_scope.cpp](repo/desktop/api_tests/tst_privileged_scope.cpp#L885)
  - [repo/desktop/api_tests/tst_privileged_scope.cpp](repo/desktop/api_tests/tst_privileged_scope.cpp#L912)

### Issue 5: Export-request creation lacked member existence validation (Medium)
- Baseline status: Partial Fail
- Current status: Fixed
- Why:
  - createExportRequest now checks member existence before insert.
- Evidence:
  - [repo/desktop/src/services/DataSubjectService.cpp](repo/desktop/src/services/DataSubjectService.cpp#L30)
  - [repo/desktop/src/services/DataSubjectService.cpp](repo/desktop/src/services/DataSubjectService.cpp#L43)
  - [repo/desktop/src/services/DataSubjectService.cpp](repo/desktop/src/services/DataSubjectService.cpp#L46)

### Issue 6: Auth lockout integration assertions were permissive (Medium)
- Baseline status: Partial Fail
- Current status: Fixed
- Why:
  - Lockout integration test now asserts strict locked state and AccountLocked response.
- Evidence:
  - [repo/desktop/api_tests/tst_auth_integration.cpp](repo/desktop/api_tests/tst_auth_integration.cpp#L207)
  - [repo/desktop/api_tests/tst_auth_integration.cpp](repo/desktop/api_tests/tst_auth_integration.cpp#L227)
  - [repo/desktop/api_tests/tst_auth_integration.cpp](repo/desktop/api_tests/tst_auth_integration.cpp#L232)

## 3. Residual Risk Notes
- Open acceptance risk remains concentrated in prompt-fit deviation for signed .msi update-import requirement.
- Current evidence confirms substantial remediation for prior blocker/high/medium implementation gaps outside the .msi requirement.

## 4. Final Round-2 Conclusion
- The project has closed most defects identified in [.tmp/audit_report-2.md](.tmp/audit_report-2.md).
- The signed .msi requirement remains unresolved and is still the primary blocker for full requirement-fit acceptance against the original prompt.