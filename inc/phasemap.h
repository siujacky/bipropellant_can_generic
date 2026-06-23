/*
 * phasemap.h — PhaseMap Wizard public API.
 *
 * Interactive wizard that identifies which GPIO pins drive which motor phases
 * on an unknown hoverboard controller. Three independent safety lines protect
 * against FET latch-up during probing:
 *
 *   1. COMPLEMENT LOCKOUT: all non-probed candidate pins are driven LOW before
 *      any candidate pin goes HIGH (atomic critical section).
 *   2. HARDWARE TIMER HARD LIMIT: SysTick ISR forces all candidates LOW and
 *      sets pm_abort after MAX_PROBE_MS=20ms regardless of main-loop state.
 *   3. CURRENT WATCHDOG (AUTO mode only): if battery ADC droop exceeds 2.0V
 *      after a probe, all outputs are disabled immediately.
 *
 * Usage (main loop):
 *   phasemap_start(PM_MODE_GUIDED);   // or PM_MODE_AUTO_ADC
 *   while (phasemap_state() != PM_DONE && phasemap_state() != PM_ABORTED) {
 *       phasemap_tick();              // call each main-loop iteration
 *       if (new_serial_char)
 *           phasemap_char(c);
 *   }
 */
#pragma once

#include <stdint.h>
#include "board_table.h"   /* gpio_pin_t */

/* =========================================================================
 * Timing constants (exposed so tests can advance mock tick correctly)
 * ======================================================================= */
#define PHASEMAP_MAX_PROBE_MS   20u   /* hard FET-on limit */
#define PHASEMAP_SETTLE_MS       5u   /* transient settle after asserting pin */
#define PHASEMAP_COOLDOWN_MS   150u   /* inter-probe thermal recovery */
#define PHASEMAP_HALL_WINDOW_MS 5000u /* user-rotation window */

/* =========================================================================
 * Public types
 * ======================================================================= */

typedef enum {
    PM_MODE_GUIDED,    /* operator-confirmed probe: serial "y/n" per pin */
    PM_MODE_AUTO_ADC,  /* autonomous: use ADC battery droop to detect fired pin */
} pm_mode_t;

typedef enum {
    PM_IDLE,           /* not started */
    PM_SAFETY_GATE,    /* waiting for operator to confirm dummy load + safety */
    PM_SCAN_ADC,       /* AUTO only: scan ADC channels to find battery channel */
    PM_PROBE_LOWSIDE,  /* probing low-side gate candidates one at a time */
    PM_PROBE_HALL,     /* probing hall sensors (100% safe — no driving) */
    PM_BUILD_PROFILE,  /* inferring high-side pairs, grouping into phases */
    PM_DONE,           /* wizard complete; TOML profile printed */
    PM_ABORTED,        /* emergency stop triggered */
} pm_state_t;

/* Result record for one low-side candidate probe. */
typedef struct {
    gpio_pin_t pin;
    uint8_t    timer_id;              /* 1=TIM1, 8=TIM8 */
    uint8_t    channel;               /* 1,2,3 */
    uint8_t    confirmed;             /* 1 = active gate confirmed */
    int16_t    voltage_droop_mv;      /* mV droop in AUTO mode; 0 in GUIDED */
} pm_probe_t;

/* Maximum number of low-side candidates we can track. On STM32F1 there are
 * 6 low-side complementary outputs (TIM1_CH1N..TIM8_CH3N) plus margin. */
#define PM_MAX_CANDIDATES  16u
#define PM_MAX_HALL_CANDS  12u

/* =========================================================================
 * Public API
 * ======================================================================= */

/* Begin the wizard.  Must be called from the main-loop context (not ISR).
 * Resets all internal state and moves to PM_SAFETY_GATE. */
void phasemap_start(pm_mode_t mode);

/* Advance the state machine.  Call once per main-loop iteration.
 * Never call from an ISR — the state machine touches GPIO and serial I/O. */
void phasemap_tick(void);

/* Feed one character of serial input to the wizard.  Safe to call from a
 * receive ISR (accesses only a volatile ring buffer). */
void phasemap_char(char c);

/* Emergency stop — drives all candidate pins LOW via direct register write
 * (GPIOx->BRR) and sets the abort flag.  Safe to call from any context
 * including ISRs; does NOT call HAL. */
void phasemap_abort(void);

/* Query current wizard state (safe from any context). */
pm_state_t phasemap_state(void);

/* =========================================================================
 * Probe-result accessors (valid after PM_BUILD_PROFILE or PM_DONE)
 * ======================================================================= */

/* Number of confirmed low-side probes. */
uint8_t phasemap_confirmed_count(void);

/* Access a specific probe result (idx 0 .. phasemap_confirmed_count()-1).
 * Returns a zeroed struct if idx is out of range. */
pm_probe_t phasemap_probe_result(uint8_t idx);
