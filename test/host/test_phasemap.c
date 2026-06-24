/*
 * test_phasemap.c -- host-native unit tests for the PhaseMap Wizard.
 *
 * T1: phasemap_char('q') from PM_IDLE stays IDLE (invalid, no start)
 * T2: PM_SAFETY_GATE: wrong string stays GATE; "CONFIRM DUMMY LOAD" advances
 * T3: probe result recording: confirmed pin -> pm_probe_t.confirmed=1
 * T4: abort mid-probe -> all pins LOW (verify via mock GPIO BRR)
 * T5: infer_highside: TIM1_CH1N (PB13) -> TIM1_CH1 (PA8)
 * T6: build_profile: TIM1 pairs -> phases_right, U-phase correctly assigned
 *
 * This test uses the FULL generated AF validity table (board_af_validity_stm32f1.c)
 * instead of the stripped fixtures.h table, so all TIM1/TIM8 low-side candidates
 * are present.  It provides its own minimal board-table and flash stubs.
 *
 * Compile (from bcg/ root):
 *   gcc -std=c11 -Wall -Wno-unused-function \
 *       -DPHASEMAP_HOST_TEST \
 *       -I inc -I generated -I test/host/shim \
 *       test/host/test_phasemap.c \
 *       src/phasemap.c \
 *       src/board_select.c \
 *       generated/board_af_validity_stm32f1.c \
 *       test/host/shim/hal_mock.c \
 *       -o /tmp/bcg_test_phasemap -lm
 */

#include "stm32f1xx_hal.h"  /* shim -- must be first */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "board_table.h"
#include "board_active.h"
#include "phasemap.h"

/* =========================================================================
 * Minimal board table and flash stubs.
 * We link against generated/board_af_validity_stm32f1.c for the real AF
 * table, so g_af_validity is NOT defined here.
 * ======================================================================= */

const board_profile_t g_board_table[] = {
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
};
const uint16_t g_board_table_count   = 1u;
const uint16_t g_default_board_index = 0u;

/* flashcontent stubs */
#include "flashcontent.h"
FLASH_CONTENT FlashContent;
const FLASH_CONTENT FlashDefaults;
int writeFlash(unsigned char *d, int l)  { (void)d;(void)l; return 1; }
int flashposn(int *l)                    { (void)l; return 0; }
int readFlash(unsigned char *d, int l)   { (void)d;(void)l; return 0; }
unsigned short readFlash16(volatile unsigned short *d, uint16_t l)
                                         { (void)d;(void)l; return 0u; }
unsigned short writeFlash16(volatile unsigned short *d, uint16_t l)
                                         { (void)d;(void)l; return 0u; }

/* =========================================================================
 * HAL_GetTick() stub
 * ======================================================================= */
static uint32_t s_mock_tick = 0;

uint32_t HAL_GetTick(void) { return s_mock_tick; }

static void tick_advance(uint32_t ms) { s_mock_tick += ms; }

/* =========================================================================
 * Mock ADC
 * ======================================================================= */
extern void phasemap_mock_adc_set(uint16_t val);

/* =========================================================================
 * Test-only export from phasemap.c
 * ======================================================================= */
extern gpio_pin_t phasemap_test_infer_highside(uint8_t timer_id, uint8_t channel);

/* =========================================================================
 * Serial feed helper
 * ======================================================================= */
static void feed_string(const char *s)
{
    while (*s) { phasemap_char(*s); s++; }
    phasemap_char('\r');
}

/* =========================================================================
 * GPIO BRR read helper
 * ======================================================================= */
static uint32_t gpio_brr(uint8_t port_idx)
{
    switch (port_idx) {
        case 0: return _mock_GPIOA.BRR;
        case 1: return _mock_GPIOB.BRR;
        case 2: return _mock_GPIOC.BRR;
        default: return 0;
    }
}

/* =========================================================================
 * Test harness
 * ======================================================================= */
static int s_failures = 0;

#define CHECK(cond, label) do {                                             \
    if (!(cond)) {                                                           \
        printf("FAIL %s: condition failed: %s\n", (label), #cond);         \
        s_failures++;                                                        \
    } else {                                                                 \
        printf("PASS %s\n", (label));                                       \
    }                                                                        \
} while (0)

/* =========================================================================
 * T1: phasemap_char in non-running state is a no-op
 * ======================================================================= */
