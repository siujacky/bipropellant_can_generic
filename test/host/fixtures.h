/*
 * fixtures.h — fabricated test fixtures for host-native board_override /
 * board_select unit tests.
 *
 * Included by BOTH test_board_override.c and test_board_select.c BEFORE any
 * project source is compiled. Provides:
 *   - Definitions for the mock GPIO instances and HAL_GPIO_Init counters
 *     (extern-declared in stm32f1xx_hal.h; defined here in the test TU).
 *   - g_af_validity[] / g_af_validity_count   (silicon-legality table)
 *   - ACTIVE (via board_select.c's definition, not redefined here)
 *   - FlashContent + writeFlash stub
 *   - g_board_table[] / g_board_table_count / g_default_board_index
 *   - helper pin-literal macros
 *
 * Pin encoding used everywhere: port 0=A, 1=B, 2=C, ...; pin 0-15.
 * So:  PA8  = {0, 8}   PA9  = {0, 9}
 *      PB12 = {1,12}   PB13 = {1,13}
 *      PC0  = {2, 0}   PC1  = {2, 1}   PC2  = {2, 2}
 *
 * MUST be included AFTER stm32f1xx_hal.h (shim) in each test TU.
 */
#pragma once
#include <stdint.h>
#include <string.h>

/* Pull in the shim first so GPIO_TypeDef, GPIO_InitTypeDef are defined
 * before we provide their definitions below. */
#include "stm32f1xx_hal.h"

#include "board_table.h"    /* gpio_pin_t, af_validity_t, board_profile_t, etc. */
#include "flashcontent.h"   /* FLASH_CONTENT, board_override_t */

/* Forward declare board_select_force before fixture_reset uses it.
 * The full declaration appears in board_override.h which is included AFTER
 * fixtures.h in the test TUs; this forward decl avoids the implicit-function
 * warning and the conflicting-types error. */
void board_select_force(uint16_t index);

/*
 * The definitions for _mock_GPIOx, hal_gpio_init_count, hal_gpio_init_last_port
 * and hal_gpio_init_last_init live in test/host/shim/hal_mock.c (linked into
 * every test binary). Fixtures.h only provides the declarations (via the shim
 * header already included above) — no duplicate definitions here.
 */

/* -------------------------------------------------------------------------
 * Fabricated g_af_validity[] table.
 *
 * Entries chosen to exercise silicon_ok() logic:
 *   PA8  TIM1 CH1 af=1  — legal TIM1 phase pin
 *   PA9  TIM1 CH2 af=1  — legal TIM1 phase pin (second channel)
 *   PB13 TIM1 CH1N af=-1 — TIM1 complementary (remap, af=-1)
 *   PC0  ADC  (af=-1)   — ADC pin on port C (but silicon_ok ADC check accepts port≤2)
 *   PC1  ADC  (af=-1)   — ADC pin on port C
 *   PC2  ADC  (af=-1)   — ADC pin on port C
 *   PB12 SPI  (af=0)    — NOT a TIM/ADC entry; used to test REJECTED_SILICON for
 *                          a phase/ADC field pointing at a non-timer pin.
 *
 * Note: silicon_ok() for CLS_PHASE scans g_af_validity for a "TIMx" prefix;
 *       for CLS_ADC it simply checks pin.port <= 2 (no table scan needed).
 *       The "SPI" peripheral entry on PB12 makes it impossible for a TIM1
 *       phase check to accept PB12 → REJECTED_SILICON in T12 ordering test
 *       only matters for phase fields, not PB12's role in conflict tests.
 * ---------------------------------------------------------------------- */
