/*
 * hal_motor_gd32e2.c — GD32E2 motor topology STUB (M5 scaffold, motor-disabled).
 *
 * The GD32E230 (ChipFamily GD32E2) is a Cortex-M23 part with a single advanced
 * timer and a different register layout from the STM32F1 baseline.
 * Enabling motor drive requires:
 *   - Verification of the GD32E230 advanced timer register map (vs GD32F1x0).
 *   - Confirmation of the ADC trigger source available from the advanced timer.
 *   - A full dead-time calibration and ADC alignment bench test on GD32E230
 *     silicon before any phase PWM is enabled.
 *
 * This is a MOTOR-DISABLED stub.  The firmware boots, initialises CAN, and
 * runs board_select / board_override, but no phase PWM outputs are toggled.
 *
 * Motor-disabled smoke only — clock tree and peripheral init must be verified
 * on hardware before enabling motor drive for the GD32E2 family.
 */
#include "hal_motor.h"

#warning "Motor drive not yet implemented for GD32E2 (GD32E230) — bench-validate before enabling"

void hal_motor_init(void)
{
    /* MOTOR-DISABLED STUB.
     * GD32E230 (Cortex-M23) motor drive has not been bench-validated.
     * Do not initialise any phase-PWM peripheral here.
     *
     * TODO (GD32E2 motor enablement checklist):
     *   1. Confirm advanced timer base address and register layout for GD32E230.
     *   2. Verify ADC trigger source from the advanced timer (TIMER0_TRGO or
     *      equivalent on GD32E23x).
     *   3. Determine ADC single vs dual sampling capability.
     *   4. Implement Cortex-M23-compatible timer + ADC init sequence.
     *   5. Bench-test on a current-limited supply before claiming motor drive.
     *   6. Remove this stub and the #warning above.
     */
}

void hal_motor_stop(void)
{
    /* MOTOR-DISABLED STUB — motors were never started, nothing to stop. */
}
