/*
 * hal_motor.h — motor topology abstraction (M5 scaffold).
 *
 * Declares the family-agnostic motor HAL interface.  Each supported family
 * provides its own implementation in src/hal_motor_<family>.c:
 *
 *   STM32F1   src/hal_motor_stm32f1.c   — dual-timer (TIM1 master / TIM8 slave)
 *                                          Full implementation (calls existing
 *                                          MX_TIM8_Init / MX_TIM1_Init from setup.c).
 *
 *   GD32F1    src/hal_motor_gd32f1.c    — STUB (motor-disabled).
 *   GD32E2    src/hal_motor_gd32e2.c    — STUB (motor-disabled).
 *   MM32SPIN0X src/hal_motor_mm32spin0x.c — STUB (motor-disabled).
 *
 * The stubs emit a #warning at compile time so build logs make the
 * motor-disabled status explicit.  They link cleanly and produce a valid
 * smoke .bin that boots, initialises CAN, and runs the board_select/override
 * machinery, but does NOT touch any phase-PWM peripheral.
 *
 * Motor-disabled smoke only — clock tree and peripheral init must be verified
 * on hardware before enabling motor drive on any non-STM32F1 family.
 */
#pragma once

/* ---------------------------------------------------------------------------
 * hal_motor_init — initialise the motor drive peripheral(s).
 *
 * Called from setup.c after board_select_init() resolves ACTIVE.
 *
 * STM32F1: sets up TIM1 (master) and TIM8 (slave-gated) for complementary
 *   PWM with dead-time, enables the ADC trigger, and enables the timer outputs.
 *   This is the verified baseline topology (TIM1_TRGO → TIM8 gate →
 *   ADC1/ADC2 dual sampling via T8_TRGO).
 *
 * GD32F1 / GD32E2 / MM32SPIN0X: no-op stub.  The motor drive peripheral
 *   init for single-advanced-timer families requires verification of the
 *   ADC trigger source (TIMER0_TRGO or equivalent) and ADC dual/single
 *   sampling on real hardware before it can be implemented correctly.
 * ------------------------------------------------------------------------- */
void hal_motor_init(void);

/* ---------------------------------------------------------------------------
 * hal_motor_stop — immediately disable motor PWM outputs (all phases low).
 *
 * STM32F1: disables TIM1 and TIM8 outputs (MOE clear on both).
 *
 * GD32F1 / GD32E2 / MM32SPIN0X: no-op stub (motors never started).
 * ------------------------------------------------------------------------- */
void hal_motor_stop(void);