const af_validity_t g_af_validity[] = {
    /* PA8  — TIM1 CH1 */
    { {0u,  8u}, "TIM1", "CH1",  1  },
    /* PA9  — TIM1 CH2 */
    { {0u,  9u}, "TIM1", "CH2",  1  },
    /* PB13 — TIM1 CH1N (remap) */
    { {1u, 13u}, "TIM1", "CH1N", -1 },
    /* PC0  — ADC channel (af=-1, analog) */
    { {2u,  0u}, "ADC",  "IN10", -1 },
    /* PC1  — ADC channel */
    { {2u,  1u}, "ADC",  "IN11", -1 },
    /* PC2  — ADC channel */
    { {2u,  2u}, "ADC",  "IN12", -1 },
    /* PB12 — SPI (NOT a timer/ADC peripheral — silicon check for TIM fails) */
    { {1u, 12u}, "SPI",  "NSS",   0 },
};
const uint16_t g_af_validity_count =
    (uint16_t)(sizeof(g_af_validity) / sizeof(g_af_validity[0]));

/* -------------------------------------------------------------------------
 * Fabricated board table.
 *
 * Index 0: motor-disabled safe default (all-sentinel pins).
 * Index 1: a real-looking profile that tests board_select narrowing.
 *          family=STM32F1, part_hint="TEST_BOARD_A"
 * Index 2: another profile same family, part_hint="TEST_BOARD_B"
 *
 * ACTIVE is the mutable RAM copy; it is defined in board_select.c.  We do NOT
 * redefine it here.  Tests that need a known ACTIVE state call
 * board_select_init() or manually memcpy a profile in.
 * ---------------------------------------------------------------------- */

/* Sentinel pin helper */
#define PIN_UNSET  { 0xFFu, 0xFFu }

/* Helpers for named pins */
#define PA2   ((gpio_pin_t){0u,  2u})
#define PA3   ((gpio_pin_t){0u,  3u})
#define PA8   ((gpio_pin_t){0u,  8u})
#define PA9   ((gpio_pin_t){0u,  9u})
#define PB0   ((gpio_pin_t){1u,  0u})
#define PB1   ((gpio_pin_t){1u,  1u})
#define PB5   ((gpio_pin_t){1u,  5u})
#define PB6   ((gpio_pin_t){1u,  6u})
#define PB7   ((gpio_pin_t){1u,  7u})
#define PB10  ((gpio_pin_t){1u, 10u})
#define PB11  ((gpio_pin_t){1u, 11u})
#define PB12  ((gpio_pin_t){1u, 12u})
#define PB13  ((gpio_pin_t){1u, 13u})
#define PC0   ((gpio_pin_t){2u,  0u})
#define PC1   ((gpio_pin_t){2u,  1u})
#define PC2   ((gpio_pin_t){2u,  2u})
#define PD0   ((gpio_pin_t){3u,  0u})
#define PE0   ((gpio_pin_t){4u,  0u})

/*
 * A fully-populated profile used as the "ACTIVE" base for override tests.
 * Halls use pins on port B with distinct pin numbers so EXTI-line tests work.
 *   hall_left:  PB5(pin5), PB6(pin6), PB7(pin7)
 *   hall_right: PC0(pin0), PC1(pin1), PC2(pin2)
 * LED green: PD0 — an innocuous pin on port D.
 * self_hold:  PE0 — port E pin 0.
 * Phase-left timer_id=1 (TIM1); u_high=PA8, rest sentinel.
 * Phase-right timer_id=0 (unset) to keep things simple.
 * ADC: battery on PC0 ... but wait, PC0 is used by right hall.
 *   To avoid built-in fixture conflicts let ADC use port A pins that are NOT
 *   in k_reserved_pins and not used elsewhere in the profile:
 *   battery_voltage.on_pin = PA9? No, PA9 is in af_validity as TIM1.
 *   Use sentinel for ADC slots in ACTIVE profile — silicon_ok just checks port.
 *   For T10/T11 we'll pass an explicit newpin, not read from ACTIVE.
 */
