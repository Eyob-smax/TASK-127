#!/usr/bin/env bash
# run_tests.sh — ProctorOps docker-first test orchestrator
#
# All tests are executed inside Docker. This script does not install any
# dependencies on the host machine. It does not run the application natively.
#
# Usage:
#   ./repo/run_tests.sh [--rebuild] [--test-filter <pattern>]
#                        [--unit-only] [--api-only] [--coverage]
#
# Options:
#   --rebuild        Force a full Docker image rebuild before running tests.
#   --test-filter    Pass a CTest regex filter to run only matching tests.
#   --unit-only      Run only unit tests (unit_tests/ targets).
#   --api-only       Run only integration tests (api_tests/ targets).
#   --coverage       Run tests with gcov instrumentation and print coverage summary.
#
# Requirements:
#   - Docker Engine 24+ with compose v2 plugin
#   - Run from the TASK-127 root directory (i.e., bash repo/run_tests.sh)

set -euo pipefail

# Reduce noisy orphan warnings in scripted compose runs.
export COMPOSE_IGNORE_ORPHANS=True

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPOSE_FILE="${SCRIPT_DIR}/docker-compose.yml"

REBUILD=false
TEST_FILTER=""
UNIT_ONLY=false
API_ONLY=false
COVERAGE=false

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
        --coverage)
            COVERAGE=true
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            echo "Usage: $0 [--rebuild] [--test-filter <pattern>] [--unit-only] [--api-only] [--coverage]" >&2
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
    tst_error_formatter tst_key_store tst_logger tst_captcha_generator tst_performance_observer
    tst_question_service tst_checkin_service tst_job_scheduler tst_job_checkpoint
    tst_ingestion_service tst_action_routing tst_command_palette tst_workspace_state
    tst_crash_recovery tst_tray_mode tst_checkin_window tst_question_editor
    tst_audit_viewer tst_masked_field_widget tst_barcode_input tst_login_window tst_step_up_dialog
    tst_sync_window tst_update_window
    tst_data_subject_window tst_ingestion_monitor_window
    tst_sync_service tst_update_service tst_data_subject_service
    tst_security_admin_window tst_privileged_scope
    tst_main_shell tst_question_bank_window tst_application tst_app_bootstrap tst_main_entrypoint
    tst_app_context tst_repository_contracts tst_package_verifier tst_audit_service
    tst_ed25519_signer
)

API_TARGETS=(
    tst_api_bootstrap tst_schema_constraints tst_auth_integration tst_audit_integration
    tst_package_verification tst_checkin_flow tst_correction_flow tst_shell_recovery
    tst_operator_workflows tst_export_flow tst_sync_import_flow tst_update_flow
    tst_privileged_scope_api
)

if [[ "${UNIT_ONLY}" == "true" ]]; then
    SUITE_FILTER="tst_(bootstrap|app_settings|domain_validation|migration|crypto|auth_service|audit_chain|clipboard_guard|masked_field|error_formatter|key_store|logger|captcha_generator|performance_observer|question_service|checkin_service|job_scheduler|job_checkpoint|ingestion_service|action_routing|command_palette|workspace_state|crash_recovery|tray_mode|checkin_window|question_editor|audit_viewer|masked_field_widget|barcode_input|login_window|step_up_dialog|sync_window|update_window|data_subject_window|ingestion_monitor_window|sync_service|update_service|data_subject_service|security_admin_window|privileged_scope|main_shell|question_bank_window|application|app_bootstrap|main_entrypoint|app_context|repository_contracts|package_verifier|audit_service|ed25519_signer)"
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
    echo "  Suite:        unit tests only (50 targets)"
elif [[ "${API_ONLY}" == "true" ]]; then
    echo "  Suite:        integration tests only (13 targets)"
else
    echo "  Suite:        all tests (50 unit + 13 integration)"
fi
echo "  Coverage:     ${COVERAGE}"
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

CONTAINER_SCRIPT="$(cat <<'EOF'
set -euo pipefail

BUILD_DIR="build"
CMAKE_EXTRA_FLAGS=()
if [[ "${RUN_COVERAGE}" == "true" ]]; then
    BUILD_DIR="/tmp/build-cov"
    CMAKE_EXTRA_FLAGS=(
        -DCMAKE_CXX_FLAGS=--coverage\ -O0\ -g
        -DCMAKE_C_FLAGS=--coverage\ -O0\ -g
        -DCMAKE_EXE_LINKER_FLAGS=--coverage
        -DCMAKE_SHARED_LINKER_FLAGS=--coverage
    )
fi

echo "Configuring CMake..."
cmake -B "${BUILD_DIR}" -S . -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_C_COMPILER=gcc \
    -DQt6_DIR="${Qt6_DIR}" \
    "${CMAKE_EXTRA_FLAGS[@]}"