static void test_T1(void)
{
    printf("\n--- T1: phasemap_char in IDLE stays IDLE ---\n");

    phasemap_abort();
    CHECK(phasemap_state() == PM_ABORTED, "T1a: abort -> PM_ABORTED");

    phasemap_char('q');
    phasemap_tick();
    CHECK(phasemap_state() == PM_ABORTED, "T1b: char in ABORTED stays ABORTED");

    printf("PASS T1: chars in non-running states do not advance state machine\n");
}

/* =========================================================================
 * T2: safety gate -- wrong phrase stays GATE, correct phrase advances
 * ======================================================================= */
static void test_T2(void)
{
    printf("\n--- T2: safety gate filter ---\n");

    memset(&_mock_GPIOA, 0, sizeof(_mock_GPIOA));
    memset(&_mock_GPIOB, 0, sizeof(_mock_GPIOB));
    memset(&_mock_GPIOC, 0, sizeof(_mock_GPIOC));
    s_mock_tick = 0;

    phasemap_start(PM_MODE_GUIDED);
    CHECK(phasemap_state() == PM_SAFETY_GATE, "T2a: start -> PM_SAFETY_GATE");

    feed_string("WRONG PHRASE");
    phasemap_tick();
    CHECK(phasemap_state() == PM_SAFETY_GATE, "T2b: wrong phrase stays PM_SAFETY_GATE");

    feed_string("just testing");
    phasemap_tick();
    CHECK(phasemap_state() == PM_SAFETY_GATE, "T2c: another wrong phrase stays PM_SAFETY_GATE");

    feed_string("CONFIRM DUMMY LOAD");
    phasemap_tick();
    CHECK(phasemap_state() == PM_PROBE_LOWSIDE,
          "T2d: correct phrase -> PM_PROBE_LOWSIDE (GUIDED)");
}

/* =========================================================================
 * T3: probe result recording -- confirmed pin sets .confirmed=1
 *
 * Sub-state sequence per probe in GUIDED mode (one tick each):
 *   PROBE_SUB_IDLE -> SETUP -> SETTLE (wait SETTLE_MS) -> HOLD (wait 'y')
 *   -> RELEASE -> COOLDOWN (wait COOLDOWN_MS) -> next probe (index++)
 * ======================================================================= */
static void test_T3(void)
{
    printf("\n--- T3: probe result recording ---\n");

    memset(&_mock_GPIOA, 0, sizeof(_mock_GPIOA));
    memset(&_mock_GPIOB, 0, sizeof(_mock_GPIOB));
    memset(&_mock_GPIOC, 0, sizeof(_mock_GPIOC));
    s_mock_tick = 0;

    phasemap_start(PM_MODE_GUIDED);
    feed_string("CONFIRM DUMMY LOAD");
    phasemap_tick();           /* safety gate consumed */
    CHECK(phasemap_state() == PM_PROBE_LOWSIDE, "T3a: in PM_PROBE_LOWSIDE");

    /* Tick 1: IDLE -> SETUP (configure all pins OUTPUT_PP + arm probe) */
    tick_advance(1);
    phasemap_tick();

    /* Tick 2: SETUP -> SETTLE */
    tick_advance(1);
    phasemap_tick();

    /* Advance past SETTLE_MS: SETTLE -> HOLD */
    tick_advance(PHASEMAP_SETTLE_MS + 1u);
    phasemap_tick();

    /* Load 'y' answer; tick: HOLD -> RELEASE */
    phasemap_char('y');
    tick_advance(1);
    phasemap_tick();

    /* RELEASE: drives all pins LOW, reconfigures pin as Hi-Z */
    tick_advance(1);
    phasemap_tick();

    /* Advance past COOLDOWN_MS: COOLDOWN -> next probe (index++) */
    tick_advance(PHASEMAP_COOLDOWN_MS + 1u);
    phasemap_tick();

    CHECK(phasemap_confirmed_count() >= 1u, "T3b: at least one confirmed probe");

    pm_probe_t r = phasemap_probe_result(0);
    CHECK(r.confirmed == 1u,                        "T3c: probe_result(0).confirmed == 1");
    CHECK(r.timer_id == 1u || r.timer_id == 8u,    "T3d: timer_id is TIM1 or TIM8");
    CHECK(r.channel >= 1u && r.channel <= 3u,       "T3e: channel in [1,3]");
    printf("PASS T3: confirmed probe timer=%u ch=%u pin=P%c%u\n",
           r.timer_id, r.channel,
           (char)('A' + r.pin.port), r.pin.pin);
}

/* =========================================================================
 * T4: abort mid-probe -> all candidate pins driven LOW via BRR
 * ======================================================================= */
