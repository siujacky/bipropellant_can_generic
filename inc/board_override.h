/*
 * board_override.h — runtime board pin override API (M4).
 *
 * board_apply_override() validates a proposed pin remap against three gates
 * (silicon legality, profile-wide pin uniqueness, EXTI-line allocation) and
 * either applies it LIVE (GPIO-class) or stages it PENDING_REBOOT
 * (phase-PWM/ADC/hall/dead-time). board_override_commit() persists the staged
 * + live overrides to flash via the existing FlashContent commit path.
 */
#pragma once

#include "board_table.h"   /* gpio_pin_t, override result enum */
#include "board_active.h"  /* override_result_t */

/* Canonical field identifiers addressable by an override. The numeric values
 * are part of the host/CAN wire contract — APPEND only, never renumber. */
typedef enum {
    /* --- GPIO-class (LIVE-capable): LED / buzzer / button / charger ------- */
    BOARD_FIELD_LED_RED = 0,
    BOARD_FIELD_LED_GREEN,
    BOARD_FIELD_LED_ORANGE,
    BOARD_FIELD_LED_BLUE,
    BOARD_FIELD_BUZZER,
    BOARD_FIELD_BUTTON,
    BOARD_FIELD_CHARGER,
    BOARD_FIELD_SELF_HOLD,

    /* --- HALL / EXTI-class (PENDING_REBOOT; also EXTI-line checked) ------- */
    BOARD_FIELD_HALL_L_A,
    BOARD_FIELD_HALL_L_B,
    BOARD_FIELD_HALL_L_C,
    BOARD_FIELD_HALL_R_A,
    BOARD_FIELD_HALL_R_B,
    BOARD_FIELD_HALL_R_C,

    /* --- PHASE-PWM-class (PENDING_REBOOT; illegal moves -> REJECTED_SILICON) */
    BOARD_FIELD_PHASE_L_UH,
    BOARD_FIELD_PHASE_L_UL,
    BOARD_FIELD_PHASE_L_VH,
    BOARD_FIELD_PHASE_L_VL,
    BOARD_FIELD_PHASE_L_WH,
    BOARD_FIELD_PHASE_L_WL,
    BOARD_FIELD_PHASE_R_UH,
    BOARD_FIELD_PHASE_R_UL,
    BOARD_FIELD_PHASE_R_VH,
    BOARD_FIELD_PHASE_R_VL,
    BOARD_FIELD_PHASE_R_WH,
    BOARD_FIELD_PHASE_R_WL,

    /* --- ADC-class (PENDING_REBOOT; illegal moves -> REJECTED_SILICON) ---- */
    BOARD_FIELD_ADC_BATTERY,
    BOARD_FIELD_ADC_DCLINK,
    BOARD_FIELD_ADC_LEFT_CUR,
    BOARD_FIELD_ADC_RIGHT_CUR,

    BOARD_FIELD_COUNT
} board_field_id_t;

/* Validate + route a single pin override. See board_override.c. */
override_result_t board_apply_override(uint8_t field_id, gpio_pin_t newpin);

/* Arm (on=1) a single subsequent BOARD_FIELD_SELF_HOLD override. The power
 * latch is never changed live and a self-hold override is REJECTED unless armed
 * immediately beforehand; the arming is one-shot (consumed by the next
 * self-hold apply, success or fail). */
void board_override_confirm_self_hold(uint8_t on);

/* Persist the current override set (live + pending) to flash. Returns 1 on a
 * successful commit, 0 if nothing to persist / write failed. */
int board_override_commit(void);

/* Re-apply persisted overrides into ACTIVE at boot (after board_select_init).
 * GPIO-class entries are NOT re-driven here (setup.c does the live HW init);
 * this only restores the ACTIVE pin slots so every consumer sees them. */
void board_override_boot_replay(void);

/* Resolver hooks implemented in board_select.c (serial/CAN-forced selection). */
void                board_select_force(uint16_t index);
void                board_select_hint(uint8_t family, const char *part_hint);
board_select_mode_t board_select_last_mode(void);
