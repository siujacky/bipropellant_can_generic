/*
 * board_override.c — runtime board pin override engine (M4).
 *
 * board_apply_override(field_id, newpin) runs THREE validators IN ORDER and
 * returns one of the five override_result_t values:
 *
 *   (1) SILICON legality  (g_af_validity): is this pin physically capable of
 *       the role the field needs on this family? phase pins must expose the
 *       active timer's channel; ADC pins must expose an ADC channel.
 *       Illegal phase/ADC moves -> REJECTED_SILICON. GPIO/hall roles are
 *       always silicon-legal (any GPIO is a digital IO / EXTI source).
 *   (2) PROFILE-WIDE PIN-UNIQUENESS: the new pin must not already be claimed by
 *       any OTHER active function in ACTIVE -> REJECTED_CONFLICT.
 *   (3) EXTI-LINE allocation (hall / EXTI-class only): EXTI line == pin number;
 *       reject if another EXTI-active pin already uses that line number
 *       -> REJECTED_EXTI.
 *
 * Routing after validation:
 *   - GPIO-class (LED/buzzer/button/charger): APPLIED_LIVE — the ACTIVE slot is
 *     updated and HAL_GPIO re-init runs NOW (sourced from ACTIVE via the
 *     board_active.h contract, never from defines.h).
 *   - self-hold (power latch): PENDING_REBOOT ONLY and only when explicitly
 *     armed via board_override_confirm_self_hold(1) — never live (re-initing
 *     the latch GPIO mid-run drops power; a wrong persisted pin soft-bricks the
 *     next boot). Applied by board_override_boot_replay() before the latch.
 *   - phase-PWM / ADC / hall / dead-time: PENDING_REBOOT — validate + stage in
 *     ACTIVE and in the persisted set, applied next boot. We NEVER call
 *     HAL_GPIO_Init on a phase/ADC/hall pin live.
 *
 * board_override_commit() persists the staged set into FlashContent and writes
 * it through the existing FlashContent flash-commit path.
 */
#include <string.h>
#include <stddef.h>
#include "board_override.h"
#include "board_active.h"
#include "board_table.h"
#include "flashcontent.h"
#include "defines.h"
#include "flashaccess.h"
#include "config.h"

extern FLASH_CONTENT FlashContent;

/* --------------------------------------------------------------------------
 * Field metadata: maps a field_id to its ACTIVE slot, class, and (for live
 * GPIO re-init) the historical defines.h default port/mask.
 * ------------------------------------------------------------------------ */
typedef enum {
    CLS_GPIO  = 0,   /* LIVE-capable digital IO */
    CLS_HALL,        /* PENDING_REBOOT + EXTI-line checked */
    CLS_PHASE,       /* PENDING_REBOOT; silicon = timer-channel legal */
    CLS_ADC,         /* PENDING_REBOOT; silicon = ADC-channel legal */
    CLS_SELF_HOLD,   /* PENDING_REBOOT ONLY + explicit confirm: re-initing the
                      * power-latch GPIO live would drop power mid-run, and a
                      * wrong persisted self-hold pin soft-bricks the next boot
                      * (latch never asserts) — so this is gated, never live. */
} field_class_t;

typedef struct {
    uint8_t        field_id;
    field_class_t  cls;
    /* offset of the gpio_pin_t slot inside board_profile_t */
    size_t         slot_off;
    /* live-reinit defaults (GPIO-class only; ignored otherwise) */
    GPIO_TypeDef  *def_port;
    uint16_t       def_mask;
    uint32_t       gpio_mode;
    uint32_t       gpio_pull;
    /* the timer this phase pin belongs to (CLS_PHASE only; 0 otherwise).
     * We resolve the *channel* legality against g_af_validity by timer name. */
    const char    *timer_name;
} field_meta_t;

#define SLOT(member)   offsetof(board_profile_t, member)

/* Note: the phase timer_name is informational for the silicon check; the actual
 * active timer id lives in ACTIVE.phases_*.timer_id. We accept a phase move if
 * the pin exposes ANY timer channel legal for that pin (a stricter per-timer
 * match would need the live timer id, resolved below in silicon_ok()). */
