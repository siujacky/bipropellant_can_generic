/* hal_motor_stm32g4.c — Motor topology for STM32G474/G484 (G4 Cat.3).
 *
 * The G474 has TIM1 AND TIM8, exactly like the STM32F103 hoverboard controller,
 * so dual-motor complementary PWM works with the SAME pin mapping:
 *
 *   Right motor (TIM1): PA8=CH1,  PA9=CH2,  PA10=CH3   high-side
 *                       PB13=CH1N, PB14=CH2N, PB15=CH3N low-side
 *   Left motor  (TIM8): PC6=CH1,  PC7=CH2,  PC8=CH3    high-side
 *                       PA7=CH1N,  PB0=CH2N,  PB1=CH3N  low-side
 *
 * Additional G4 advantages over F103 (all require HAL + G4 HAL driver):
 *   - 170 MHz (vs 72 MHz): faster loop rate, better ADC oversampling
 *   - HRTIM: 184 ps dead-time resolution for FOC (swap timer if needed)
 *   - 3x OpAmps: on-chip current sensing without external ICs
 *   - Native FDCAN: no MCP2515 needed — use fdcan_bus.c instead of mcp2515.c
 *     (CAN FD protocol, up to 8 Mbit/s; backwards compatible at 500 kbps)
 *   - USB FS with HSI48: crystal-free USB, driver-free CDC on Windows 10/11
 *
 * STATUS: motor-disabled stub.
 * This file is a SCAFFOLD placeholder — the topology comment and interface
 * are correct; the actual TIM1/TIM8 PWM init for G4 HAL needs to be written
 * before enabling motors (G4 HAL API differs from F1 HAL for BDTR/dead-time).
 * Bench-validate PWM/ADC alignment on real G474 hardware before arming.
 *
 * Build env: -DBOARD_FAMILY_STM32G4 (add to Makefile env:stm32g4)
 */

#include "hal_motor.h"

#ifdef BOARD_FAMILY_STM32G4

#warning "hal_motor_stm32g4: DUAL-MOTOR scaffold (TIM1+TIM8 wiring correct). \
Wire PWM init to G4 HAL before enabling motors. Bench-validate on silicon."

void hal_motor_init(void)
{
    /* TODO: implement using stm32g4xx HAL:
     *   HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1/2/3)  right motor
     *   HAL_TIMEx_PWMN_Start(&htim8, TIM_CHANNEL_1/2/3)  left motor
     *
     * G4 BDTR dead-time: STM32G474 supports sub-nanosecond dead-time via HRTIM.
     * For TIM1/TIM8 standard mode, dead-time is configured identically to F103
     * except the G4 HAL uses htim.Init.DeadTime differently; verify register
     * values against RM0440 table 46 before powering motors.
     *
     * Native CAN FD: if using FDCAN instead of MCP2515, replace can_bus.c with
     * fdcan_bus.c and configure FDCAN1 (PB8/PB9 or PA11/PA12) at 500 kbps.
     */
}

void hal_motor_stop(void)
{
    /* TODO: HAL_TIM_PWM_Stop + HAL_TIMEx_PWMN_Stop for both motors,
     * or set MOE=0 in BDTR for immediate all-channels-off. */
}

#endif /* BOARD_FAMILY_STM32G4 */
