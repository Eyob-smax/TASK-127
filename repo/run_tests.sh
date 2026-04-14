#!/usr/bin/env bash
# run_tests.sh — ProctorOps docker-first test orchestrator
#
# All tests are executed inside Docker. This script does not install any
# dependencies on the host machine. It does not run the application natively.
#
# Usage:
#   ./repo/run_tests.sh [--rebuild] [--test-filter <pattern>]
#                        [--unit-only] [--api-only]
#
# Options:
#   --rebuild        Force a full Docker image rebuild before running tests.
#   --test-filter    Pass a CTest regex filter to run only matching tests.
#   --unit-only      Run only unit tests (unit_tests/ targets).
#   --api-only       Run only integration tests (api_tests/ targets).
#
# Requirements:
#   - Docker Engine 24+ with compose v2 plugin
#   - Run from the TASK-127 root directory (i.e., bash repo/run_tests.sh)
#
# Test suites (27 unit tests, 13 integration tests):
#
#   Unit tests (unit_tests/):
#     tst_bootstrap              — Qt Test framework bootstrap
#     tst_app_settings           — AppSettings defaults, round-trip, path honesty
#     tst_domain_validation      — domain model invariants, Validation.h constants
#     tst_migration              — schema_migrations infrastructure
#     tst_crypto                 — SecureRandom, Argon2id, AES-GCM, Ed25519 Verifier+Signer, HashChain
#     tst_auth_service           — sign-in, lockout, CAPTCHA, step-up, RBAC, console lock
#     tst_audit_chain            — audit hash chain linkage, PII encryption
#     tst_clipboard_guard        — clipboard masking and PII redaction
#     tst_masked_field           — MaskingPolicy field masking rules
#     tst_question_service       — question CRUD, query builder, KP tree, mappings, tags
#     tst_checkin_service        — check-in flow, mobile normalization, corrections
#     tst_job_scheduler          — priority, dependency, retry, fairness, worker cap, crash recovery
#     tst_job_checkpoint         — checkpoint save/restore across phases
#     tst_ingestion_service      — JSONL/CSV import pipeline, lifecycle
#     tst_action_routing         — named action registry, shortcut dispatch
#     tst_command_palette        — Ctrl+K fuzzy action search
#     tst_workspace_state        — workspace state persistence
#     tst_crash_recovery         — crash detection and restoration
#     tst_tray_mode              — system tray state transitions, kiosk mode
#     tst_checkin_window         — CheckInWindow UI structure
#     tst_question_editor        — QuestionEditorDialog UI structure
#     tst_audit_viewer           — AuditViewerWindow UI structure
#     tst_sync_service           — signing key management, package import, conflicts
#     tst_update_service         — package staging, rollback, install history
#     tst_data_subject_service   — GDPR export/deletion workflows, redaction
#     tst_security_admin_window  — SecurityAdminWindow tab/button structure
#     tst_privileged_scope       — RBAC, step-up, masking, state machines, validation
#
#   Integration tests (api_tests/):
#     tst_api_bootstrap          — test infrastructure bootstrap
#     tst_schema_constraints     — schema constraint enforcement
#     tst_auth_integration       — full auth flow with real DB
#     tst_audit_integration      — audit chain integrity, PII encryption
#     tst_package_verification   — Ed25519 + SHA-256 package verification
#     tst_checkin_flow           — full check-in with dedup and deduction
#     tst_correction_flow        — correction request → approve → apply
#     tst_shell_recovery         — crash detection, workspace restoration
#     tst_operator_workflows     — check-in + question governance integration
#     tst_export_flow            — GDPR export → fulfill → redaction → audit
#     tst_sync_import_flow       — sync package verify → import → conflict → key revocation
#     tst_update_flow            — update import → stage → apply → rollback
#     tst_privileged_scope_api   — privileged flows, compliance evidence, authorization

set -euo pipefail

