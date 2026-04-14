# unit_tests/CMakeLists.txt
# ProctorOps unit test suite — Qt Test / CTest
#
# Tests are organized one executable per module. Each executable is registered
# with CTest. New test targets are added here as modules are implemented.
#
# Active targets (27):
#   tst_bootstrap — verifies Qt Test framework and CMake integration
#   tst_app_settings — AppSettings default values, round-trip, and path honesty
#   tst_domain_validation — domain model invariants and Validation.h constants
#   tst_migration — schema_migrations infrastructure and file ordering
#   tst_crypto — crypto primitives (SecureRandom, Argon2id, AES-GCM, Ed25519 Verifier+Signer, HashChain)
#   tst_auth_service — auth sign-in, lockout, CAPTCHA, step-up expiry, RBAC, console lock
#   tst_audit_chain — audit hash chain linkage, PII encryption, chain verification
#   tst_clipboard_guard — clipboard masking and PII redaction
#   tst_masked_field — MaskingPolicy field masking rules
#   tst_question_service — question CRUD, query builder, KP tree, mappings, tags
#   tst_checkin_service — check-in flow, mobile normalization, corrections
#   tst_job_scheduler — priority, dependency, retry, fairness, worker cap, crash recovery
#   tst_job_checkpoint — checkpoint save/restore across phases
#   tst_ingestion_service — JSONL/CSV import pipeline, lifecycle
#   tst_action_routing — named action registry, shortcut dispatch
#   tst_command_palette — Ctrl+K fuzzy action search
#   tst_workspace_state — workspace state persistence
#   tst_crash_recovery — crash detection and restoration
#   tst_tray_mode — system tray state transitions, kiosk mode
#   tst_checkin_window — CheckInWindow UI structure
#   tst_question_editor — QuestionEditorDialog UI structure
#   tst_audit_viewer — AuditViewerWindow UI structure
#   tst_sync_service — signing key management, package import verification, conflict detection
#   tst_update_service — update package lifecycle, digest verification, rollback records
#   tst_data_subject_service — GDPR export/deletion request workflows, redaction, retention
#   tst_security_admin_window — tab structure, button states, widget accessibility
#   tst_privileged_scope — RBAC, step-up, masking, state machines, validation constraints

find_package(Qt6 REQUIRED COMPONENTS Test Sql Widgets)

# ── Helper macro: define a Qt Test target ─────────────────────────────────────
# Usage: add_proctorops_test(TARGET_NAME MAIN_SOURCE [EXTRA_SOURCES...])
macro(add_proctorops_test TARGET_NAME SOURCE_FILE)
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
        TIMEOUT 60
    )
    # SOURCE_ROOT gives test code a reliable path to database/migrations and
    # database/fixtures without embedding developer-machine absolute paths.
    target_compile_definitions(${TARGET_NAME} PRIVATE
        SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    )
endmacro()

# ── Bootstrap and framework tests ────────────────────────────────────────────
add_proctorops_test(tst_bootstrap     tst_bootstrap.cpp)
add_proctorops_test(tst_app_settings  tst_app_settings.cpp
    ../src/app/AppSettings.cpp)

# ── Domain validation and migration tests ─────────────────────────────────────
add_proctorops_test(tst_domain_validation tst_domain_validation.cpp)
add_proctorops_test(tst_migration         tst_migration.cpp)

# ── Security and crypto unit tests ────────────────────────────────────────────
add_proctorops_test(tst_crypto            tst_crypto.cpp)
add_proctorops_test(tst_auth_service      tst_auth_service.cpp)
add_proctorops_test(tst_audit_chain       tst_audit_chain.cpp)
add_proctorops_test(tst_clipboard_guard   tst_clipboard_guard.cpp)
add_proctorops_test(tst_masked_field      tst_masked_field.cpp)

# ── Core engine unit tests ────────────────────────────────────────────────────
add_proctorops_test(tst_question_service  tst_question_service.cpp)
add_proctorops_test(tst_checkin_service   tst_checkin_service.cpp)
add_proctorops_test(tst_job_scheduler     tst_job_scheduler.cpp)
add_proctorops_test(tst_job_checkpoint    tst_job_checkpoint.cpp)
add_proctorops_test(tst_ingestion_service tst_ingestion_service.cpp)

# ── Shell and runtime unit tests ─────────────────────────────────────────────
# Extra source files are compiled into each test executable because ProctorOpsCore
# is an INTERFACE library (header/link only). Shell tests compile only the specific
# modules they exercise, keeping each test target small and dependency-explicit.

add_proctorops_test(tst_action_routing  tst_action_routing.cpp
    ../src/app/ActionRouter.cpp)

add_proctorops_test(tst_command_palette tst_command_palette.cpp
    ../src/app/ActionRouter.cpp
    ../src/dialogs/CommandPaletteDialog.cpp)

add_proctorops_test(tst_workspace_state tst_workspace_state.cpp
    ../src/app/WorkspaceState.cpp)

add_proctorops_test(tst_crash_recovery  tst_crash_recovery.cpp
    ../src/app/WorkspaceState.cpp)

add_proctorops_test(tst_tray_mode       tst_tray_mode.cpp
    ../src/tray/TrayManager.cpp)

# ── Operator workflow window tests ────────────────────────────────────────────
add_proctorops_test(tst_checkin_window  tst_checkin_window.cpp
    ../src/windows/CheckInWindow.cpp
    ../src/widgets/BarcodeInput.cpp)

add_proctorops_test(tst_question_editor tst_question_editor.cpp
    ../src/dialogs/QuestionEditorDialog.cpp)

add_proctorops_test(tst_audit_viewer    tst_audit_viewer.cpp
    ../src/windows/AuditViewerWindow.cpp)

# ── Sync, update, and compliance unit tests ───────────────────────────────────
add_proctorops_test(tst_sync_service          tst_sync_service.cpp)
add_proctorops_test(tst_update_service        tst_update_service.cpp)
add_proctorops_test(tst_data_subject_service  tst_data_subject_service.cpp)

# ── Security administration window unit tests ─────────────────────────────────
add_proctorops_test(tst_security_admin_window tst_security_admin_window.cpp
    ../src/windows/SecurityAdminWindow.cpp
    ../src/dialogs/StepUpDialog.cpp)

# ── Privileged-action scope enforcement unit tests ────────────────────────────
add_proctorops_test(tst_privileged_scope tst_privileged_scope.cpp)