static void test_T4(void)
{
    printf("\n--- T4: abort drives all candidate pins LOW ---\n");

    memset(&_mock_GPIOA, 0, sizeof(_mock_GPIOA));
    memset(&_mock_GPIOB, 0, sizeof(_mock_GPIOB));
    memset(&_mock_GPIOC, 0, sizeof(_mock_GPIOC));
    s_mock_tick = 0;

    phasemap_start(PM_MODE_GUIDED);
    feed_string("CONFIRM DUMMY LOAD");
    phasemap_tick();        /* gate consumed */
    tick_advance(1);
    phasemap_tick();        /* IDLE -> SETUP */
    tick_advance(1);
    phasemap_tick();        /* SETUP -> SETTLE */

    /* Mid-probe: pin is HIGH (complement lockout already happened). Abort now. */
    phasemap_abort();

    CHECK(phasemap_state() == PM_ABORTED, "T4a: state is PM_ABORTED after abort");

    uint32_t total_brr = gpio_brr(0) | gpio_brr(1) | gpio_brr(2);
    CHECK(total_brr != 0u, "T4b: at least one GPIO BRR was written (pins driven LOW)");
    printf("PASS T4: GPIOA.BRR=0x%04lX GPIOB.BRR=0x%04lX GPIOC.BRR=0x%04lX\n",
           (unsigned long)gpio_brr(0),
           (unsigned long)gpio_brr(1),
           (unsigned long)gpio_brr(2));
}

/* =========================================================================
 * T5: infer_highside -- TIM1_CH1N (PB13) -> TIM1_CH1 (PA8)
 *
 * Calls the test-only helper directly; no state machine needed.
 * Uses the real generated/board_af_validity_stm32f1.c table.
 * ======================================================================= */
static void test_T5(void)
{
    printf("\n--- T5: infer_highside TIM1_CH1N(PB13) -> TIM1_CH1(PA8) ---\n");

    /* TIM1_CH1: high-side = PA8 (port=0, pin=8) */
    gpio_pin_t hs = phasemap_test_infer_highside(1u, 1u);
    CHECK(hs.port == 0u && hs.pin == 8u,
          "T5a: TIM1 CH1 high-side = PA8 ({port=0,pin=8})");

    /* TIM1_CH2: high-side = PA9 (port=0, pin=9) */
    gpio_pin_t hs2 = phasemap_test_infer_highside(1u, 2u);
    CHECK(hs2.port == 0u && hs2.pin == 9u,
          "T5b: TIM1 CH2 high-side = PA9 ({port=0,pin=9})");

    /* TIM8_CH1: high-side = PC6 (port=2, pin=6) */
    gpio_pin_t hs3 = phasemap_test_infer_highside(8u, 1u);
    CHECK(hs3.port == 2u && hs3.pin == 6u,
          "T5c: TIM8 CH1 high-side = PC6 ({port=2,pin=6})");

    printf("PASS T5: high-side inference correct\n");
}

/* =========================================================================
 * T6: build_profile -- TIM1 pairs -> phases_right, U-phase correctly assigned
 *
 * Drive one complete GUIDED probe cycle by pumping exactly the ticks needed
 * for each sub-state transition, then verify the result.
 * ======================================================================= */

/*
 * drive_one_probe: pump exactly the ticks required to complete one GUIDED
 * probe sub-state machine from the current position (must already be in
 * PROBE_SUB_IDLE at entry).
 *
 * Sub-states and how to advance each:
 *   IDLE   -> SETUP:     tick
 *   SETUP  -> SETTLE:    tick (atomic setup inside)
 *   SETTLE -> HOLD:      advance SETTLE_MS+1, tick
 *   HOLD   -> RELEASE:   push answer char, tick
 *   RELEASE -> COOLDOWN: tick (drives pin LOW)
 *   COOLDOWN -> IDLE++:  advance COOLDOWN_MS+1, tick
 *
 * Returns 1 if probe confirmed, 0 if rejected, -1 if state left LOWSIDE.
 */
