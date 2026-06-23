/*
 * hal_motor_gd32f1.c — GD32F1 motor topology STUB (M5 scaffold, motor-disabled).
 *
 * The GD32F130 (ChipFamily GD32F1) has a single advanced timer (TIMER0 = TIM1
 * alias) and a different ADC trigger topology from the STM32F1 dual-timer path.
 * The correct single-advanced-timer motor drive sequence for GD32F130 requires:
 *   - Verification that TIMER0_TRGO can be used as the ADC external trigger.
 *   - Determination of whether the GD32F130 supports simultaneous dual-ADC
 *     sampling (the GD32F1x0 family MAY have only a single ADC unit).
 *   - A full ADC trigger alignment bench test on real GD32F130 silicon BEFORE
 *     any phase PWM is enabled.
 *
 * Until that bench validation is complete, this file provides a MOTOR-DISABLED
 * stub: hal_motor_init() and hal_motor_stop() are intentional no-ops.
 * The firmware boots, initialises CAN, and runs the board_select / board_override
 * machinery, but NO phase PWM outputs are toggled.
 *
 * Motor-disabled smoke only — clock tree and peripheral init must be verified
 * on hardware before enabling motor drive for the GD32F1 family.
 */
#include "hal_motor.h"

#warning "Motor drive not yet implemented for GD32F1 (GD32F130) — bench-validate before enabling"

void hal_motor_init(void)
{
    /* MOTOR-DISABLED STUB.
     * GD32F1 (GD32F130) single-advanced-timer + ADC-trigger topology has not
     * been bench-validated.  Do not initialise any phase-PWM peripheral here.
     *
     * TODO (GD32F1 motor enablement checklist):
     *   1. Confirm TIMER0_TRGO can trigger ADC on GD32F130 (check RM table).
     *   2. Determine single vs dual ADC sampling path for phase currents.
     *   3. Implement TIMER0 init (complementary PWM, dead-time, slave-mode if
     *      single-motor; no master/slave gating needed unlike STM32F1).
     *   4. Bench-test on a current-limited supply before claiming motor drive.
     *   5. Remove this stub and the #warning above.
     */
}

void hal_motor_stop(void)
{
    /* MOTOR-DISABLED STUB — motors were never started, nothing to stop. */
}
