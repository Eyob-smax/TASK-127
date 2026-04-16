# unit_tests/CMakeLists.txt
# ProctorOps unit test suite — Qt Test / CTest
#
# Tests are organized one executable per module. Each executable is registered
# with CTest. New test targets are added here as modules are implemented.
#
# Active targets (48):
#   tst_bootstrap — verifies Qt Test framework and CMake integration
#   tst_app_settings — AppSettings default values, round-trip, and path honesty
#   tst_domain_validation — domain model invariants and Validation.h constants
#   tst_migration — schema_migrations infrastructure and file ordering
#   tst_crypto — crypto primitives (SecureRandom, Argon2id, AES-GCM, Ed25519 Verifier+Signer, HashChain)
#   tst_auth_service — auth sign-in, lockout, CAPTCHA, step-up expiry, RBAC, console lock
#   tst_audit_chain — audit hash chain linkage, PII encryption, chain verification
#   tst_clipboard_guard — clipboard masking and PII redaction
#   tst_masked_field — MaskingPolicy field masking rules
#   tst_error_formatter — ErrorFormatter user-facing message, form hint, security classification
#   tst_key_store — KeyStore master key persistence, rotation, and secret round-trip
#   tst_logger — structured logging output and sensitive-field scrubbing
#   tst_captcha_generator — CAPTCHA generation and answer verification
#   tst_performance_observer — cold-start and memory-sample recorder, shutdown event
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
#   tst_masked_field_widget — MaskedFieldWidget mask/reveal/copy behavior
#   tst_sync_service — signing key management, package import verification, conflict detection
#   tst_update_service — update package lifecycle, digest verification, rollback records
#   tst_data_subject_service — GDPR export/deletion request workflows, redaction, retention
#   tst_security_admin_window — tab structure, button states, widget accessibility
#   tst_privileged_scope — RBAC, step-up, masking, state machines, validation constraints
#   tst_login_window — LoginWindow structure, CAPTCHA visibility, bootstrap mode
#   tst_step_up_dialog — StepUpDialog structure and step-up confirm flow
#   tst_sync_window — SyncWindow tab layout and disabled row-scoped actions
#   tst_update_window — UpdateWindow tab layout and staged-action button states
#   tst_data_subject_window — DataSubjectWindow tab layout and request-action button states
#   tst_ingestion_monitor_window — IngestionMonitorWindow structure and unavailable-service state
#   tst_main_shell — MainShell MDI workspace lifecycle, window dedup, lock/unlock, action registration
#   tst_question_bank_window — QuestionBankWindow paginated table, filter panel, initial load, activateFilter
#   tst_application — Application startup lifecycle, database open, cold-start timing, clean shutdown
#   tst_app_bootstrap — startup AppContext wiring extracted from main bootstrap path
#   tst_main_entrypoint — static orchestration contract checks for src/main.cpp startup flow
#   tst_app_context — AppContext default session state and owned component management
#   tst_repository_contracts — direct repository contract coverage across data-access modules
#   tst_package_verifier — package signature and digest verification unit coverage

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
add_proctorops_test(tst_error_formatter   tst_error_formatter.cpp)
add_proctorops_test(tst_key_store         tst_key_store.cpp)
add_proctorops_test(tst_logger            tst_logger.cpp)
add_proctorops_test(tst_captcha_generator tst_captcha_generator.cpp)
add_proctorops_test(tst_performance_observer tst_performance_observer.cpp)

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

add_proctorops_test(tst_masked_field_widget tst_masked_field_widget.cpp
    ../src/widgets/MaskedFieldWidget.cpp)

add_proctorops_test(tst_barcode_input tst_barcode_input.cpp
    ../src/widgets/BarcodeInput.cpp)

add_proctorops_test(tst_login_window    tst_login_window.cpp
    ../src/windows/LoginWindow.cpp)

add_proctorops_test(tst_step_up_dialog  tst_step_up_dialog.cpp
    ../src/dialogs/StepUpDialog.cpp)

add_proctorops_test(tst_sync_window     tst_sync_window.cpp
    ../src/windows/SyncWindow.cpp
    ../src/dialogs/StepUpDialog.cpp)

add_proctorops_test(tst_update_window   tst_update_window.cpp
    ../src/windows/UpdateWindow.cpp
    ../src/dialogs/StepUpDialog.cpp)

add_proctorops_test(tst_data_subject_window tst_data_subject_window.cpp
    ../src/windows/DataSubjectWindow.cpp
    ../src/dialogs/StepUpDialog.cpp)

add_proctorops_test(tst_ingestion_monitor_window tst_ingestion_monitor_window.cpp
    ../src/windows/IngestionMonitorWindow.cpp)

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

# ── MainShell and QuestionBankWindow unit tests ───────────────────────────────
add_proctorops_test(tst_main_shell           tst_main_shell.cpp
    ../src/windows/MainShell.cpp
    ../src/dialogs/CommandPaletteDialog.cpp
    ../src/dialogs/QuestionEditorDialog.cpp
    ../src/dialogs/StepUpDialog.cpp
    ../src/tray/TrayManager.cpp
    ../src/windows/CheckInWindow.cpp
    ../src/windows/QuestionBankWindow.cpp
    ../src/windows/AuditViewerWindow.cpp
    ../src/windows/SyncWindow.cpp
    ../src/windows/DataSubjectWindow.cpp
    ../src/windows/SecurityAdminWindow.cpp
    ../src/windows/UpdateWindow.cpp
    ../src/windows/IngestionMonitorWindow.cpp
    ../src/widgets/BarcodeInput.cpp)

add_proctorops_test(tst_question_bank_window tst_question_bank_window.cpp
    ../src/windows/QuestionBankWindow.cpp
    ../src/dialogs/QuestionEditorDialog.cpp)

# ── Application startup lifecycle and AppContext unit tests ───────────────────
add_proctorops_test(tst_application          tst_application.cpp
    ../src/app/Application.cpp)

add_proctorops_test(tst_app_bootstrap        tst_app_bootstrap.cpp
    ../src/app/AppBootstrap.cpp
    ../src/app/AppSettings.cpp)

add_proctorops_test(tst_main_entrypoint      tst_main_entrypoint.cpp)

add_proctorops_test(tst_app_context          tst_app_context.cpp)

# ── Repository and package verification contract tests ──────────────────────
add_proctorops_test(tst_repository_contracts tst_repository_contracts.cpp)
add_proctorops_test(tst_package_verifier     tst_package_verifier.cpp)