static const field_meta_t k_fields[] = {
    /* --- GPIO-class ------------------------------------------------------ */
    { BOARD_FIELD_LED_RED,    CLS_GPIO, SLOT(led_red),    LED_PORT,     LED_PIN,     GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0 },
    { BOARD_FIELD_LED_GREEN,  CLS_GPIO, SLOT(led_green),  LED_PORT,     LED_PIN,     GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0 },
    { BOARD_FIELD_LED_ORANGE, CLS_GPIO, SLOT(led_orange), LED_PORT,     LED_PIN,     GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0 },
    { BOARD_FIELD_LED_BLUE,   CLS_GPIO, SLOT(led_blue),   LED_PORT,     LED_PIN,     GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0 },
    { BOARD_FIELD_BUZZER,     CLS_GPIO, SLOT(buzzer),     BUZZER_PORT,  BUZZER_PIN,  GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0 },
    { BOARD_FIELD_BUTTON,     CLS_GPIO, SLOT(button),     BUTTON_PORT,  BUTTON_PIN,  GPIO_MODE_INPUT,     GPIO_NOPULL, 0 },
    { BOARD_FIELD_CHARGER,    CLS_GPIO, SLOT(charger),    CHARGER_PORT, CHARGER_PIN, GPIO_MODE_INPUT,     GPIO_PULLUP, 0 },
    { BOARD_FIELD_SELF_HOLD,  CLS_SELF_HOLD, SLOT(self_hold),  OFF_PORT,  OFF_PIN,     GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, 0 },

    /* --- HALL / EXTI-class ----------------------------------------------- */
    { BOARD_FIELD_HALL_L_A, CLS_HALL, SLOT(halls_left.hall_a),  0, 0, 0, 0, 0 },
    { BOARD_FIELD_HALL_L_B, CLS_HALL, SLOT(halls_left.hall_b),  0, 0, 0, 0, 0 },
    { BOARD_FIELD_HALL_L_C, CLS_HALL, SLOT(halls_left.hall_c),  0, 0, 0, 0, 0 },
    { BOARD_FIELD_HALL_R_A, CLS_HALL, SLOT(halls_right.hall_a), 0, 0, 0, 0, 0 },
    { BOARD_FIELD_HALL_R_B, CLS_HALL, SLOT(halls_right.hall_b), 0, 0, 0, 0, 0 },
    { BOARD_FIELD_HALL_R_C, CLS_HALL, SLOT(halls_right.hall_c), 0, 0, 0, 0, 0 },

    /* --- PHASE-PWM-class -------------------------------------------------- */
    { BOARD_FIELD_PHASE_L_UH, CLS_PHASE, SLOT(phases_left.u_high),  0, 0, 0, 0, 0 },
    { BOARD_FIELD_PHASE_L_UL, CLS_PHASE, SLOT(phases_left.u_low),   0, 0, 0, 0, 0 },
    { BOARD_FIELD_PHASE_L_VH, CLS_PHASE, SLOT(phases_left.v_high),  0, 0, 0, 0, 0 },
    { BOARD_FIELD_PHASE_L_VL, CLS_PHASE, SLOT(phases_left.v_low),   0, 0, 0, 0, 0 },
    { BOARD_FIELD_PHASE_L_WH, CLS_PHASE, SLOT(phases_left.w_high),  0, 0, 0, 0, 0 },
    { BOARD_FIELD_PHASE_L_WL, CLS_PHASE, SLOT(phases_left.w_low),   0, 0, 0, 0, 0 },
    { BOARD_FIELD_PHASE_R_UH, CLS_PHASE, SLOT(phases_right.u_high), 0, 0, 0, 0, 0 },
    { BOARD_FIELD_PHASE_R_UL, CLS_PHASE, SLOT(phases_right.u_low),  0, 0, 0, 0, 0 },
    { BOARD_FIELD_PHASE_R_VH, CLS_PHASE, SLOT(phases_right.v_high), 0, 0, 0, 0, 0 },
    { BOARD_FIELD_PHASE_R_VL, CLS_PHASE, SLOT(phases_right.v_low),  0, 0, 0, 0, 0 },
    { BOARD_FIELD_PHASE_R_WH, CLS_PHASE, SLOT(phases_right.w_high), 0, 0, 0, 0, 0 },
    { BOARD_FIELD_PHASE_R_WL, CLS_PHASE, SLOT(phases_right.w_low),  0, 0, 0, 0, 0 },

    /* --- ADC-class (the on_pin slot of each adc_chan_t) ------------------- */
    { BOARD_FIELD_ADC_BATTERY,   CLS_ADC, SLOT(battery_voltage.on_pin),     0, 0, 0, 0, 0 },
    { BOARD_FIELD_ADC_DCLINK,    CLS_ADC, SLOT(dc_link_voltage.on_pin),     0, 0, 0, 0, 0 },
    { BOARD_FIELD_ADC_LEFT_CUR,  CLS_ADC, SLOT(left_phase_current.on_pin),  0, 0, 0, 0, 0 },
    { BOARD_FIELD_ADC_RIGHT_CUR, CLS_ADC, SLOT(right_phase_current.on_pin), 0, 0, 0, 0, 0 },
};
#define K_FIELD_COUNT (sizeof(k_fields) / sizeof(k_fields[0]))