static const board_profile_t k_test_profile_active = {
    .family        = BOARD_FAMILY_STM32F1,
    .part_hint     = "TEST_BOARD_A",
    .uid           = "",
    .fingerprint   = 0,
    .motor_count   = 2,
    .self_hold     = { 4u, 0u },           /* PE0 */
    .self_hold_active_high = 1,
    .halls_left    = {
        .hall_a = { 1u, 5u },              /* PB5  pin-number=5 */
        .hall_b = { 1u, 6u },              /* PB6  pin-number=6 */
        .hall_c = { 1u, 7u },              /* PB7  pin-number=7 */
    },
    .halls_right   = {
        .hall_a = { 2u, 0u },              /* PC0  pin-number=0 */
        .hall_b = { 2u, 1u },              /* PC1  pin-number=1 */
        .hall_c = { 2u, 2u },              /* PC2  pin-number=2 */
    },
    .phases_left   = {
        .timer_id  = 1u,                   /* TIM1 */
        .u_high    = { 0u, 8u },           /* PA8  */
        .u_low     = { 0xFFu, 0xFFu },     /* sentinel */
        .v_high    = { 0xFFu, 0xFFu },
        .v_low     = { 0xFFu, 0xFFu },
        .w_high    = { 0xFFu, 0xFFu },
        .w_low     = { 0xFFu, 0xFFu },
        .dead_time_ns = 0,
    },
    .phases_right  = {
        .timer_id  = 0u,                   /* no active timer (disabled) */
        .u_high    = { 0xFFu, 0xFFu },
        .u_low     = { 0xFFu, 0xFFu },
        .v_high    = { 0xFFu, 0xFFu },
        .v_low     = { 0xFFu, 0xFFu },
        .w_high    = { 0xFFu, 0xFFu },
        .w_low     = { 0xFFu, 0xFFu },
        .dead_time_ns = 0,
    },
    /* LED green on PD0: port=3, pin=0 */
    .led_red       = { 0xFFu, 0xFFu },
    .led_green     = { 3u, 0u },           /* PD0 */
    .led_orange    = { 0xFFu, 0xFFu },
    .led_blue      = { 0xFFu, 0xFFu },
    .buzzer        = { 0xFFu, 0xFFu },
    .button        = { 0xFFu, 0xFFu },
    .charger       = { 0xFFu, 0xFFu },
    /* ADC: leave as sentinel (tests construct newpin explicitly) */
    .battery_voltage      = { 0, 0, { 0xFFu, 0xFFu }, 0.0f, 0.0f },
    .dc_link_voltage      = { 0, 0, { 0xFFu, 0xFFu }, 0.0f, 0.0f },
    .left_phase_current   = { 0, 0, { 0xFFu, 0xFFu }, 0.0f, 0.0f },
    .right_phase_current  = { 0, 0, { 0xFFu, 0xFFu }, 0.0f, 0.0f },
    .mcu_temp_channel = 0xFFu,
    .name = "Test Active Profile",
};

/* Safe default row (motor-disabled: all sentinel pins, motor_count=0) */
static const board_profile_t k_test_profile_safe = {
    .family       = BOARD_FAMILY_STM32F1,
    .part_hint    = "SAFE_DEFAULT",
    .uid          = "",
    .fingerprint  = 0,
    .motor_count  = 0,
    .self_hold    = { 0xFFu, 0xFFu },
    .halls_left   = { {0xFFu,0xFFu}, {0xFFu,0xFFu}, {0xFFu,0xFFu} },
    .halls_right  = { {0xFFu,0xFFu}, {0xFFu,0xFFu}, {0xFFu,0xFFu} },
    .phases_left  = { 0u, {0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu},
                          {0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu}, 0u },
    .phases_right = { 0u, {0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu},
                          {0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu}, 0u },
    .led_red      = { 0xFFu, 0xFFu },
    .led_green    = { 0xFFu, 0xFFu },
    .led_orange   = { 0xFFu, 0xFFu },
    .led_blue     = { 0xFFu, 0xFFu },
    .buzzer       = { 0xFFu, 0xFFu },
    .button       = { 0xFFu, 0xFFu },
    .charger      = { 0xFFu, 0xFFu },
    .battery_voltage     = { 0, 0, {0xFFu,0xFFu}, 0.0f, 0.0f },
    .dc_link_voltage     = { 0, 0, {0xFFu,0xFFu}, 0.0f, 0.0f },
    .left_phase_current  = { 0, 0, {0xFFu,0xFFu}, 0.0f, 0.0f },
    .right_phase_current = { 0, 0, {0xFFu,0xFFu}, 0.0f, 0.0f },
    .mcu_temp_channel = 0xFFu,
    .name = "Motor-Disabled Safe Default",
};

