/*
 * phasemap.c — PhaseMap Wizard core implementation.
 *
 * SAFETY ARCHITECTURE (three independent lines):
 *
 *  1. COMPLEMENT LOCKOUT
 *     Before any probe pin goes HIGH, ALL other candidate pins are configured
 *     GPIO_OUTPUT_PP and driven LOW in a single atomic block (interrupts
 *     disabled). The timer is NOT started; we drive gate inputs directly.
 *
 *  2. HARDWARE TIMER HARD LIMIT (SysTick)
 *     MAX_PROBE_MS = 20 ms.  When phasemap_arm_systick() is called, we store
 *     the current SysTick counter and check it every tick.  If the deadline
 *     passes with the pin still HIGH, the tick handler drives all candidates
 *     LOW via direct register writes and sets pm_abort.  On bare-metal the
 *     SysTick ISR (SysTick_Handler) also calls phasemap_systick_isr() to
 *     enforce the limit from the ISR itself.
 *
 *  3. CURRENT WATCHDOG (AUTO mode only)
 *     After each probe, if the battery ADC droop > 2000 mV (runaway current /
 *     FET or dummy-load fault), phasemap_abort() is called immediately.
 *
 * EXCLUDED PIN: BOARD_FIELD_SELF_HOLD (ACTIVE.self_hold) is NEVER probed.
 *
 * TIMER NOT STARTED: We configure candidate pins as GPIO_OUTPUT_PP only.
 *     The PWM timer peripheral is never initialised during the wizard.
 *
 * NO HAL_Delay(): timing is based on the SysTick counter (HAL_GetTick()), not
 *     HAL_Delay, to keep the main-loop executor running.
 */

#include "phasemap.h"
#include "board_table.h"
#include "board_active.h"   /* ACTIVE, BOARD_PIN_IS_UNSET, BOARD_PORT_PTR */

#include "stm32f1xx_hal.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* =========================================================================
 * Timing constants — aliases to the public phasemap.h names
 * ======================================================================= */
#define MAX_PROBE_MS   PHASEMAP_MAX_PROBE_MS
#define SETTLE_MS      PHASEMAP_SETTLE_MS
#define COOLDOWN_MS    PHASEMAP_COOLDOWN_MS
#define HALL_WINDOW_MS PHASEMAP_HALL_WINDOW_MS
#define HALL_SAMPLE_HZ   10u  /* hall sample rate during rotation window */

/* ADC battery droop abort threshold: 2000 mV */
#define BATT_DROOP_ABORT_MV  2000

/* ADC scan: ratio range for an 8x–15x resistor divider */
#define BATT_RATIO_LOW   0.065f
#define BATT_RATIO_HIGH  0.120f

/* Voltage supply for ratio calculation (set by user; default 36V = 36000 mV) */
#define DEFAULT_VSUPPLY_MV  36000u

/* =========================================================================
 * Serial input ring buffer (ISR-safe)
 * ======================================================================= */
#define SERIAL_BUF_SIZE 32u

static volatile char   s_serial_buf[SERIAL_BUF_SIZE];
static volatile uint8_t s_serial_head = 0;
static volatile uint8_t s_serial_tail = 0;

static inline void serial_push(char c)
{
    uint8_t next = (uint8_t)((s_serial_head + 1u) % SERIAL_BUF_SIZE);
    if (next != s_serial_tail) {
        s_serial_buf[s_serial_head] = c;
        s_serial_head = next;
    }
}

static inline int serial_pop(char *out)
{
    if (s_serial_head == s_serial_tail) return 0;
    *out = s_serial_buf[s_serial_tail];
    s_serial_tail = (uint8_t)((s_serial_tail + 1u) % SERIAL_BUF_SIZE);
    return 1;
}

/* =========================================================================
 * Candidate pin table
 *
 * Low-side candidates: all complementary timer outputs (suffix 'N') from
 * g_af_validity that are NOT the self_hold pin.
 * Hall candidates: all NON-complementary timer pins plus known hall pins.
 * ======================================================================= */

/* One entry per candidate pin we will probe */
typedef struct {
    gpio_pin_t pin;
    uint8_t    timer_id;   /* from g_af_validity peripheral name "TIM1"→1 */
    uint8_t    channel;    /* 1,2,3 from channel name "CH1N" → 1 */
    uint8_t    is_lowside; /* 1 if channel name ends with 'N' */
} candidate_t;

static candidate_t s_candidates[PM_MAX_CANDIDATES];
static uint8_t     s_cand_count = 0;

/* Hall candidate table (pins that changed state during user-rotation) */
typedef struct {
    gpio_pin_t pin;
    uint8_t    changed; /* 1 = changed during rotation window */
    uint8_t    baseline;
} hall_cand_t;

static hall_cand_t s_hall_cands[PM_MAX_HALL_CANDS];
static uint8_t     s_hall_cand_count = 0;

/* Probe results */
static pm_probe_t s_probes[PM_MAX_CANDIDATES];
static uint8_t    s_probe_count = 0;

/* =========================================================================
 * State machine
 * ======================================================================= */
static volatile pm_state_t s_state      = PM_IDLE;
static volatile uint8_t    pm_abort     = 0;  /* set by SysTick ISR or watchdog */
static pm_mode_t           s_mode       = PM_MODE_GUIDED;

