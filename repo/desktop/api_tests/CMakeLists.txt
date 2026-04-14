# api_tests/CMakeLists.txt
# ProctorOps integration-style tests for internal service contracts.
#
# These tests exercise service-to-repository interactions, multi-step
# workflows, and cross-service flows using a real in-memory or temp-file
# SQLite database. No mocks substitute for real service or repository
# implementations.
#
# Active targets (13):
#   tst_api_bootstrap          — verifies test infrastructure compiles and SQLite is available
#   tst_schema_constraints     — schema-level constraint enforcement (UNIQUE, CHECK, FK, dedup)
#   tst_auth_integration       — full auth flow: login, lockout, CAPTCHA, step-up, RBAC, console lock
#   tst_audit_integration      — audit chain integrity, PII encryption, secret-safe logging
#   tst_package_verification   — Ed25519 signature + SHA-256 digest verification for packages
#   tst_checkin_flow           — full check-in including dedup and punch-card deduction
#   tst_correction_flow        — full correction request → step-up → approve → audit flow
#   tst_shell_recovery         — crash detection, workspace restoration, graceful shutdown
#   tst_operator_workflows     — check-in, correction, question governance integration flows
#   tst_export_flow            — GDPR export request → fulfill → redaction → audit flow
#   tst_sync_import_flow       — sync package verify → conflict detect → key revocation flow
#   tst_update_flow            — update package import → stage → apply → rollback flow
#   tst_privileged_scope_api   — privileged flows, compliance evidence, authorization boundaries

find_package(Qt6 REQUIRED COMPONENTS Test Sql Widgets)

macro(add_proctorops_api_test TARGET_NAME SOURCE_FILE)
    qt_add_executable(${TARGET_NAME} ${SOURCE_FILE} ${ARGN})
    target_link_libraries(${TARGET_NAME} PRIVATE
        Qt6::Test
        Qt6::Sql
        Qt6::Widgets
        ProctorOpsCore
    )
    add_test(
        NAME ${TARGET_NAME}
        COMMAND ${TARGET_NAME}
    )
    set_tests_properties(${TARGET_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120
    )
    # SOURCE_ROOT gives test code a reliable path to database/migrations and
    # database/fixtures without embedding developer-machine absolute paths.
    target_compile_definitions(${TARGET_NAME} PRIVATE
        SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    )
endmacro()

# ── API test infrastructure bootstrap ────────────────────────────────────────
add_proctorops_api_test(tst_api_bootstrap tst_api_bootstrap.cpp)

# ── Schema constraint integration tests ───────────────────────────────────────
add_proctorops_api_test(tst_schema_constraints tst_schema_constraints.cpp)

# ── Security and auth integration tests ───────────────────────────────────────
add_proctorops_api_test(tst_auth_integration     tst_auth_integration.cpp)
add_proctorops_api_test(tst_audit_integration    tst_audit_integration.cpp)
add_proctorops_api_test(tst_package_verification tst_package_verification.cpp)

# ── Core engine integration tests ─────────────────────────────────────────────
add_proctorops_api_test(tst_checkin_flow      tst_checkin_flow.cpp)
add_proctorops_api_test(tst_correction_flow   tst_correction_flow.cpp)

# ── Shell recovery integration test ──────────────────────────────────────────
add_proctorops_api_test(tst_shell_recovery tst_shell_recovery.cpp
    ../src/app/WorkspaceState.cpp)

# ── Operator workflow integration tests ───────────────────────────────────────
add_proctorops_api_test(tst_operator_workflows tst_operator_workflows.cpp)

# ── Compliance and sync workflow integration tests ────────────────────────────
add_proctorops_api_test(tst_export_flow       tst_export_flow.cpp)
add_proctorops_api_test(tst_sync_import_flow  tst_sync_import_flow.cpp)
add_proctorops_api_test(tst_update_flow       tst_update_flow.cpp)

# ── Privileged-action scope integration tests ─────────────────────────────────
add_proctorops_api_test(tst_privileged_scope_api  tst_privileged_scope.cpp)
