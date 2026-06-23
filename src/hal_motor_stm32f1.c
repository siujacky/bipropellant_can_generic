/*
 * hal_motor_stm32f1.c — STM32F1 motor topology implementation (M5 scaffold).
 *
 * Implements hal_motor_init() and hal_motor_stop() for the STM32F103 target.
 *
 * The STM32F1 motor topology is the verified baseline dual-advanced-timer path:
 *   - TIM8  (RIGHT_TIM / htim_right) = master timer, TIM_TRGO_ENABLE.
 *   - TIM1  (LEFT_TIM  / htim_left)  = slave-gated by TIM8 TRGO (ITR0).
 *   - ADC1 external trigger = ADC_EXTERNALTRIGCONV_T8_TRGO.
 *   - Dual ADC1/ADC2 simultaneous regular conversion.
 *
 * hal_motor_init() delegates to the existing MX_TIM_Init() + MX_ADC1_Init()
 * + MX_ADC2_Init() functions in setup.c.  This keeps the STM32F1 path
 * byte-for-byte equivalent to the baseline while exposing it through the
 * family-agnostic hal_motor.h interface.
 *
 * hal_motor_stop() clears the MOE (Main Output Enable) bit on both timers,
 * immediately tristating all phase PWM outputs.  This mirrors the existing
 * BLDC_controller stop path.
 *
 * This file is compiled ONLY when BOARD_FAMILY_STM32F1 is defined (see the
 * env:stm32f1 section in Makefile / platformio.ini).
 */

#include "hal_motor.h"
#include "setup.h"
#include "stm32f1xx_hal.h"
#include "defines.h"

/* htim_right (TIM8 = master) and htim_left (TIM1 = slave) are defined in
 * setup.c and used throughout the codebase. */
extern TIM_HandleTypeDef htim_right;
extern TIM_HandleTypeDef htim_left;

void hal_motor_init(void)
{
    /* Delegate to the existing verified init sequence in setup.c.
     * MX_TIM_Init() sets up both TIM8 (master) and TIM1 (slave-gated),
     * configures complementary PWM channels, dead-time, and starts the timers
     * with MOE disabled (the BLDC controller enables MOE when armed). */
    MX_TIM_Init();

    /* ADC init: dual-mode, triggered by TIM8_TRGO. */
    MX_ADC1_Init();
    MX_ADC2_Init();
}

void hal_motor_stop(void)
{
    /* Clear MOE on both timers: tristates all phase outputs immediately.
     * Safe to call even if the timers were never started (MOE is already 0). */
    if (htim_right.Instance) {
        htim_right.Instance->BDTR &= ~TIM_BDTR_MOE;
    }
    if (htim_left.Instance) {
        htim_left.Instance->BDTR &= ~TIM_BDTR_MOE;
    }
}