/* Tick timestamp of the current sub-state entry */
static uint32_t s_substate_enter_ms = 0;

/* Current probe index (which candidate is being probed) */
static uint8_t  s_probe_idx     = 0;

/* Sub-state within a probe sequence */
typedef enum {
    PROBE_SUB_IDLE = 0,
    PROBE_SUB_SETUP,      /* atomic pin setup (critical section) */
    PROBE_SUB_SETTLE,     /* waiting SETTLE_MS */
    PROBE_SUB_MEASURE,    /* reading ADC in AUTO mode */
    PROBE_SUB_HOLD,       /* waiting for remaining probe window */
    PROBE_SUB_RELEASE,    /* drive LOW, reconfigure as INPUT */
    PROBE_SUB_COOLDOWN,   /* waiting COOLDOWN_MS */
    PROBE_SUB_USER_QUERY, /* GUIDED: waiting for y/n from user */
    PROBE_SUB_DONE,
} probe_sub_t;

static probe_sub_t s_probe_sub = PROBE_SUB_IDLE;

/* SysTick-based hard limit */
static uint32_t s_probe_start_ms = 0; /* tick when we drove the pin HIGH */
static uint8_t  s_pin_is_high    = 0; /* is any candidate pin currently HIGH? */

/* ADC battery channel */
static uint8_t  s_batt_adc_ch   = 0xFF; /* 0xFF = not found */
static uint32_t s_vsupply_mv     = DEFAULT_VSUPPLY_MV;

/* Baseline battery voltage (ADC raw) before probing this pin */
static uint16_t s_batt_adc_baseline = 0;

/* Safety gate accumulator */
static char s_gate_buf[32];
static uint8_t s_gate_len = 0;

/* =========================================================================
 * Serial output helper (wraps printf; firmware uses softwareserial/USART)
 * On target, assume printf() routes to the debug serial.
 * ======================================================================= */
#define PM_PRINT(...)   printf(__VA_ARGS__)

/* =========================================================================
 * Low-level GPIO helpers (ISR-safe direct register access)
 * ======================================================================= */

/* Drive a pin LOW via direct register write — safe from any ISR context. */
static inline void pin_drive_low_isr(gpio_pin_t gp)
{
    if (gp.port == 0xFFu) return;
    GPIO_TypeDef *port = BOARD_PORT_PTR(gp.port);
    if (!port) return;
    port->BRR = (uint32_t)(1u << gp.pin);
}

/* Drive a pin HIGH via direct register write. */
static inline void pin_drive_high(gpio_pin_t gp)
{
    if (gp.port == 0xFFu) return;
    GPIO_TypeDef *port = BOARD_PORT_PTR(gp.port);
    if (!port) return;
    port->BSRR = (uint32_t)(1u << gp.pin);
}

/* Configure a pin as GPIO_OUTPUT_PP at 2 MHz (enough for gate drive).
 * Uses HAL_GPIO_Init — works on all supported families (STM32F1, GD32F1, etc.).
 * The timer is NOT started; we are driving the gate input directly as GPIO.
 * NEVER configure as AF mode during the wizard.
 */
static void pin_config_output_pp(gpio_pin_t gp)
{
    if (gp.port == 0xFFu) return;
    GPIO_TypeDef *port = BOARD_PORT_PTR(gp.port);
    if (!port) return;
    GPIO_InitTypeDef cfg = {0};
    cfg.Pin   = (uint32_t)(1u << gp.pin);
    cfg.Mode  = GPIO_MODE_OUTPUT_PP;
    cfg.Pull  = GPIO_NOPULL;
    cfg.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(port, &cfg);
}

/* Configure a pin as Hi-Z input (no pull). */
static void pin_config_input_hiz(gpio_pin_t gp)
{
    if (gp.port == 0xFFu) return;
    GPIO_InitTypeDef cfg = {0};
    cfg.Pin   = (uint32_t)(1u << gp.pin);
    cfg.Mode  = GPIO_MODE_INPUT;
    cfg.Pull  = GPIO_NOPULL;
    cfg.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_TypeDef *port = BOARD_PORT_PTR(gp.port);
    if (port) HAL_GPIO_Init(port, &cfg);
}

/* Configure a pin as input with pull-up (for hall sensing). */
static void pin_config_input_pullup(gpio_pin_t gp)
{
    if (gp.port == 0xFFu) return;
    GPIO_InitTypeDef cfg = {0};
    cfg.Pin   = (uint32_t)(1u << gp.pin);
    cfg.Mode  = GPIO_MODE_INPUT;
    cfg.Pull  = GPIO_PULLUP;
    cfg.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_TypeDef *port = BOARD_PORT_PTR(gp.port);
    if (port) HAL_GPIO_Init(port, &cfg);
}

/* Read a digital input pin. Returns 1 if HIGH. */
static uint8_t pin_read(gpio_pin_t gp)
{
    if (gp.port == 0xFFu) return 0;
    GPIO_TypeDef *port = BOARD_PORT_PTR(gp.port);
    if (!port) return 0;
    return (port->IDR & (uint32_t)(1u << gp.pin)) ? 1u : 0u;
}