static const field_meta_t *find_meta(uint8_t field_id) {
    for (size_t i = 0; i < K_FIELD_COUNT; i++) {
        if (k_fields[i].field_id == field_id) {
            return &k_fields[i];
        }
    }
    return (const field_meta_t *)0;
}

static gpio_pin_t *slot_ptr(const field_meta_t *m) {
    return (gpio_pin_t *)((uint8_t *)&ACTIVE + m->slot_off);
}

static int pin_eq(gpio_pin_t a, gpio_pin_t b) {
    return a.port == b.port && a.pin == b.pin;
}

static int pin_set(gpio_pin_t p) {
    return !(p.port == 0xFFu);   /* sentinel test (matches BOARD_PIN_IS_UNSET) */
}

/* --------------------------------------------------------------------------
 * Validator 1: SILICON legality via g_af_validity.
 * ------------------------------------------------------------------------ */
static int af_has_peripheral_prefix(gpio_pin_t p, const char *prefix) {
    size_t n = strlen(prefix);
    for (uint16_t i = 0; i < g_af_validity_count; i++) {
        if (g_af_validity[i].pin.port == p.port &&
            g_af_validity[i].pin.pin  == p.pin &&
            strncmp(g_af_validity[i].peripheral, prefix, n) == 0) {
            return 1;
        }
    }
    return 0;
}

