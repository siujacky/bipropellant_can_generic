#!/usr/bin/env bash
# run_host_tests.sh — compile and run both board_override and board_select
# unit tests using the host-native shim and fabricated fixtures.
#
# Run from the bcg/ directory root:
#   bash test/host/run_host_tests.sh
#
# Requires: native gcc (not arm-none-eabi).

set -euo pipefail

BCG_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
INC="${BCG_DIR}/inc"
GEN="${BCG_DIR}/generated"
SHIM="${BCG_DIR}/test/host/shim"
SRC="${BCG_DIR}/src"
TEST="${BCG_DIR}/test/host"

PASS=0
FAIL=0

run_test() {
    local label="$1"
    local out="$2"
    shift 2   # remaining args are source files

    echo ""
    echo "--- Compiling ${label} ---"
    if gcc -std=c11 -Wall -Wno-unused-function \
           -I"${INC}" -I"${GEN}" -I"${SHIM}" \
           "$@" \
           -o "${out}" -lm 2>&1; then
        echo "--- Running ${label} ---"
        if "${out}"; then
            echo "--- ${label}: PASSED ---"
            PASS=$((PASS + 1))
        else
            echo "--- ${label}: FAILED (runtime) ---"
            FAIL=$((FAIL + 1))
        fi
    else
        echo "--- ${label}: FAILED (compile error) ---"
        FAIL=$((FAIL + 1))
    fi
}

# hal_mock.c provides definitions for shared extern GPIO mock state and
# hal_gpio_init_count so all TUs see the same counter.
MOCK_C="${SHIM}/hal_mock.c"

# board_override tests: also link board_select.c because ACTIVE is defined there.
run_test "board_override (T1-T13)" \
    /tmp/bcg_test_override \
    "${TEST}/test_board_override.c" \
    "${SRC}/board_override.c" \
    "${SRC}/board_select.c" \
    "${MOCK_C}"

run_test "board_select (S1-S6)" \
    /tmp/bcg_test_select \
    "${TEST}/test_board_select.c" \
    "${SRC}/board_select.c" \
    "${MOCK_C}"

run_test "phasemap wizard (T1-T6)" \
    /tmp/bcg_test_phasemap \
    "${TEST}/test_phasemap.c" \
    "${SRC}/phasemap.c" \
    "${SRC}/board_select.c" \
    "${GEN}/board_af_validity_stm32f1.c" \
    "${MOCK_C}" \
    "-DPHASEMAP_HOST_TEST"

echo ""
echo "==================================="
echo "Results: ${PASS} suite(s) PASSED, ${FAIL} suite(s) FAILED"
echo "==================================="

if [ "${FAIL}" -gt 0 ]; then
    exit 1
fi
exit 0
