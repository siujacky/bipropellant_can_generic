/*
 * main_smoke.c — Minimal smoke entry point for non-STM32F1 family builds.
 *
 * Motor-disabled smoke only.  This file provides the main() entry point for
 * the GD32F1, GD32E2, and MM32SPIN0X smoke builds (env:gd32f1, env:gd32e2,
 * env:mm32spin0x).  It:
 *
 *   1. Calls board_select_init() to resolve the ACTIVE board profile from the
 *      family-specific board_table (GD32F1/GD32E2/MM32SPIN0X rows only).
 *   2. Calls hal_motor_init() — which is a no-op stub for these families
 *      (MOTOR-DISABLED), confirming the interface compiles and links.
 *   3. Loops forever (self-hold equivalent for smoke builds without GPIO init).
 *
 * NOT used for the STM32F1 build (env:stm32f1 uses src/main.c, which links
 * the full firmware).
 *
 * Motor-disabled smoke only — this is a build-validation entry point, NOT a
 * production firmware.  The full main.c with CAN, BLDC, and peripheral init
 * must be adapted per family before deploying to real hardware.
 */

#include "board_active.h"    /* board_select_init(), ACTIVE */
#include "hal_motor.h"       /* hal_motor_init() stub */

/* Minimal self-hold: a volatile to prevent the compiler optimising away
 * the infinite loop. */
static volatile int s_running = 1;

int main(void)
{
    /* Step 1: Resolve the active board profile (motor-disabled safe default
     * on a fresh/unconfigured board; stored profile if NVRAM is set). */
    board_select_init();

    /* Step 2: Motor init — motor-disabled stub for GD32F1/GD32E2/MM32SPIN0X.
     * This is a no-op that confirms the hal_motor interface links correctly. */
    hal_motor_init();

    /* Step 3: Spin forever. In production, this would be replaced by the
     * main loop (CAN processing, BLDC control, etc.) once the family-specific
     * peripheral init has been bench-validated. */
    while (s_running) {
        /* Idle — waiting for interrupt-driven events once peripheral init
         * is complete. For the smoke build, this is the final state. */
    }

    return 0;  /* unreachable; suppresses compiler warning */
}

/* Minimal flashaccess stubs for the smoke build.
 * The full flashaccess.c from the STM32F1 firmware uses STM32 HAL flash APIs
 * which are not available on GD32/MM32 without a compat shim.  These no-op
 * stubs satisfy the linker so board_override.c compiles cleanly.
 *
 * TODO (per-family flash enablement):
 *   Replace these stubs with family-specific flash read/write implementations
 *   once the per-family HAL (or register-level flash driver) is available.
 */
int  flashposn(int *len)                               { (void)len; return 0; }
int  readFlash(unsigned char *data, int len)           { (void)data; (void)len; return 0; }
int  writeFlash(unsigned char *data, int len)          { (void)data; (void)len; return 0; }
unsigned short readFlash16(volatile unsigned short *d, unsigned short l)
    { (void)d; (void)l; return 0u; }
unsigned short writeFlash16(volatile unsigned short *d, unsigned short l)
    { (void)d; (void)l; return 0u; }

/* FlashContent + FlashDefaults: zero-initialised.  The board_select and
 * board_override modules reference these; on a fresh smoke build they just
 * fall through to the motor-disabled safe default. */
#include "flashcontent.h"
FLASH_CONTENT FlashContent;
const FLASH_CONTENT FlashDefaults;
