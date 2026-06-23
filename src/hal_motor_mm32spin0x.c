/*
 * hal_motor_mm32spin0x.c — MM32SPIN0X motor topology STUB (M5 scaffold, motor-disabled).
 *
 * The MM32SPIN0x (Cortex-M0) is not register-compatible with the ST/GD advanced-
 * timer + ADC-trigger paths.  It uses the MM32 SPIN0x peripheral set, which
 * requires the MM32 SPIN0x firmware library or equivalent register definitions.
 *
 * Motor drive for MM32SPIN0X is rated HIGHEST RISK / EXPERIMENTAL because:
 *   - The register layout and timer names differ from both STM32F1 and GD32.
 *   - There is currently only ONE MM32SPIN0X board in the profile set (single
 *     data point; no cross-board validation possible).
 *   - The ADC trigger + PWM sync topology has not been verified on MM32 silicon.
 *
 * This is a MOTOR-DISABLED stub.  Ship motor-disabled first; enable PWM only
 * after bench validation on a current-limited supply.
 *
 * Motor-disabled smoke only — clock tree and peripheral init must be verified
 * on hardware before enabling motor drive for the MM32SPIN0X family.
 */
#include "hal_motor.h"

#warning "Motor drive not yet implemented for MM32SPIN0X — bench-validate before enabling"

void hal_motor_init(void)
{
    /* MOTOR-DISABLED STUB.
     * MM32SPIN0x motor drive has not been bench-validated.
     * Do not initialise any phase-PWM peripheral here.
     *
     * TODO (MM32SPIN0X motor enablement checklist):
     *   1. Obtain MM32 SPIN0x Reference Manual and locate the advanced timer
     *      (TIM1 or ATIM equivalent) and its ADC trigger capability.
     *   2. Implement MM32-specific timer + ADC init (register names differ from
     *      ST/GD; do NOT assume STM32 HAL macros are valid here).
     *   3. Verify dead-time support and range on MM32 SPIN0x silicon.
     *   4. Bench-test on a current-limited supply before claiming motor drive.
     *   5. Remove this stub and the #warning above.
     */
}

void hal_motor_stop(void)
{
    /* MOTOR-DISABLED STUB — motors were never started, nothing to stop. */
}