/* =========================================================================
 * Candidate list builder
 *
 * Scans g_af_validity for entries whose peripheral starts with "TIM" and
 * channel ends with 'N' (complementary low-side).  Excludes:
 *   - The self_hold pin (ACTIVE.self_hold)
 *   - Entries whose pin matches ACTIVE.self_hold
 * ======================================================================= */

static uint8_t pins_equal(gpio_pin_t a, gpio_pin_t b)
{
    return (a.port == b.port && a.pin == b.pin) ? 1u : 0u;
}

/* Parse timer_id from peripheral name "TIM1" → 1, "TIM8" → 8.
 * Returns 0 if not a recognised TIMx peripheral. */
static uint8_t parse_timer_id(const char *periph)
{
    if (periph[0] != 'T' || periph[1] != 'I' || periph[2] != 'M') return 0;
    return (uint8_t)atoi(periph + 3);
}

/* Parse channel number from channel string "CH1N" → 1, "CH2N" → 2 etc.
 * Returns 0 if not a CHx / CHxN pattern. */
static uint8_t parse_channel(const char *ch)
{
    if (ch[0] != 'C' || ch[1] != 'H') return 0;
    return (uint8_t)atoi(ch + 2);
}

/* Returns 1 if channel string ends with 'N' (complementary). */
static uint8_t channel_is_lowside(const char *ch)
{
    uint8_t len = 0;
    while (ch[len]) len++;
    return (len > 0 && ch[len - 1u] == 'N') ? 1u : 0u;
}

static void build_candidate_list(void)
{
    s_cand_count = 0;
    gpio_pin_t self_hold = ACTIVE.self_hold;

    for (uint16_t i = 0; i < g_af_validity_count && s_cand_count < PM_MAX_CANDIDATES; i++) {
        const af_validity_t *e = &g_af_validity[i];

        /* Skip non-TIM peripherals */
        uint8_t tid = parse_timer_id(e->peripheral);
        if (tid == 0) continue;

        /* Only TIM1 and TIM8 (the motor timers) */
        if (tid != 1u && tid != 8u) continue;

        /* Only low-side (complementary 'N') outputs */
        if (!channel_is_lowside(e->channel)) continue;

        /* Exclude self_hold pin */
        if (!BOARD_PIN_IS_UNSET(self_hold) && pins_equal(e->pin, self_hold)) continue;

        /* No duplicate pins */
        uint8_t dup = 0;
        for (uint8_t j = 0; j < s_cand_count; j++) {
            if (pins_equal(s_candidates[j].pin, e->pin)) { dup = 1; break; }
        }
        if (dup) continue;

        s_candidates[s_cand_count].pin       = e->pin;
        s_candidates[s_cand_count].timer_id  = tid;
        s_candidates[s_cand_count].channel   = parse_channel(e->channel);
        s_candidates[s_cand_count].is_lowside = 1u;
        s_cand_count++;
    }
}

/* Build hall candidate list from all NON-complementary timer pins and
 * any entry in g_af_validity that is not already in the lowside candidate set.
 * Hall pins are commonly on ordinary GPIO without any AF requirement. */
static void build_hall_candidate_list(void)
{
    s_hall_cand_count = 0;

    /* Add known hall pins from ACTIVE profile */
    gpio_pin_t hall_pins[6] = {
        ACTIVE.halls_left.hall_a,  ACTIVE.halls_left.hall_b,  ACTIVE.halls_left.hall_c,
        ACTIVE.halls_right.hall_a, ACTIVE.halls_right.hall_b, ACTIVE.halls_right.hall_c,
    };
    for (uint8_t i = 0; i < 6u && s_hall_cand_count < PM_MAX_HALL_CANDS; i++) {
        if (BOARD_PIN_IS_UNSET(hall_pins[i])) continue;
        s_hall_cands[s_hall_cand_count].pin      = hall_pins[i];
        s_hall_cands[s_hall_cand_count].changed  = 0;
        s_hall_cands[s_hall_cand_count].baseline = 0;
        s_hall_cand_count++;
    }

    /* Also add non-complementary TIM pins from g_af_validity as potential hall */
    for (uint16_t i = 0; i < g_af_validity_count && s_hall_cand_count < PM_MAX_HALL_CANDS; i++) {
        const af_validity_t *e = &g_af_validity[i];
        uint8_t tid = parse_timer_id(e->peripheral);
        if (tid != 0) continue;  /* skip timer pins; halls are not timer AF */

        /* check not already in hall list */
        uint8_t dup = 0;
        for (uint8_t j = 0; j < s_hall_cand_count; j++) {
            if (pins_equal(s_hall_cands[j].pin, e->pin)) { dup = 1; break; }
        }
        if (!dup) {
            s_hall_cands[s_hall_cand_count].pin     = e->pin;
            s_hall_cands[s_hall_cand_count].changed = 0;
            s_hall_cands[s_hall_cand_count].baseline= 0;
            s_hall_cand_count++;
        }
    }
}

/* =========================================================================
 * Emergency stop — drives ALL candidate pins LOW via direct register write.
 * Safe from any ISR context: uses volatile flag, no HAL.
 * ======================================================================= */
