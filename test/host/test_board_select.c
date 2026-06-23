/*
 * test_board_select.c — 6 host-native unit tests for board_select precedence.
 *
 * Tests S1-S6.  Uses assert() + printf; main() returns 0 only if all pass.
 *
 * Compile (from the bcg/ directory root):
 *   gcc -std=c11 -Wall \
 *       -I/tmp/bcg/inc -I/tmp/bcg/generated -Itest/host/shim \
 *       test/host/test_board_select.c src/board_select.c \
 *       -o /tmp/bcg_test_select -lm
 */

#include "stm32f1xx_hal.h"  /* shim must come first */
#include "fixtures.h"        /* g_board_table, FlashContent, etc. */

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "board_active.h"   /* ACTIVE extern, board_select_init() */
#include "board_table.h"    /* board_select_mode_t */

/* board_select_last_mode, board_select_force, board_select_hint are
 * declared in board_override.h (they are implemented in board_select.c) */
#include "board_override.h"

/* =========================================================================
 * Helpers
 * ====================================================================== */
static const char *mode_name(board_select_mode_t m) {
    switch (m) {
        case BOARD_SELECT_DEFAULT_SAFE:   return "DEFAULT_SAFE";
        case BOARD_SELECT_STORED_INDEX:   return "STORED_INDEX";
        case BOARD_SELECT_SERIAL_FORCED:  return "SERIAL_FORCED";
        default:                          return "UNKNOWN";
    }
}

#define ASSERT_MODE(got, expected, label) do {                           \
    if ((got) != (expected)) {                                           \
        printf("FAIL %s: expected mode %s got %s\n",                    \
               (label), mode_name(expected), mode_name(got));           \
        failures++;                                                      \
    } else {                                                             \
        printf("PASS %s: mode=%s\n", (label), mode_name(got));          \
    }                                                                    \
} while (0)

/* Reset board_select internal state and FlashContent before each test.
 * We clear the forced index and hints by calling the public API, and
 * zero FlashContent (board_override_valid=0 → no stored index). */
static void select_reset(void) {
    memset(&FlashContent, 0, sizeof(FlashContent));
    /* force=0xFFFF = "not forced" */
    board_select_force(0xFFFFu);
    /* clear hints: family 0xFF = "no family hint"; empty part_hint */
    board_select_hint(0xFFu, (const char *)0);
}

/* =========================================================================
 * S1: default loads motor-disabled safe row.
 *     No forced index, no stored index, no hint → falls through to
 *     g_default_board_index (0) → ACTIVE = safe default.
 *     Mode must be BOARD_SELECT_DEFAULT_SAFE.
 * ====================================================================== */
static void s1_default_safe(int *failures) {
    select_reset();
    board_select_init();
    board_select_mode_t mode = board_select_last_mode();
    ASSERT_MODE(mode, BOARD_SELECT_DEFAULT_SAFE, "S1:default_loads_safe");
    /* Verify ACTIVE is the safe default: motor_count == 0 */
    if (ACTIVE.motor_count != 0) {
        printf("FAIL S1: expected motor_count=0 got %u\n", ACTIVE.motor_count);
        (*failures)++;
    } else {
        printf("PASS S1: ACTIVE.motor_count=0 (safe default)\n");
    }
}

/* =========================================================================
 * S2: force overrides default.
 *     Set s_forced_index to 1 (TEST_BOARD_A, motor_count=2).
 *     Mode must be BOARD_SELECT_SERIAL_FORCED and ACTIVE.motor_count==2.
 * ====================================================================== */
static void s2_force_overrides_default(int *failures) {
    select_reset();
    board_select_force(1u);  /* index 1 = TEST_BOARD_A */
    board_select_init();
    board_select_mode_t mode = board_select_last_mode();
    ASSERT_MODE(mode, BOARD_SELECT_SERIAL_FORCED, "S2:force_overrides_default");
    if (ACTIVE.motor_count != 2) {
        printf("FAIL S2: expected motor_count=2 got %u\n", ACTIVE.motor_count);
        (*failures)++;
    } else {
        printf("PASS S2: ACTIVE.motor_count=2 (TEST_BOARD_A forced)\n");
    }
}

/* =========================================================================
 * S3: force then clear reverts to default.
 *     After forcing index 1, clear the force (0xFFFF = not forced).
 *     Call board_select_init() again → should revert to safe default.
 * ====================================================================== */