static int silicon_ok(const field_meta_t *m, gpio_pin_t newpin) {
    switch (m->cls) {
        case CLS_GPIO:
        case CLS_HALL:
        case CLS_SELF_HOLD:
            /* Any GPIO is a legal digital IO / EXTI source / latch output. */
            return 1;

        case CLS_PHASE: {
            /* Must expose the ACTIVE side's timer channel. Resolve which side
             * (left/right) this field belongs to and require a matching TIMx
             * entry in g_af_validity. */
            uint8_t tid = (m->field_id >= BOARD_FIELD_PHASE_R_UH)
                              ? ACTIVE.phases_right.timer_id
                              : ACTIVE.phases_left.timer_id;
            char tname[8];
            /* timer_id 1->TIM1, 8->TIM8, etc. */
            if (tid == 0) {
                return 0;  /* no active timer -> cannot place a phase pin */
            }
            tname[0] = 'T'; tname[1] = 'I'; tname[2] = 'M';
            tname[3] = (char)('0' + tid);
            tname[4] = '\0';
            return af_has_peripheral_prefix(newpin, tname);
        }

        case CLS_ADC:
            /* STM32F1 ADC channels are not enumerated in g_af_validity (analog,
             * AF = -1). Accept any non-sentinel pin on a port that physically
             * carries an ADC channel (A/B/C). A finer check would need an
             * ADC-channel table; here we reject obviously-illegal ports. */
            return (newpin.port <= 2u);   /* GPIOA/B/C carry ADC123 inputs */
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * Reserved peripheral pins NOT represented in board_profile_t.
 *
 * pin_conflicts() only scans k_fields[] (the ACTIVE profile slots). Pins owned
 * by peripherals that live OUTSIDE board_profile_t are otherwise invisible to
 * the uniqueness check, so a host-controllable override (can_bus.c) could remap
 * a LIVE GPIO field onto an active peripheral line (e.g. the MCP2515/CAN INT on
 * PB12) and be APPLIED_LIVE — contending/reconfiguring the running CAN link.
 *
 * These are the software-SPI / MCP2515 lines held in software_spi.c statics.
 * They default to the SOFT_SPI_* compile-time pins (PA2 MOSI, PA3 MISO,
 * PB10 SCK, PB11 CS, PB12 MCP2515 INT). gpio_pin_t encoding: port 0=A,1=B,2=C;
 * pin = 0..15. Kept here so any override that lands on one of these reserved
 * lines is REJECTED_CONFLICT.
 *
 * NOTE: these mirror the static defaults in software_spi.c. If SoftSPI_Rebind()
 * is ever wired to move the CAN pins live, this table must track the same pins
 * (or be sourced from the same gpio_pin_t statics) so the guarantee holds.
 * ------------------------------------------------------------------------ */
static const gpio_pin_t k_reserved_pins[] = {
    { 0u,  2u },   /* PA2  — SoftSPI MOSI  (SOFT_SPI_MOSI_PORT/PIN) */
    { 0u,  3u },   /* PA3  — SoftSPI MISO  (SOFT_SPI_MISO_PORT/PIN) */
    { 1u, 10u },   /* PB10 — SoftSPI SCK   (SOFT_SPI_SCK_PORT/PIN)  */
    { 1u, 11u },   /* PB11 — SoftSPI CS    (SOFT_SPI_CS_PORT/PIN)   */
    { 1u, 12u },   /* PB12 — MCP2515 INT   (SOFT_SPI_INT_PORT/PIN)  */
};
#define K_RESERVED_PIN_COUNT (sizeof(k_reserved_pins) / sizeof(k_reserved_pins[0]))

static int pin_reserved(gpio_pin_t newpin) {
    for (size_t i = 0; i < K_RESERVED_PIN_COUNT; i++) {
        if (pin_eq(k_reserved_pins[i], newpin)) {
            return 1;
        }
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * Validator 2: profile-wide pin uniqueness.
 * Reject if newpin is already claimed by ANY OTHER active function in ACTIVE,
 * or by a reserved peripheral pin outside board_profile_t (SoftSPI/MCP2515).
 * ------------------------------------------------------------------------ */
static int pin_conflicts(const field_meta_t *self, gpio_pin_t newpin) {
    if (pin_reserved(newpin)) {
        return 1;  /* collides with a reserved peripheral line (e.g. CAN INT) */
    }
    for (size_t i = 0; i < K_FIELD_COUNT; i++) {
        if (k_fields[i].field_id == self->field_id) {
            continue;  /* a field never conflicts with itself */
        }
        gpio_pin_t *other = (gpio_pin_t *)((uint8_t *)&ACTIVE + k_fields[i].slot_off);
        if (pin_set(*other) && pin_eq(*other, newpin)) {
            return 1;
        }
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * Validator 3: EXTI-line allocation (hall / EXTI-class only).
 * EXTI line number == pin number; reject if another EXTI-active (hall) pin
 * already occupies that line number.
 *
 * Scope note: on this firmware/topology the ONLY hardware EXTI sources are the
 * halls (GPIO_MODE_IT, setup.c; EXTI9_5/EXTI15_10 in hallinterrupts.c). Button,
 * charger and the MCP2515 INT are POLLED (GPIO_MODE_INPUT / IDR reads), not EXTI
 * lines, so iterating CLS_HALL alone is correct here. If any of those ever
 * becomes a hardware EXTI source, widen this scan to include it.
 * ------------------------------------------------------------------------ */
static int exti_conflicts(const field_meta_t *self, gpio_pin_t newpin) {
    for (size_t i = 0; i < K_FIELD_COUNT; i++) {
        if (k_fields[i].cls != CLS_HALL) {
            continue;  /* only EXTI-class fields allocate EXTI lines */
        }
        if (k_fields[i].field_id == self->field_id) {
            continue;
        }
        gpio_pin_t *other = (gpio_pin_t *)((uint8_t *)&ACTIVE + k_fields[i].slot_off);
        if (pin_set(*other) && other->pin == newpin.pin) {
            return 1;  /* same EXTI line number already taken */
        }
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * Persisted-set helpers.
 * ------------------------------------------------------------------------ */
static void stage_persist(uint8_t field_id, gpio_pin_t newpin, int pending) {
    board_override_t *ov = &FlashContent.board_override;
    /* update existing entry for this field, if present */
    for (uint16_t i = 0; i < ov->count && i < BOARD_OVERRIDE_MAX; i++) {
        if (ov->entry[i].field_id == field_id) {
            ov->entry[i].pin   = newpin;
            ov->entry[i].flags = pending ? 0x01u : 0x00u;
            FlashContent.board_override_valid = 1;
            return;
        }
    }
    if (ov->count < BOARD_OVERRIDE_MAX) {
        ov->entry[ov->count].field_id = field_id;
        ov->entry[ov->count].flags    = pending ? 0x01u : 0x00u;
        ov->entry[ov->count].pin      = newpin;
        ov->count++;
        FlashContent.board_override_valid = 1;
    }
}

/* Live HAL_GPIO re-init of a GPIO-class pin, SOURCED FROM ACTIVE via the
 * board_active.h contract (BOARD_GPIO_INIT), never from defines.h directly. */
static void gpio_reinit_live(const field_meta_t *m) {
    GPIO_InitTypeDef init = {0};
    init.Mode  = m->gpio_mode;
    init.Pull  = m->gpio_pull;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_pin_t gp = *slot_ptr(m);
    BOARD_GPIO_INIT(&init, gp, m->def_port, m->def_mask);
}

/* --------------------------------------------------------------------------
 * Public API.
 * ------------------------------------------------------------------------ */
/* One-shot arming for a self-hold pin change. Cleared after the next
 * board_apply_override() of BOARD_FIELD_SELF_HOLD (whether it succeeds or is
 * rejected), so every self-hold change needs a fresh, deliberate confirm. */
static uint8_t s_self_hold_confirmed = 0u;

void board_override_confirm_self_hold(uint8_t on) {
    s_self_hold_confirmed = on ? 1u : 0u;
}

override_result_t board_apply_override(uint8_t field_id, gpio_pin_t newpin) {
    const field_meta_t *m = find_meta(field_id);
    if (m == (const field_meta_t *)0) {
        return REJECTED_SILICON;  /* unknown field -> treat as illegal */
    }

    /* (1) SILICON */
    if (!silicon_ok(m, newpin)) {
        return REJECTED_SILICON;
    }
    /* (2) PROFILE-WIDE UNIQUENESS */
    if (pin_conflicts(m, newpin)) {
        return REJECTED_CONFLICT;
    }
    /* (3) EXTI-LINE (hall/EXTI-class only) */
    if (m->cls == CLS_HALL && exti_conflicts(m, newpin)) {
        return REJECTED_EXTI;
    }

    /* Self-hold is the power latch: NEVER applied live (re-initing it mid-run
     * drops power), and gated behind a one-shot confirm so a stray command
     * can't persist a self-hold pin that soft-bricks the next boot. */
    if (m->cls == CLS_SELF_HOLD) {
        uint8_t confirmed = s_self_hold_confirmed;
        s_self_hold_confirmed = 0u;            /* consume the confirm regardless */
        if (!confirmed) {
            return REJECTED_CONFLICT;          /* not armed -> refuse */
        }
        *slot_ptr(m) = newpin;
        stage_persist(field_id, newpin, /*pending=*/1);
        return PENDING_REBOOT;                 /* applied by boot_replay before latch */
    }

    /* Route. */
    if (m->cls == CLS_GPIO) {
        /* LIVE: update ACTIVE then re-init the HW from ACTIVE. */
        *slot_ptr(m) = newpin;
        gpio_reinit_live(m);
        stage_persist(field_id, newpin, /*pending=*/0);
        return APPLIED_LIVE;
    }

    /* PENDING_REBOOT: stage in ACTIVE + persisted set, apply next boot.
     * NEVER HAL_GPIO_Init a phase/ADC/hall pin live. */
    *slot_ptr(m) = newpin;
    stage_persist(field_id, newpin, /*pending=*/1);
    return PENDING_REBOOT;
}

int board_override_commit(void) {
    if (!FlashContent.board_override_valid) {
        return 0;
    }
    if (FlashContent.magic != CURRENT_MAGIC) {
        FlashContent.magic = CURRENT_MAGIC;
    }
    /* Reuse the existing FlashContent commit path. */
    writeFlash((unsigned char *)&FlashContent, sizeof(FlashContent));
    return 1;
}

void board_override_boot_replay(void) {
    if (!FlashContent.board_override_valid) {
        return;
    }
    board_override_t *ov = &FlashContent.board_override;
    for (uint16_t i = 0; i < ov->count && i < BOARD_OVERRIDE_MAX; i++) {
        if (ov->entry[i].field_id == 0xFFu) {
            continue;
        }
        const field_meta_t *m = find_meta(ov->entry[i].field_id);
        if (m == (const field_meta_t *)0) {
            continue;
        }
        /* Restore the ACTIVE slot so every consumer sees the override. The live
         * HW init for GPIO-class pins is done by setup.c (which sources ACTIVE);
         * phase/ADC/hall pins are picked up by their own boot init. */
        *slot_ptr(m) = ov->entry[i].pin;
    }
}