void phasemap_abort(void)
{
    pm_abort = 1;
    s_pin_is_high = 0;

    /* Drive all candidate pins LOW via BRR (atomic per-port) */
    /* Accumulate masks per port first, then write once per port */
    uint32_t port_masks[8] = {0};
    for (uint8_t i = 0; i < s_cand_count; i++) {
        gpio_pin_t gp = s_candidates[i].pin;
        if (gp.port < 8u) {
            port_masks[gp.port] |= (uint32_t)(1u << gp.pin);
        }
    }

    /* Write all ports */
    for (uint8_t p = 0; p < 8u; p++) {
        if (!port_masks[p]) continue;
        GPIO_TypeDef *port = BOARD_PORT_PTR(p);
        if (port) port->BRR = port_masks[p];
    }

    s_state = PM_ABORTED;
}

/* Called from SysTick_Handler on the target (the handler calls this every 1ms).
 * Also called from phasemap_tick() to handle the soft-limit path. */
void phasemap_systick_isr(void)
{
    if (!s_pin_is_high) return;
    if (pm_abort) {
        s_pin_is_high = 0;
        return;
    }
    /* Check hard limit: force all pins LOW if we have exceeded MAX_PROBE_MS */
    uint32_t now = HAL_GetTick();
    if ((now - s_probe_start_ms) >= MAX_PROBE_MS) {
        /* Drive all candidate pins LOW via BRR — ISR safe */
        uint32_t port_masks[8] = {0};
        for (uint8_t i = 0; i < s_cand_count; i++) {
            gpio_pin_t gp = s_candidates[i].pin;
            if (gp.port < 8u) port_masks[gp.port] |= (uint32_t)(1u << gp.pin);
        }
        for (uint8_t p = 0; p < 8u; p++) {
            if (!port_masks[p]) continue;
            GPIO_TypeDef *port = BOARD_PORT_PTR(p);
            if (port) port->BRR = port_masks[p];
        }
        s_pin_is_high = 0;
        pm_abort = 1;
        s_state = PM_ABORTED;
    }
}

/* =========================================================================
 * ADC helpers
 *
 * On target: assumes ADC1 is accessible via the hadc1 handle (declared in
 * main.c).  For the host test build these are replaced by stubs.
 * ======================================================================= */

#ifndef PHASEMAP_HOST_TEST
extern ADC_HandleTypeDef hadc1;

/* Software-trigger a single ADC1 conversion on the given channel.
 * Returns the 12-bit result (0-4095). */
static uint16_t adc_single_read(uint8_t channel)
{
    /* Reconfigure ADC1 channel (simplified; assumes SW trigger + no DMA) */
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel      = channel;
    sConfig.Rank         = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint16_t val = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return val;
}

static uint16_t adc_average(uint8_t channel, uint8_t samples)
{
    uint32_t acc = 0;
    for (uint8_t i = 0; i < samples; i++) acc += adc_single_read(channel);
    return (uint16_t)(acc / samples);
}

#else
/* Host-test stubs */
static uint16_t s_mock_adc_val = 2048u;  /* overridden by tests */

void phasemap_mock_adc_set(uint16_t val) { s_mock_adc_val = val; }

static uint16_t adc_single_read(uint8_t channel)
{
    (void)channel;
    return s_mock_adc_val;
}
static uint16_t adc_average(uint8_t channel, uint8_t samples)
{
    (void)samples;
    return adc_single_read(channel);
}
#endif /* PHASEMAP_HOST_TEST */

/* =========================================================================
 * ADC channel scan (PM_SCAN_ADC)
 *
 * For ch=0..15: sample 64 times and check ratio against divider window.
 * Stores the best candidate in s_batt_adc_ch.
 * ======================================================================= */
static void run_adc_scan(void)
{
    uint8_t  best_ch    = 0xFF;
    float    best_delta = 1.0f;  /* how close to centre of (0.065..0.120) */
    float    centre     = (BATT_RATIO_LOW + BATT_RATIO_HIGH) * 0.5f;

    PM_PRINT("[PhaseMap] ADC scan: looking for battery divider (ratio %.3f-%.3f)\r\n",
             (double)BATT_RATIO_LOW, (double)BATT_RATIO_HIGH);

    for (uint8_t ch = 0; ch < 16u; ch++) {
        uint16_t raw = adc_average(ch, 64u);
        float ratio = (float)raw / 4096.0f * 3.3f / ((float)s_vsupply_mv / 1000.0f);

        if (ratio > BATT_RATIO_LOW && ratio < BATT_RATIO_HIGH) {
            float delta = (ratio > centre) ? (ratio - centre) : (centre - ratio);
            if (delta < best_delta) {
                best_delta = delta;
                best_ch    = ch;
            }
            PM_PRINT("[PhaseMap]   ADC ch%u raw=%u ratio=%.4f  <-- candidate\r\n",
                     ch, raw, (double)ratio);
        }
    }

    s_batt_adc_ch = best_ch;
    if (best_ch == 0xFF) {
        PM_PRINT("[PhaseMap] ADC scan: no battery channel found. Vsupply_mv=%lu\r\n",
                 (unsigned long)s_vsupply_mv);
    } else {
        PM_PRINT("[PhaseMap] ADC scan: battery channel = ADC1_IN%u\r\n", best_ch);
    }
}