/* Second real profile for narrowing tests (same family, different part) */
static const board_profile_t k_test_profile_b = {
    .family      = BOARD_FAMILY_STM32F1,
    .part_hint   = "TEST_BOARD_B",
    .uid         = "",
    .fingerprint = 0,
    .motor_count = 1,
    .name        = "Test Board B",
    /* everything else sentinel / zero */
    .self_hold   = { 0xFFu, 0xFFu },
    .halls_left  = { {0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu} },
    .halls_right = { {0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu} },
    .phases_left = { 0u, {0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu},
                         {0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu}, 0u },
    .phases_right= { 0u, {0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu},
                         {0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu}, 0u },
    .led_red     = {0xFFu,0xFFu}, .led_green = {0xFFu,0xFFu},
    .led_orange  = {0xFFu,0xFFu}, .led_blue  = {0xFFu,0xFFu},
    .buzzer      = {0xFFu,0xFFu}, .button    = {0xFFu,0xFFu},
    .charger     = {0xFFu,0xFFu},
    .battery_voltage     = {0,0,{0xFFu,0xFFu},0.0f,0.0f},
    .dc_link_voltage     = {0,0,{0xFFu,0xFFu},0.0f,0.0f},
    .left_phase_current  = {0,0,{0xFFu,0xFFu},0.0f,0.0f},
    .right_phase_current = {0,0,{0xFFu,0xFFu},0.0f,0.0f},
    .mcu_temp_channel = 0xFFu,
};

/*
 * g_board_table:
 *   [0] = safe default (motor-disabled)   <- g_default_board_index = 0
 *   [1] = TEST_BOARD_A (family STM32F1)
 *   [2] = TEST_BOARD_B (family STM32F1)
 */