# Reduce noisy orphan warnings in scripted compose runs.
export COMPOSE_IGNORE_ORPHANS=True

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPOSE_FILE="${SCRIPT_DIR}/docker-compose.yml"

REBUILD=false
TEST_FILTER=""
UNIT_ONLY=false
API_ONLY=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --rebuild)
            REBUILD=true
            shift
            ;;
        --test-filter)
            TEST_FILTER="$2"
            shift 2
            ;;
        --unit-only)
            UNIT_ONLY=true
            shift
            ;;
        --api-only)
            API_ONLY=true
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            echo "Usage: $0 [--rebuild] [--test-filter <pattern>] [--unit-only] [--api-only]" >&2
            exit 1
            ;;
    esac
done

# Validate mutually exclusive flags
if [[ "${UNIT_ONLY}" == "true" && "${API_ONLY}" == "true" ]]; then
    echo "Error: --unit-only and --api-only are mutually exclusive." >&2
    exit 1
fi

# Build suite filter from --unit-only / --api-only
SUITE_FILTER=""
UNIT_TARGETS=(
    tst_bootstrap tst_app_settings tst_domain_validation tst_migration tst_crypto
    tst_auth_service tst_audit_chain tst_clipboard_guard tst_masked_field
    tst_question_service tst_checkin_service tst_job_scheduler tst_job_checkpoint
    tst_ingestion_service tst_action_routing tst_command_palette tst_workspace_state
    tst_crash_recovery tst_tray_mode tst_checkin_window tst_question_editor
    tst_audit_viewer tst_sync_service tst_update_service tst_data_subject_service
    tst_security_admin_window tst_privileged_scope
)

API_TARGETS=(
    tst_api_bootstrap tst_schema_constraints tst_auth_integration tst_audit_integration
    tst_package_verification tst_checkin_flow tst_correction_flow tst_shell_recovery
    tst_operator_workflows tst_export_flow tst_sync_import_flow tst_update_flow
    tst_privileged_scope_api
)

if [[ "${UNIT_ONLY}" == "true" ]]; then
    SUITE_FILTER="tst_(bootstrap|app_settings|domain_validation|migration|crypto|auth_service|audit_chain|clipboard_guard|masked_field|question_service|checkin_service|job_scheduler|job_checkpoint|ingestion_service|action_routing|command_palette|workspace_state|crash_recovery|tray_mode|checkin_window|question_editor|audit_viewer|sync_service|update_service|data_subject_service|security_admin_window|privileged_scope)"
elif [[ "${API_ONLY}" == "true" ]]; then
    SUITE_FILTER="tst_(api_bootstrap|schema_constraints|auth_integration|audit_integration|package_verification|checkin_flow|correction_flow|shell_recovery|operator_workflows|export_flow|sync_import_flow|update_flow|privileged_scope_api)"
fi

# Combine filters: --test-filter takes priority, but --unit-only/--api-only can
# narrow scope further. If both are set, use test-filter (more specific).
EFFECTIVE_FILTER="${TEST_FILTER:-${SUITE_FILTER}}"

echo "=========================================="
echo "  ProctorOps — Docker Test Runner"
echo "=========================================="
echo "  Compose file: ${COMPOSE_FILE}"
echo "  Rebuild:      ${REBUILD}"
if [[ "${UNIT_ONLY}" == "true" ]]; then
    echo "  Suite:        unit tests only (27 targets)"
elif [[ "${API_ONLY}" == "true" ]]; then
    echo "  Suite:        integration tests only (13 targets)"
else
    echo "  Suite:        all tests (27 unit + 13 integration)"
fi
echo "  Test filter:  ${EFFECTIVE_FILTER:-<all>}"
echo "=========================================="

# Build or pull the test image
if [[ "${REBUILD}" == "true" ]]; then
    echo ""
    echo "[1/3] Rebuilding Docker image (--rebuild requested)..."
    docker compose -f "${COMPOSE_FILE}" build --no-cache build-test