/* Read battery voltage in mV.  Uses s_batt_adc_ch if valid, else falls back
 * to ACTIVE.battery_voltage slot. Returns 0 if no channel is available. */
static uint32_t read_batt_mv(void)
{
    uint8_t ch = s_batt_adc_ch;

    /* Fall back to ACTIVE slot if scan was not run or failed */
    if (ch == 0xFF && !BOARD_PIN_IS_UNSET(ACTIVE.battery_voltage.on_pin)) {
        ch = ACTIVE.battery_voltage.channel;
    }
    if (ch == 0xFF) return 0u;

    uint16_t raw = adc_average(ch, 4u);
    /* Convert: raw * 3300mV / 4096 * divider */
    float divider = (ACTIVE.battery_voltage.divider > 0.0f)
                    ? ACTIVE.battery_voltage.divider : 11.0f;
    float mv = (float)raw / 4096.0f * 3300.0f * divider;
    return (uint32_t)mv;
}

/* =========================================================================
 * Complement lockout + probe pin setup
 *
 * Must be called inside a critical section (interrupts disabled).
 * Configures ALL candidates as OUTPUT_PP and drives them LOW, then
 * configures and drives the target pin HIGH.
 * ======================================================================= */
static void probe_pin_atomic_setup(uint8_t probe_idx)
{
    /* Step 1: configure ALL candidates OUTPUT_PP and drive LOW */
    for (uint8_t i = 0; i < s_cand_count; i++) {
        pin_config_output_pp(s_candidates[i].pin);
        pin_drive_low_isr(s_candidates[i].pin);
    }

    /* Step 2: drive the target pin HIGH */
    pin_drive_high(s_candidates[probe_idx].pin);
}

/* =========================================================================
 * High-side inference (PM_BUILD_PROFILE)
 *
 * For each confirmed low-side probe (timer_id, channel), find the
 * corresponding non-complementary (high-side) entry in g_af_validity with the
 * same timer and channel number (channel string does NOT end with 'N').
 * ======================================================================= */
static gpio_pin_t infer_highside(uint8_t timer_id, uint8_t channel)
{
    for (uint16_t i = 0; i < g_af_validity_count; i++) {
        const af_validity_t *e = &g_af_validity[i];
        if (parse_timer_id(e->peripheral) != timer_id) continue;
        if (parse_channel(e->channel) != channel) continue;
        if (channel_is_lowside(e->channel)) continue; /* skip the 'N' entries */
        return e->pin;
    }
    gpio_pin_t unset = {0xFFu, 0xFFu};
    return unset;
}

/* Port index to letter */
static char port_letter(uint8_t port_idx)
{
    return (char)('A' + port_idx);
}

/* =========================================================================
 * TOML profile output (PM_DONE)
 *
 * Groups confirmed pairs:
 *   TIM8 → phases_left  (left motor)
 *   TIM1 → phases_right (right motor)
 * Channel 1→U, 2→V, 3→W.
 * ======================================================================= */

typedef struct {
    uint8_t    channel;
    gpio_pin_t high_pin;
    gpio_pin_t low_pin;
    uint8_t    valid;
} phase_pair_t;

static void print_pin_toml(const char *key, gpio_pin_t gp)
{
    if (BOARD_PIN_IS_UNSET(gp)) {
        PM_PRINT("%s = \"UNKNOWN\"\n", key);
    } else {
        PM_PRINT("%s = \"P%c%u\"\n", key, port_letter(gp.port), gp.pin);
    }
}

static void print_phase_block(const char *section_name, uint8_t timer_id,
                              pm_probe_t *probes, uint8_t probe_count)
{
    phase_pair_t pairs[3] = {0};

    for (uint8_t i = 0; i < probe_count; i++) {
        if (!probes[i].confirmed) continue;
        if (probes[i].timer_id != timer_id) continue;
        uint8_t ch = probes[i].channel;
        if (ch < 1u || ch > 3u) continue;
        pairs[ch - 1u].channel  = ch;
        pairs[ch - 1u].low_pin  = probes[i].pin;
        pairs[ch - 1u].high_pin = infer_highside(timer_id, ch);
        pairs[ch - 1u].valid    = 1u;
    }

    PM_PRINT("[%s]\n", section_name);
    PM_PRINT("timer = \"TIM%u\"\n", timer_id);
    /* Map ch1→U, ch2→V, ch3→W */
    const char *phase_names[3] = {"u", "v", "w"};
    for (uint8_t c = 0; c < 3u; c++) {
        char key_h[16], key_l[16];
        snprintf(key_h, sizeof(key_h), "%s_high", phase_names[c]);
        snprintf(key_l, sizeof(key_l), "%s_low",  phase_names[c]);
        if (pairs[c].valid) {
            print_pin_toml(key_h, pairs[c].high_pin);
            print_pin_toml(key_l, pairs[c].low_pin);
        } else {
            PM_PRINT("%s = \"UNDETECTED\"\n", key_h);
            PM_PRINT("%s = \"UNDETECTED\"\n", key_l);
        }
    }
    PM_PRINT("dead_time_ns = 833\n\n");
}

