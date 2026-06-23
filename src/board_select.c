/*
 * board_select.c — boot resolver (M3, real implementation).
 *
 * Populates the mutable RAM copy ACTIVE with the selected board profile.
 * Runs from main() AFTER the board-independent self-hold latch and BEFORE
 * peripheral init, so ACTIVE is authoritative by the time setup.c sources its
 * pins through the board_active.h accessors.
 *
 * Selection tiers, in priority order:
 *   1. PRIMARY selector — an explicit index:
 *        a. a serial/CAN-forced index set at runtime via board_select_force()
 *           (highest priority; lets a host pin the board over the wire), OR
 *        b. the NVRAM stored_index persisted in FlashContent
 *           (board_override_valid + board_selected_index).
 *      An explicit, in-range index is taken verbatim — no guessing.
 *   2. BEST-EFFORT narrowing by family (+ part_hint) when no explicit index is
 *      given. If the narrowing leaves MORE THAN ONE non-default candidate we
 *      REFUSE to guess and fall through to the safe default — selection must be
 *      explicit. A unique non-default match is accepted.
 *   3. UID tier — NO-OP. No current row carries a uid (all empty), so there is
 *      nothing to match; retained only as a documented placeholder.
 *   4. FALLBACK — g_default_board_index (the motor-disabled safe row).
 *
 * The persisted pin overrides (FlashContent.board_override) are layered on top
 * of the resolved row by board_override.c's boot replay; this file only chooses
 * the base row.
 */
#include <string.h>
#include "board_active.h"
#include "board_table.h"
#include "flashcontent.h"

/* The single mutable RAM copy of the selected board profile. */
board_profile_t ACTIVE;

/* Runtime serial/CAN-forced index. 0xFFFF = not forced. Set before
 * board_select_init() runs (or persisted and re-evaluated next boot). */
static uint16_t s_forced_index = 0xFFFFu;

/* Best-effort narrowing hints (optional). Empty/0xFF = "don't narrow on this". */
static uint8_t s_hint_family    = 0xFFu;
static char    s_hint_part[24]  = {0};

/* The mode actually used by the last resolve, for diagnostics / persistence. */
static board_select_mode_t s_last_mode = BOARD_SELECT_DEFAULT_SAFE;

board_select_mode_t board_select_last_mode(void) { return s_last_mode; }

/* Host/serial/CAN entry points (declared in board_active.h's companion or used
 * directly by can_bus.c via extern). */
void board_select_force(uint16_t index) { s_forced_index = index; }

void board_select_hint(uint8_t family, const char *part_hint) {
    s_hint_family = family;
    if (part_hint) {
        strncpy(s_hint_part, part_hint, sizeof(s_hint_part) - 1);
        s_hint_part[sizeof(s_hint_part) - 1] = '\0';
    } else {
        s_hint_part[0] = '\0';
    }
}

static void load_index(uint16_t index, board_select_mode_t mode) {
    memcpy(&ACTIVE, &g_board_table[index], sizeof(ACTIVE));
    s_last_mode = mode;
}

/* Best-effort family (+part_hint) narrowing.
 * Returns a unique non-default row index, or 0xFFFF if 0 or >1 candidates. */
static uint16_t narrow_by_family(void) {
    if (s_hint_family == 0xFFu) {
        return 0xFFFFu;  /* no family hint -> cannot narrow */
    }

    uint16_t found = 0xFFFFu;
    uint16_t matches = 0;
    int have_part = (s_hint_part[0] != '\0');

    for (uint16_t i = 0; i < g_board_table_count; i++) {
        if (i == g_default_board_index) {
            continue;  /* never auto-narrow to the safe default */
        }
        if (g_board_table[i].family != s_hint_family) {
            continue;
        }
        /* If a part_hint is supplied, require a prefix match to count. */
        if (have_part &&
            strncmp(g_board_table[i].part_hint, s_hint_part,
                    strlen(s_hint_part)) != 0) {
            continue;
        }
        matches++;
        found = i;
    }

    /* Require explicit selection when ambiguous — NEVER guess. */
    if (matches == 1) {
        return found;
    }
    return 0xFFFFu;
}

void board_select_init(void) {
    /* Tier 1a: serial/CAN-forced index (runtime, highest priority). */
    if (s_forced_index != 0xFFFFu && s_forced_index < g_board_table_count) {
        load_index(s_forced_index, BOARD_SELECT_SERIAL_FORCED);
        return;
    }

    /* Tier 1b: NVRAM stored index. */
    if (FlashContent.board_override_valid &&
        FlashContent.board_selected_index != 0xFFFFu &&
        FlashContent.board_selected_index < g_board_table_count) {
        load_index(FlashContent.board_selected_index, BOARD_SELECT_STORED_INDEX);
        return;
    }

    /* Tier 2: best-effort family+part_hint narrowing (unique match only). */
    {
        uint16_t narrowed = narrow_by_family();
        if (narrowed != 0xFFFFu) {
            load_index(narrowed, BOARD_SELECT_STORED_INDEX);
            return;
        }
    }

    /* Tier 3: UID — NO-OP (no row carries a uid; nothing to match). */

    /* Tier 4: fallback to the motor-disabled safe default row. */
    load_index(g_default_board_index, BOARD_SELECT_DEFAULT_SAFE);
}
