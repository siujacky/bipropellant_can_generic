#!/usr/bin/env bash
# scripts/ci_check.sh — CI guard for the bipropellant_can_generic firmware stack.
#
# Steps (all run with set -e; any non-zero exit aborts):
#   1. Codegen regenerate-and-diff: re-run emit_board_table --all-families and
#      assert generated/ is unchanged (no untracked code drift).
#   2. pytest codegen/tests/ -q (Python unit tests for the board-table codegen).
#   3. bash test/host/run_host_tests.sh (host-native validator smoke test).
#   4. If arm-none-eabi-gcc is available, gcc -fsyntax-only smoke on
#      src/board_override.c + src/board_select.c.  If not available, echo-skip.
#
# The arm-none-eabi cross-compile step is aspirational — GitHub Actions free
# runners may not have gcc-arm-none-eabi without an apt-get install; tolerate
# its absence gracefully.

set -e

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "================================================================"
echo " BCG CI guard — $(date)"
echo " repo: ${REPO}"
echo "================================================================"

# ---------------------------------------------------------------------------
# Step 1: codegen regenerate-and-diff
# ---------------------------------------------------------------------------
echo ""
echo "--- Step 1: codegen regenerate-and-diff ---"

cd "${REPO}"
python3 -m codegen.emit_board_table \
    --all-families \
    --profiles ./profiles \
    --out ./generated

# git diff --exit-code fails (rc=1) if any tracked file in generated/ has changed.
# Untracked new files are NOT caught by --exit-code alone, so also check with
# git status --porcelain (untracked files in generated/ indicate drift).
git diff --exit-code -- generated/ || {
    echo "ERROR: generated/ has drifted from the committed state."
    echo "       Run: python3 -m codegen.emit_board_table --all-families --profiles ./profiles --out ./generated"
    echo "       Then commit the updated generated/ files."
    exit 1
}

UNTRACKED=$(git status --porcelain generated/ | grep "^??" || true)
if [ -n "${UNTRACKED}" ]; then
    echo "ERROR: untracked files in generated/ — new generated files not committed:"
    echo "${UNTRACKED}"
    exit 1
fi

echo "OK: generated/ is up-to-date and matches the committed state."

# ---------------------------------------------------------------------------
# Step 2: pytest codegen/tests/
# ---------------------------------------------------------------------------
echo ""
echo "--- Step 2: pytest codegen/tests/ ---"

cd "${REPO}"
python3 -m pytest codegen/tests/ -q

echo "OK: all codegen Python tests passed."

# ---------------------------------------------------------------------------
# Step 3: host validator tests
# ---------------------------------------------------------------------------
echo ""
echo "--- Step 3: host validator tests ---"

bash "${REPO}/test/host/run_host_tests.sh"

echo "OK: host validator tests passed."

# ---------------------------------------------------------------------------
# Step 4: arm-none-eabi-gcc -fsyntax-only smoke (aspirational; skip if absent)
# ---------------------------------------------------------------------------
echo ""
echo "--- Step 4: arm-none-eabi-gcc -fsyntax-only smoke ---"

CROSS_GCC="arm-none-eabi-gcc"
INC="${REPO}/inc"
GEN="${REPO}/generated"

if command -v "${CROSS_GCC}" >/dev/null 2>&1; then
    echo "Found ${CROSS_GCC}: $(${CROSS_GCC} --version | head -1)"
    echo "Running syntax-only check on src/board_override.c + src/board_select.c …"

    # Use the STM32F1 family headers for the syntax check (the primary supported
    # target; other families use the same generated/board_table.h structure).
    CROSS_INCLUDES=(
        "-I${INC}"
        "-I${GEN}"
        "-IDrivers/STM32F1xx_HAL_Driver/Inc"
        "-IDrivers/STM32F1xx_HAL_Driver/Inc/Legacy"
        "-IDrivers/CMSIS/Device/ST/STM32F1xx/Include"
        "-IDrivers/CMSIS/Include"
        "-Isrc/hbprotocol"
    )
    CROSS_DEFINES=(
        "-DUSE_HAL_DRIVER"
        "-DSTM32F103xE"
    )
    CROSS_FLAGS=(
        "-mcpu=cortex-m3"
        "-mthumb"
        "-std=gnu11"
        "-Wall"
        "-fsyntax-only"
    )

    cd "${REPO}"
    "${CROSS_GCC}" "${CROSS_FLAGS[@]}" "${CROSS_INCLUDES[@]}" "${CROSS_DEFINES[@]}" \
        src/board_override.c
    echo "  OK: src/board_override.c"

    "${CROSS_GCC}" "${CROSS_FLAGS[@]}" "${CROSS_INCLUDES[@]}" "${CROSS_DEFINES[@]}" \
        src/board_select.c
    echo "  OK: src/board_select.c"

    # Also syntax-check the generated data files (family: stm32f1 only;
    # other families are checked in M5 env builds).
    "${CROSS_GCC}" "${CROSS_FLAGS[@]}" "${CROSS_INCLUDES[@]}" "${CROSS_DEFINES[@]}" \
        generated/board_table_stm32f1.c
    echo "  OK: generated/board_table_stm32f1.c"

    "${CROSS_GCC}" "${CROSS_FLAGS[@]}" "${CROSS_INCLUDES[@]}" "${CROSS_DEFINES[@]}" \
        generated/board_af_validity_stm32f1.c
    echo "  OK: generated/board_af_validity_stm32f1.c"

    echo "OK: arm-none-eabi-gcc syntax-only checks passed."
else
    echo "SKIP: ${CROSS_GCC} not found."
    echo "      Install gcc-arm-none-eabi to enable cross-compile syntax checks."
    echo "      (On GitHub Actions: sudo apt-get install gcc-arm-none-eabi)"
    echo "      This step is informational — CI does not fail without it."
fi

echo ""
echo "================================================================"
echo " ALL CI STEPS PASSED"
echo "================================================================"