static void print_toml_profile(void)
{
    PM_PRINT("\n");
    PM_PRINT("# PhaseMap Wizard output — paste into profiles/*.toml\n");
    PM_PRINT("[meta]\n");
    PM_PRINT("name = \"phasemap-detected\"\n");
    PM_PRINT("confidence = 0.8\n\n");

    PM_PRINT("[chip]\n");
    PM_PRINT("family = \"STM32F1\"\n\n");

    PM_PRINT("[power]\n");
    if (!BOARD_PIN_IS_UNSET(ACTIVE.self_hold)) {
        PM_PRINT("self_hold = \"P%c%u\"\n",
                 port_letter(ACTIVE.self_hold.port), ACTIVE.self_hold.pin);
    } else {
        PM_PRINT("self_hold = \"UNKNOWN\"\n");
    }
    PM_PRINT("\n");

    /* phases_left from TIM8, phases_right from TIM1 */
    print_phase_block("phases_left",  8u, s_probes, s_probe_count);
    print_phase_block("phases_right", 1u, s_probes, s_probe_count);

    /* Hall candidates that changed state */
    PM_PRINT("[halls_right]\n");
    uint8_t hall_found = 0;
    const char *hnames[3] = {"hall_a", "hall_b", "hall_c"};
    for (uint8_t i = 0, hi = 0; i < s_hall_cand_count && hi < 3u; i++) {
        if (s_hall_cands[i].changed) {
            gpio_pin_t gp = s_hall_cands[i].pin;
            PM_PRINT("%s = \"P%c%u\"\n", hnames[hi], port_letter(gp.port), gp.pin);
            hi++;
            hall_found = 1;
        }
    }
    if (!hall_found) PM_PRINT("# No hall transitions detected\n");
    PM_PRINT("\n");
}

/* =========================================================================
 * State: PM_SAFETY_GATE
 *
 * Waits for the operator to type "CONFIRM DUMMY LOAD" on the serial console.
 * This verifies that a current-limiting dummy load is connected so probing
 * cannot destroy the motor windings if a FET fires into a live phase.
 * ======================================================================= */
static const char *SAFETY_PHRASE = "CONFIRM DUMMY LOAD";

static void handle_safety_gate(void)
{
    char c;
    while (serial_pop(&c)) {
        if (c == '\r' || c == '\n') {
            s_gate_buf[s_gate_len] = '\0';
            if (strcmp(s_gate_buf, SAFETY_PHRASE) == 0) {
                PM_PRINT("[PhaseMap] Safety gate confirmed.\r\n");

                /* Advance to appropriate next state */
                if (s_mode == PM_MODE_AUTO_ADC) {
                    s_state = PM_SCAN_ADC;
                } else {
                    s_state = PM_PROBE_LOWSIDE;
                }
                s_probe_sub = PROBE_SUB_IDLE;
                s_probe_idx = 0;
            } else {
                PM_PRINT("[PhaseMap] Incorrect phrase. Type exactly: %s\r\n", SAFETY_PHRASE);
                s_gate_len = 0;
            }
            s_gate_len = 0;
        } else {
            if (s_gate_len < (uint8_t)(sizeof(s_gate_buf) - 1u)) {
                s_gate_buf[s_gate_len++] = c;
            }
        }
    }
}

/* =========================================================================
 * State: PM_PROBE_LOWSIDE — probe one candidate per tick cycle
 * ======================================================================= */