echo "Building..."
BUILD_TARGET_ARGS=()
if [[ "${RUN_UNIT_ONLY}" == "true" ]]; then
    read -r -a TARGET_LIST <<< "${RUN_UNIT_TARGETS}"
    BUILD_TARGET_ARGS=(--target "${TARGET_LIST[@]}")
elif [[ "${RUN_API_ONLY}" == "true" ]]; then
    read -r -a TARGET_LIST <<< "${RUN_API_TARGETS}"
    BUILD_TARGET_ARGS=(--target "${TARGET_LIST[@]}")
fi
cmake --build "${BUILD_DIR}" --parallel "$(nproc)" "${BUILD_TARGET_ARGS[@]}"

echo "Running tests..."
cd "${BUILD_DIR}"
CTEST_ARGS=(--output-on-failure)
if [[ "${RUN_COVERAGE}" == "true" ]]; then
    # Serial execution avoids .gcda write races for shared object files.
    CTEST_ARGS+=(--parallel 1)
else
    CTEST_ARGS+=(--parallel 2)
fi
if [[ -n "${RUN_EFFECTIVE_FILTER}" ]]; then
    CTEST_ARGS+=(-R "${RUN_EFFECTIVE_FILTER}")
fi
ctest "${CTEST_ARGS[@]}"

if [[ "${RUN_COVERAGE}" == "true" ]]; then
    echo ""
    echo "-- Coverage Summary --"

    gcovr \
        --root /workspace \
        --object-directory "${BUILD_DIR}" \
        --gcov-executable gcov-12 \
        --filter '/workspace/src/' \
        --exclude '/workspace/src/main\.cpp' \
        --exclude '.*\.moc$' \
        --exclude '.*moc_.*\.cpp$' \
        --exclude '.*qrc_.*\.cpp$' \
        --exclude '.*ui_.*\.h$' \
        --exclude-unreachable-branches \
        --exclude-throw-branches \
        --json-summary /tmp/summary.json \
        --csv /tmp/per-file.csv \
        >/tmp/gcovr.log

    python3 - <<'PY'
import csv
import json

s = json.load(open('/tmp/summary.json'))
print(f"All files - Lines: {s['line_percent']}% ({s['line_covered']}/{s['line_total']})")
print(f"All files - Functions: {s['function_percent']}% ({s['function_covered']}/{s['function_total']})")
print(f"All files - Branches: {s['branch_percent']}% ({s['branch_covered']}/{s['branch_total']})")

rows = []
with open('/tmp/per-file.csv', newline='') as f:
    r = csv.DictReader(f)
    for row in r:
        lt = int(row.get('line_total') or 0)
        lc = int(row.get('line_covered') or 0)
        unc = lt - lc
        lp = float(row.get('line_percent') or 0) * 100
        score = unc * lt
        rows.append((score, unc, lt, lp, row.get('filename', '')))

rows.sort(reverse=True)
print('Top ROI gaps (score|uncovered|loc|line%|file):')
for score, unc, lt, lp, fn in rows[:12]:
    print(f"{score}|{unc}|{lt}|{lp:.1f}|{fn}")
PY
fi

echo ""
echo "-- Test Summary --"

COUNT_ARGS=(-N)
if [[ -n "${RUN_EFFECTIVE_FILTER}" ]]; then
    COUNT_ARGS+=(-R "${RUN_EFFECTIVE_FILTER}")
fi

TOTAL="$(ctest "${COUNT_ARGS[@]}" 2>/dev/null | tail -1 | grep -oE '[0-9]+' || true)"
TOTAL="${TOTAL:-?}"

CASE_TOTAL=0
shopt -s nullglob
for exe in unit_tests/tst_* api_tests/tst_*; do
    if [[ -x "${exe}" ]]; then
        fn_output="$("./${exe}" -functions 2>/dev/null || true)"
        fn_count="$(printf '%s\n' "${fn_output}" | sed '/^[[:space:]]*$/d' | wc -l | tr -d '[:space:]')"
        fn_count="${fn_count:-0}"
        CASE_TOTAL=$((CASE_TOTAL + fn_count))
    fi
done
shopt -u nullglob

echo "Total test targets: ${TOTAL}"
echo "Total QTest functions: ${CASE_TOTAL}"
EOF
)"

set +e
docker compose -f "${COMPOSE_FILE}" run --rm \
    -e CTEST_OUTPUT_ON_FAILURE=1 \
    -e QT_QPA_PLATFORM=offscreen \
    -e RUN_UNIT_ONLY="${UNIT_ONLY}" \
    -e RUN_API_ONLY="${API_ONLY}" \
    -e RUN_COVERAGE="${COVERAGE}" \
    -e RUN_EFFECTIVE_FILTER="${EFFECTIVE_FILTER}" \
    -e RUN_UNIT_TARGETS="${UNIT_TARGETS_CMAKE}" \
    -e RUN_API_TARGETS="${API_TARGETS_CMAKE}" \
    build-test bash -lc "${CONTAINER_SCRIPT}"
EXIT_CODE=$?
set -e

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

exit "${EXIT_CODE}"