else
    echo ""
    echo "[1/3] Building Docker image (incremental)..."
    docker compose -f "${COMPOSE_FILE}" build build-test
fi

# Flatten target arrays for safe environment transport into the container shell.
UNIT_TARGETS_CMAKE="${UNIT_TARGETS[*]}"
API_TARGETS_CMAKE="${API_TARGETS[*]}"

echo ""
echo "[2/3] Running tests inside Docker container..."
# Qt6_DIR is set by the Dockerfile ENV and inherited automatically.
# The cmake invocation here must match the Dockerfile builder stage exactly.
CONTAINER_SCRIPT="$(cat <<'EOF'
set -euo pipefail
echo "Configuring CMake..."
cmake -B build -S . -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_C_COMPILER=gcc \
    -DQt6_DIR="${Qt6_DIR}"

echo "Building..."
BUILD_TARGET_ARGS=()
if [[ "${RUN_UNIT_ONLY}" == "true" ]]; then
    read -r -a TARGET_LIST <<< "${RUN_UNIT_TARGETS}"
    BUILD_TARGET_ARGS=(--target "${TARGET_LIST[@]}")
elif [[ "${RUN_API_ONLY}" == "true" ]]; then
    read -r -a TARGET_LIST <<< "${RUN_API_TARGETS}"
    BUILD_TARGET_ARGS=(--target "${TARGET_LIST[@]}")
fi
cmake --build build --parallel "$(nproc)" "${BUILD_TARGET_ARGS[@]}"

echo "Running tests..."
cd build
CTEST_ARGS=(--output-on-failure --parallel 2)
if [[ -n "${RUN_EFFECTIVE_FILTER}" ]]; then
    CTEST_ARGS+=(-R "${RUN_EFFECTIVE_FILTER}")
fi
ctest "${CTEST_ARGS[@]}"

echo ""
echo "-- Test Summary --"
COUNT_ARGS=(-N)
if [[ -n "${RUN_EFFECTIVE_FILTER}" ]]; then
    COUNT_ARGS+=(-R "${RUN_EFFECTIVE_FILTER}")
fi
TOTAL=$(ctest "${COUNT_ARGS[@]}" 2>/dev/null | tail -1 | grep -oE '[0-9]+' || echo '?')

CASE_TOTAL=0
for exe in unit_tests/tst_* api_tests/tst_*; do
    if [[ -x "${exe}" ]]; then
        fn_count=$("./${exe}" -functions 2>/dev/null | sed '/^[[:space:]]*$/d' | wc -l | tr -d '[:space:]')
        CASE_TOTAL=$((CASE_TOTAL + fn_count))
    fi
done

echo "Total test targets: ${TOTAL}"
echo "Total QTest functions: ${CASE_TOTAL}"
EOF
)"

docker compose -f "${COMPOSE_FILE}" run --rm \
    -e CTEST_OUTPUT_ON_FAILURE=1 \
    -e QT_QPA_PLATFORM=offscreen \
    -e RUN_UNIT_ONLY="${UNIT_ONLY}" \
    -e RUN_API_ONLY="${API_ONLY}" \
    -e RUN_EFFECTIVE_FILTER="${EFFECTIVE_FILTER}" \
    -e RUN_UNIT_TARGETS="${UNIT_TARGETS_CMAKE}" \
    -e RUN_API_TARGETS="${API_TARGETS_CMAKE}" \
    build-test bash -lc "${CONTAINER_SCRIPT}"

EXIT_CODE=$?

echo ""
echo "[3/3] Cleaning up..."
docker compose -f "${COMPOSE_FILE}" down --remove-orphans 2>/dev/null || true

echo ""
if [[ ${EXIT_CODE} -eq 0 ]]; then
    echo "=========================================="
    echo "  All tests PASSED"
    echo "=========================================="
else
    echo "=========================================="
    echo "  Tests FAILED (exit code: ${EXIT_CODE})"
    echo "=========================================="
fi

exit ${EXIT_CODE}