static void handle_probe_lowside(void)
{
    uint32_t now = HAL_GetTick();

    if (s_probe_idx >= s_cand_count) {
        /* All candidates probed — advance to hall probe */
        s_state = PM_PROBE_HALL;
        s_substate_enter_ms = now;
        build_hall_candidate_list();
        PM_PRINT("[PhaseMap] Low-side probe complete. Now probing hall sensors.\r\n");
        PM_PRINT("[PhaseMap] Slowly rotate the wheel 1/6 turn (60 degrees) in 5 seconds.\r\n");
        return;
    }

    candidate_t *cand = &s_candidates[s_probe_idx];

    switch (s_probe_sub) {
    case PROBE_SUB_IDLE:
        PM_PRINT("[PhaseMap] Probing P%c%u (TIM%u_CH%u%s) ...\r\n",
                 port_letter(cand->pin.port), cand->pin.pin,
                 cand->timer_id, cand->channel,
                 cand->is_lowside ? "N" : "");

        /* Configure baseline ADC reading before asserting pin */
        if (s_mode == PM_MODE_AUTO_ADC && s_batt_adc_ch != 0xFF) {
            s_batt_adc_baseline = adc_average(s_batt_adc_ch, 4u);
        }

        s_probe_sub = PROBE_SUB_SETUP;
        break;

    case PROBE_SUB_SETUP:
        /* === COMPLEMENT LOCKOUT — atomic critical section === */
        __disable_irq();
        probe_pin_atomic_setup(s_probe_idx);
        s_probe_start_ms = HAL_GetTick();
        s_pin_is_high    = 1;
        __enable_irq();

        s_substate_enter_ms = now;
        s_probe_sub = PROBE_SUB_SETTLE;
        break;

    case PROBE_SUB_SETTLE:
        /* Check hard limit from the main loop too (belt-and-suspenders) */
        phasemap_systick_isr();
        if (pm_abort) return;

        if ((now - s_substate_enter_ms) >= SETTLE_MS) {
            s_probe_sub = (s_mode == PM_MODE_AUTO_ADC) ? PROBE_SUB_MEASURE
                                                        : PROBE_SUB_HOLD;
            s_substate_enter_ms = now;
        }
        break;

    case PROBE_SUB_MEASURE:
        /* AUTO_ADC: read battery droop */
        phasemap_systick_isr();
        if (pm_abort) return;
        {
            /* read_batt_mv() uses s_batt_adc_ch and ACTIVE.battery_voltage.divider */
            uint32_t batt_now_mv = read_batt_mv();
            /* Also read via raw ADC for droop_raw calculation */
            uint16_t raw_now = adc_average(s_batt_adc_ch, 4u);
            float    droop_raw = (float)(s_batt_adc_baseline) - (float)raw_now;
            /* Convert raw droop to mV: droop_mv = droop_raw * 3300 * divider / 4096 */
            float divider = (ACTIVE.battery_voltage.divider > 0.0f)
                            ? ACTIVE.battery_voltage.divider : 11.0f;
            int16_t droop_mv = (int16_t)(droop_raw / 4096.0f * 3300.0f * divider);
            (void)batt_now_mv; /* used as cross-check; primary droop from raw */

            s_probes[s_probe_idx].pin             = cand->pin;
            s_probes[s_probe_idx].timer_id        = cand->timer_id;
            s_probes[s_probe_idx].channel         = cand->channel;
            s_probes[s_probe_idx].voltage_droop_mv = droop_mv;

            /* Confirmed if non-trivial droop (> 50 mV = real current) */
            if (droop_mv > 50) {
                s_probes[s_probe_idx].confirmed = 1u;
                PM_PRINT("[PhaseMap]   -> CONFIRMED (droop %d mV)\r\n", droop_mv);
            } else {
                s_probes[s_probe_idx].confirmed = 0u;
                PM_PRINT("[PhaseMap]   -> not confirmed (droop %d mV)\r\n", droop_mv);
            }
            s_probe_count = s_probe_idx + 1u;

            /* Abort if droop exceeds safety threshold */
            if (droop_mv > BATT_DROOP_ABORT_MV) {
                PM_PRINT("[PhaseMap] ABORT: excessive droop %d mV (FET fault?)\r\n",
                         droop_mv);
                phasemap_abort();
                return;
            }
        }
        s_probe_sub = PROBE_SUB_HOLD;
        s_substate_enter_ms = now;
        break;

    case PROBE_SUB_HOLD:
        phasemap_systick_isr();
        if (pm_abort) return;

        /* In GUIDED mode, wait for user input */
        if (s_mode == PM_MODE_GUIDED) {
            static uint8_t guided_prompted = 0;
            if (!guided_prompted) {
                PM_PRINT("[PhaseMap]   Gate is HIGH. Did you see/hear the motor respond? [y/n]: ");
                guided_prompted = 1;
            }
            char c;
            if (serial_pop(&c)) {
                guided_prompted = 0;
                s_probes[s_probe_idx].pin      = cand->pin;
                s_probes[s_probe_idx].timer_id = cand->timer_id;
                s_probes[s_probe_idx].channel  = cand->channel;
                s_probes[s_probe_idx].voltage_droop_mv = 0;
                if (c == 'y' || c == 'Y') {
                    s_probes[s_probe_idx].confirmed = 1u;
                    PM_PRINT("\r\n[PhaseMap]   -> CONFIRMED (user)\r\n");
                } else {
                    s_probes[s_probe_idx].confirmed = 0u;
                    PM_PRINT("\r\n[PhaseMap]   -> not confirmed (user)\r\n");
                }
                s_probe_count = s_probe_idx + 1u;
                s_probe_sub = PROBE_SUB_RELEASE;
                s_substate_enter_ms = now;
            }
        } else {
            /* AUTO mode: wait remainder of MAX_PROBE_MS */
            if ((now - s_probe_start_ms) >= MAX_PROBE_MS) {
                s_probe_sub = PROBE_SUB_RELEASE;
                s_substate_enter_ms = now;
            }
        }
        break;

    case PROBE_SUB_RELEASE:
        phasemap_systick_isr();
        {
            /* Drive all candidates LOW first */
            __disable_irq();
            for (uint8_t i = 0; i < s_cand_count; i++) {
                pin_drive_low_isr(s_candidates[i].pin);
            }
            s_pin_is_high = 0;
            __enable_irq();

            /* Reconfigure probe pin as Hi-Z input */
            pin_config_input_hiz(cand->pin);
        }
        s_probe_sub = PROBE_SUB_COOLDOWN;
        s_substate_enter_ms = now;
        break;

    case PROBE_SUB_COOLDOWN:
        if ((now - s_substate_enter_ms) >= COOLDOWN_MS) {
            s_probe_idx++;
            s_probe_sub = PROBE_SUB_IDLE;
        }
        break;

    default:
        break;
    }
}

/* =========================================================================
 * State: PM_PROBE_HALL — 100% safe, no driving
 * ======================================================================= */