const board_profile_t g_board_table[] = {
    /* 0 */ /* safe default — copied from k_test_profile_safe */
    {
        .family      = BOARD_FAMILY_STM32F1,
        .part_hint   = "SAFE_DEFAULT",
        .uid         = "", .fingerprint = 0, .motor_count = 0,
        .self_hold   = {0xFFu,0xFFu},
        .halls_left  = {{0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu}},
        .halls_right = {{0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu}},
        .phases_left = {0u,{0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu},
                            {0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu},0u},
        .phases_right= {0u,{0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu},
                            {0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu},0u},
        .led_red={0xFFu,0xFFu},.led_green={0xFFu,0xFFu},
        .led_orange={0xFFu,0xFFu},.led_blue={0xFFu,0xFFu},
        .buzzer={0xFFu,0xFFu},.button={0xFFu,0xFFu},.charger={0xFFu,0xFFu},
        .battery_voltage    ={0,0,{0xFFu,0xFFu},0.0f,0.0f},
        .dc_link_voltage    ={0,0,{0xFFu,0xFFu},0.0f,0.0f},
        .left_phase_current ={0,0,{0xFFu,0xFFu},0.0f,0.0f},
        .right_phase_current={0,0,{0xFFu,0xFFu},0.0f,0.0f},
        .mcu_temp_channel = 0xFFu,
        .name = "Motor-Disabled Safe Default",
    },
    /* 1 — TEST_BOARD_A */
    {
        .family      = BOARD_FAMILY_STM32F1,
        .part_hint   = "TEST_BOARD_A",
        .uid         = "", .fingerprint = 0, .motor_count = 2,
        .self_hold   = {4u,0u},
        .halls_left  = {{1u,5u},{1u,6u},{1u,7u}},
        .halls_right = {{2u,0u},{2u,1u},{2u,2u}},
        .phases_left = {1u,{0u,8u},{0xFFu,0xFFu},{0xFFu,0xFFu},
                            {0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu},0u},
        .phases_right= {0u,{0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu},
                            {0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu},0u},
        .led_green   = {3u,0u},
        .led_red={0xFFu,0xFFu},.led_orange={0xFFu,0xFFu},.led_blue={0xFFu,0xFFu},
        .buzzer={0xFFu,0xFFu},.button={0xFFu,0xFFu},.charger={0xFFu,0xFFu},
        .battery_voltage    ={0,0,{0xFFu,0xFFu},0.0f,0.0f},
        .dc_link_voltage    ={0,0,{0xFFu,0xFFu},0.0f,0.0f},
        .left_phase_current ={0,0,{0xFFu,0xFFu},0.0f,0.0f},
        .right_phase_current={0,0,{0xFFu,0xFFu},0.0f,0.0f},
        .mcu_temp_channel = 0xFFu,
        .name = "Test Board A",
    },
    /* 2 — TEST_BOARD_B */
    {
        .family      = BOARD_FAMILY_STM32F1,
        .part_hint   = "TEST_BOARD_B",
        .uid         = "", .fingerprint = 0, .motor_count = 1,
        .self_hold   = {0xFFu,0xFFu},
        .halls_left  = {{0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu}},
        .halls_right = {{0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu}},
        .phases_left = {0u,{0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu},
                            {0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu},0u},
        .phases_right= {0u,{0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu},
                            {0xFFu,0xFFu},{0xFFu,0xFFu},{0xFFu,0xFFu},0u},
        .led_red={0xFFu,0xFFu},.led_green={0xFFu,0xFFu},
        .led_orange={0xFFu,0xFFu},.led_blue={0xFFu,0xFFu},
        .buzzer={0xFFu,0xFFu},.button={0xFFu,0xFFu},.charger={0xFFu,0xFFu},
        .battery_voltage    ={0,0,{0xFFu,0xFFu},0.0f,0.0f},
        .dc_link_voltage    ={0,0,{0xFFu,0xFFu},0.0f,0.0f},
        .left_phase_current ={0,0,{0xFFu,0xFFu},0.0f,0.0f},
        .right_phase_current={0,0,{0xFFu,0xFFu},0.0f,0.0f},
        .mcu_temp_channel = 0xFFu,
        .name = "Test Board B",
    },
};
const uint16_t g_board_table_count  = 3u;
const uint16_t g_default_board_index = 0u;   /* motor-disabled safe row */

/* -------------------------------------------------------------------------
 * FlashContent instance + writeFlash stub.
 * ---------------------------------------------------------------------- */
FLASH_CONTENT FlashContent;
const FLASH_CONTENT FlashDefaults; /* zero-init default; unused in tests */

/* Stub: record that a write was requested but do nothing */
static int s_flash_write_called = 0;
int writeFlash(unsigned char *data, int len) {
    (void)data; (void)len;
    s_flash_write_called++;
    return 1;
}
/* Other flashaccess stubs (not called by board_override/board_select) */
int flashposn(int *len)                               { (void)len; return 0; }
int readFlash(unsigned char *data, int len)           { (void)data; (void)len; return 0; }
unsigned short readFlash16(volatile unsigned short *d, uint16_t l) { (void)d;(void)l;return 0u; }
unsigned short writeFlash16(volatile unsigned short *d, uint16_t l){ (void)d;(void)l;return 0u; }

/* -------------------------------------------------------------------------
 * Helper: reset global test state before each test.
 * Copies k_test_profile_active into ACTIVE (declared in board_select.c) and
 * resets FlashContent to zero-override state.
 * ---------------------------------------------------------------------- */
#include "board_active.h"   /* declares extern board_profile_t ACTIVE */

static inline void fixture_reset(void)
{
    memcpy(&ACTIVE, &k_test_profile_active, sizeof(ACTIVE));
    memset(&FlashContent, 0, sizeof(FlashContent));
    hal_gpio_init_count = 0;
    hal_gpio_init_last_port = (GPIO_TypeDef *)0;
    memset(&hal_gpio_init_last_init, 0, sizeof(hal_gpio_init_last_init));
    s_flash_write_called = 0;
    /* Clear any board_select forced state */
    board_select_force(0xFFFFu);
}