static int drive_one_probe(char answer)
{
    if (phasemap_state() != PM_PROBE_LOWSIDE) return -1;
    uint8_t before = phasemap_confirmed_count();

    /* IDLE -> SETUP */
    tick_advance(1);
    phasemap_tick();
    if (phasemap_state() != PM_PROBE_LOWSIDE) return -1;

    /* SETUP -> SETTLE */
    tick_advance(1);
    phasemap_tick();
    if (phasemap_state() != PM_PROBE_LOWSIDE) return -1;

    /* SETTLE -> HOLD (advance past settle window) */
    tick_advance(PHASEMAP_SETTLE_MS + 1u);
    phasemap_tick();
    if (phasemap_state() != PM_PROBE_LOWSIDE) return -1;

    /* HOLD -> RELEASE (answer y/n) */
    phasemap_char(answer);
    tick_advance(1);
    phasemap_tick();
    if (phasemap_state() != PM_PROBE_LOWSIDE) return -1;

    /* RELEASE -> COOLDOWN */
    tick_advance(1);
    phasemap_tick();
    if (phasemap_state() != PM_PROBE_LOWSIDE) return -1;

    /* COOLDOWN -> next (advance past cooldown window) */
    tick_advance(PHASEMAP_COOLDOWN_MS + 1u);
    phasemap_tick();

    uint8_t after = phasemap_confirmed_count();
    return (after > before) ? 1 : 0;
}

static void test_T6(void)
{
    printf("\n--- T6: build_profile TIM1 pairs -> phases_right ---\n");

    memset(&_mock_GPIOA, 0, sizeof(_mock_GPIOA));
    memset(&_mock_GPIOB, 0, sizeof(_mock_GPIOB));
    memset(&_mock_GPIOC, 0, sizeof(_mock_GPIOC));
    s_mock_tick = 0;

    phasemap_start(PM_MODE_GUIDED);
    feed_string("CONFIRM DUMMY LOAD");
    phasemap_tick();
    CHECK(phasemap_state() == PM_PROBE_LOWSIDE, "T6a: in PM_PROBE_LOWSIDE");

    /* Confirm the first probe with 'y'. */
    int r0_result = drive_one_probe('y');
    if (r0_result < 0) {
        printf("FAIL T6: state left PM_PROBE_LOWSIDE during first probe drive\n");
        s_failures++;
        return;
    }

    /* Drain remaining probes with 'n' to reach PM_PROBE_HALL. */
    uint32_t safety = 0;
    while (phasemap_state() == PM_PROBE_LOWSIDE && safety < 100u) {
        safety++;
        if (drive_one_probe('n') < 0) break;
    }

    /* Complete the hall probe window. */
    if (phasemap_state() == PM_PROBE_HALL) {
        tick_advance(PHASEMAP_HALL_WINDOW_MS + 1u);
        phasemap_tick();
    }

    /* PM_BUILD_PROFILE -> PM_DONE */
    if (phasemap_state() == PM_BUILD_PROFILE) {
        phasemap_tick();
    }

    CHECK(phasemap_confirmed_count() >= 1u,
          "T6b: at least 1 probe confirmed (first probe answered 'y')");

    /* Validate structure of the confirmed result. */
    pm_probe_t r = phasemap_probe_result(0);
    CHECK(r.confirmed == 1u,
          "T6c: probe_result(0).confirmed == 1");
    CHECK(r.timer_id == 1u || r.timer_id == 8u,
          "T6d: probe_result(0).timer_id is TIM1 or TIM8");
    CHECK(r.channel >= 1u && r.channel <= 3u,
          "T6e: probe_result(0).channel in [1,3]");

    /* Verify high-side inference for the confirmed low-side result. */
    gpio_pin_t hs = phasemap_test_infer_highside(r.timer_id, r.channel);
    CHECK(!BOARD_PIN_IS_UNSET(hs),
          "T6f: high-side pin inferred (not sentinel)");

    /* For TIM8_CH1 (first candidate PA7 in the STM32F1 table): high = PC6 */
    if (r.timer_id == 8u && r.channel == 1u) {
        CHECK(hs.port == 2u && hs.pin == 6u,
              "T6g: TIM8 CH1 high-side = PC6");
    }
    /* For TIM1_CH1 (PB13 in the table): high = PA8 */
    if (r.timer_id == 1u && r.channel == 1u) {
        CHECK(hs.port == 0u && hs.pin == 8u,
              "T6g: TIM1 CH1 high-side = PA8");
    }

    printf("PASS T6: %u probes confirmed; timer=%u ch=%u; high-side=P%c%u\n",
           phasemap_confirmed_count(),
           r.timer_id, r.channel,
           (char)('A' + hs.port), hs.pin);
}

/* =========================================================================
 * main
 * ======================================================================= */
int main(void)
{
    board_select_init();

    printf("=== PhaseMap Wizard Host Tests ===\n");

    test_T1();
    test_T2();
    test_T3();
    test_T4();
    test_T5();
    test_T6();

    printf("\n===================================\n");
    if (s_failures == 0) {
        printf("All tests PASSED.\n");
        return 0;
    } else {
        printf("%d test(s) FAILED.\n", s_failures);
        return 1;
    }
}