static void s3_force_clear_reverts(int *failures) {
    select_reset();
    board_select_force(1u);   /* arm force */
    board_select_init();      /* now forced to index 1 */
    board_select_force(0xFFFFu); /* clear force */
    board_select_init();         /* re-run: no forced, no stored, no hint → default */
    board_select_mode_t mode = board_select_last_mode();
    ASSERT_MODE(mode, BOARD_SELECT_DEFAULT_SAFE, "S3:force_clear_reverts_to_default");
    if (ACTIVE.motor_count != 0) {
        printf("FAIL S3: expected motor_count=0 got %u\n", ACTIVE.motor_count);
        (*failures)++;
    } else {
        printf("PASS S3: ACTIVE.motor_count=0 after force cleared\n");
    }
}

/* =========================================================================
 * S4: stored-index loaded.
 *     Populate FlashContent.board_override_valid=1 and
 *     board_selected_index=2 (TEST_BOARD_B, motor_count=1).
 *     No force, no hint → Tier 1b (stored) → BOARD_SELECT_STORED_INDEX.
 * ====================================================================== */
static void s4_stored_index_loaded(int *failures) {
    select_reset();
    FlashContent.board_override_valid  = 1;
    FlashContent.board_selected_index  = 2u;  /* TEST_BOARD_B */
    board_select_init();
    board_select_mode_t mode = board_select_last_mode();
    ASSERT_MODE(mode, BOARD_SELECT_STORED_INDEX, "S4:stored_index_loaded");
    if (ACTIVE.motor_count != 1) {
        printf("FAIL S4: expected motor_count=1 got %u\n", ACTIVE.motor_count);
        (*failures)++;
    } else {
        printf("PASS S4: ACTIVE.motor_count=1 (TEST_BOARD_B via stored index)\n");
    }
}

/* =========================================================================
 * S5: hint alone with >1 match → falls back to default.
 *     Both index 1 (TEST_BOARD_A) and index 2 (TEST_BOARD_B) are family
 *     BOARD_FAMILY_STM32F1, so narrowing with family=STM32F1 and no part_hint
 *     finds 2 candidates → refuses to guess → fallback to default.
 * ====================================================================== */
static void s5_hint_ambiguous_reverts(int *failures) {
    select_reset();
    /* Hint: family STM32F1, no part_hint */
    board_select_hint(BOARD_FAMILY_STM32F1, (const char *)0);
    board_select_init();
    board_select_mode_t mode = board_select_last_mode();
    ASSERT_MODE(mode, BOARD_SELECT_DEFAULT_SAFE, "S5:hint_ambiguous_falls_back");
    if (ACTIVE.motor_count != 0) {
        printf("FAIL S5: expected motor_count=0 (safe) got %u\n", ACTIVE.motor_count);
        (*failures)++;
    } else {
        printf("PASS S5: ACTIVE.motor_count=0 (ambiguous hint → safe default)\n");
    }
}

/* =========================================================================
 * S6: hint with 1 match → that profile is selected.
 *     Hint: family STM32F1, part_hint="TEST_BOARD_A".
 *     Only index 1 matches → unique → load it → BOARD_SELECT_STORED_INDEX.
 *     (board_select.c uses BOARD_SELECT_STORED_INDEX for the narrowing path.)
 * ====================================================================== */
static void s6_hint_unique_match(int *failures) {
    select_reset();
    board_select_hint(BOARD_FAMILY_STM32F1, "TEST_BOARD_A");
    board_select_init();
    board_select_mode_t mode = board_select_last_mode();
    /* narrow_by_family returns a unique match → load_index with STORED_INDEX */
    ASSERT_MODE(mode, BOARD_SELECT_STORED_INDEX, "S6:hint_unique_match");
    if (ACTIVE.motor_count != 2) {
        printf("FAIL S6: expected motor_count=2 (TEST_BOARD_A) got %u\n", ACTIVE.motor_count);
        (*failures)++;
    } else {
        printf("PASS S6: ACTIVE.motor_count=2 (TEST_BOARD_A via unique hint)\n");
    }
}

/* =========================================================================
 * main
 * ====================================================================== */
int main(void) {
    int failures = 0;

    printf("=== board_select unit tests ===\n");

    s1_default_safe(&failures);
    s2_force_overrides_default(&failures);
    s3_force_clear_reverts(&failures);
    s4_stored_index_loaded(&failures);
    s5_hint_ambiguous_reverts(&failures);
    s6_hint_unique_match(&failures);

    printf("===============================\n");
    if (failures == 0) {
        printf("ALL 6 TESTS PASSED\n");
        return 0;
    } else {
        printf("%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