static void handle_probe_hall(void)
{
    static uint8_t hall_init_done = 0;
    uint32_t now = HAL_GetTick();

    if (!hall_init_done) {
        /* Configure all hall candidates as input with pull-up and read baseline */
        for (uint8_t i = 0; i < s_hall_cand_count; i++) {
            pin_config_input_pullup(s_hall_cands[i].pin);
            s_hall_cands[i].baseline = pin_read(s_hall_cands[i].pin);
            s_hall_cands[i].changed  = 0;
        }
        hall_init_done = 1;
    }

    /* Sample all hall candidates */
    for (uint8_t i = 0; i < s_hall_cand_count; i++) {
        if (pin_read(s_hall_cands[i].pin) != s_hall_cands[i].baseline) {
            s_hall_cands[i].changed = 1u;
        }
    }

    /* Wait for the user rotation window */
    if ((now - s_substate_enter_ms) >= HALL_WINDOW_MS) {
        hall_init_done = 0;
        PM_PRINT("[PhaseMap] Hall probe complete.\r\n");
        for (uint8_t i = 0; i < s_hall_cand_count; i++) {
            gpio_pin_t gp = s_hall_cands[i].pin;
            if (s_hall_cands[i].changed) {
                PM_PRINT("[PhaseMap]   P%c%u — HALL CANDIDATE (changed)\r\n",
                         port_letter(gp.port), gp.pin);
            }
        }
        s_state = PM_BUILD_PROFILE;
    }
}

/* =========================================================================
 * Public API
 * ======================================================================= */

void phasemap_start(pm_mode_t mode)
{
    s_mode        = mode;
    pm_abort      = 0;
    s_cand_count  = 0;
    s_probe_count = 0;
    s_probe_idx   = 0;
    s_probe_sub   = PROBE_SUB_IDLE;
    s_pin_is_high = 0;
    s_gate_len    = 0;
    s_batt_adc_ch = 0xFF;
    s_serial_head = 0;
    s_serial_tail = 0;
    memset(s_probes,     0, sizeof(s_probes));
    memset(s_candidates, 0, sizeof(s_candidates));
    memset(s_hall_cands, 0, sizeof(s_hall_cands));

    /* Build the candidate list from the AF validity table */
    build_candidate_list();

    if (s_cand_count == 0u) {
        PM_PRINT("[PhaseMap] ERROR: no low-side candidates found in g_af_validity.\r\n");
        s_state = PM_ABORTED;
        return;
    }

    s_state = PM_SAFETY_GATE;
    s_substate_enter_ms = HAL_GetTick();

    PM_PRINT("\r\n");
    PM_PRINT("[PhaseMap] === PhaseMap Wizard ===\r\n");
    PM_PRINT("[PhaseMap] Mode: %s\r\n",
             mode == PM_MODE_AUTO_ADC ? "AUTO_ADC" : "GUIDED");
    PM_PRINT("[PhaseMap] Found %u low-side candidate pins.\r\n", s_cand_count);
    PM_PRINT("[PhaseMap] SAFETY: Connect a current-limiting dummy load (e.g. 10 ohm resistor)\r\n");
    PM_PRINT("[PhaseMap]         between each phase output and ground before proceeding.\r\n");
    PM_PRINT("[PhaseMap] Type exactly: %s\r\n", SAFETY_PHRASE);
}

void phasemap_tick(void)
{
    if (pm_abort && s_state != PM_ABORTED) {
        s_state = PM_ABORTED;
        return;
    }

    /* SysTick-equivalent check from main loop */
    if (s_pin_is_high) {
        phasemap_systick_isr();
        if (pm_abort) return;
    }

    switch (s_state) {
    case PM_IDLE:
        break;

    case PM_SAFETY_GATE:
        handle_safety_gate();
        break;

    case PM_SCAN_ADC:
        run_adc_scan();
        s_state = PM_PROBE_LOWSIDE;
        s_probe_sub = PROBE_SUB_IDLE;
        s_probe_idx = 0;
        break;

    case PM_PROBE_LOWSIDE:
        handle_probe_lowside();
        break;

    case PM_PROBE_HALL:
        handle_probe_hall();
        break;

    case PM_BUILD_PROFILE:
        PM_PRINT("[PhaseMap] Building profile...\r\n");
        s_state = PM_DONE;
        print_toml_profile();
        PM_PRINT("[PhaseMap] Done.\r\n");
        break;

    case PM_DONE:
    case PM_ABORTED:
        break;
    }
}

void phasemap_char(char c)
{
    /* Safe from ISR: push into ring buffer */
    serial_push(c);
}

pm_state_t phasemap_state(void)
{
    return s_state;
}

/* =========================================================================
 * Test-only exports (host test build only)
 * ======================================================================= */
#ifdef PHASEMAP_HOST_TEST
/* Expose the high-side inference function so T5 can call it directly. */
gpio_pin_t phasemap_test_infer_highside(uint8_t timer_id, uint8_t channel)
{
    return infer_highside(timer_id, channel);
}
#endif /* PHASEMAP_HOST_TEST */

uint8_t phasemap_confirmed_count(void)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < s_probe_count; i++) {
        if (s_probes[i].confirmed) n++;
    }
    return n;
}

pm_probe_t phasemap_probe_result(uint8_t idx)
{
    uint8_t found = 0;
    for (uint8_t i = 0; i < s_probe_count; i++) {
        if (!s_probes[i].confirmed) continue;
        if (found == idx) return s_probes[i];
        found++;
    }
    pm_probe_t zero = {0};
    return zero;
}
