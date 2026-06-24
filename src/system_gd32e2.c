/*
 * system_gd32e2.c — Minimal SystemInit for GD32E230 (GD32E2 family, Cortex-M23).
 *
 * Motor-disabled smoke only — clock tree and peripheral init must be verified
 * on hardware before use.  No GPL vendor library required.
 *
 * SystemInit(): configures the GD32E230 to run from the internal 8MHz HSI
 * oscillator.  No PLL.  Sufficient for CAN init and board_select/override.
 *
 * GD32E230 (Cortex-M23) register map (from GD32E23x User Manual):
 *   RCU_CTL  = 0x40021000  (bit 0: HSIEN, bit 1: HSISTB)
 *   RCU_CFG0 = 0x40021004  (SW bits [1:0]: 00=HSI)
 *   RCU_INT  = 0x40021008
 *
 * NOTE: GD32E230 is a Cortex-M23 (ARMv8-M Baseline).  Compiled with
 * -mcpu=cortex-m23.  The register map for RCU is identical to the GD32F1x0
 * family; peripheral base addresses are the same AHB-based layout.
 *
 * Motor-disabled smoke only — clock tree and peripheral init must be verified
 * on hardware before use.
 */

#include <stdint.h>

/* RCU register base (GD32E23x AHB1 — identical to GD32F1x0 layout) */
#define RCU_BASE    0x40021000u
#define RCU_CTL     (*(volatile uint32_t *)(RCU_BASE + 0x00u))
#define RCU_CFG0    (*(volatile uint32_t *)(RCU_BASE + 0x04u))
#define RCU_INT     (*(volatile uint32_t *)(RCU_BASE + 0x08u))

#define RCU_CTL_HSIEN  (1u << 0)
#define RCU_CTL_HSISTB (1u << 1)

uint32_t SystemCoreClock = 8000000u;

void SystemInit(void)
{
    /* Enable and wait for HSI stable */
    RCU_CTL |= RCU_CTL_HSIEN;
    uint32_t timeout = 5000u;
    while (!(RCU_CTL & RCU_CTL_HSISTB) && timeout--) {}

    /* Select HSI as system clock (SW = 00) */
    RCU_CFG0 &= ~0x3u;

    /* Clear pending RCU interrupt flags */
    RCU_INT = 0x009F0000u;

    /*
     * TODO (GD32E2 full clock init):
     *   1. Configure PLL from HSI/2 or HSE for target frequency.
     *   2. Set FMC wait states BEFORE switching to PLL.
     *   3. Switch to PLL and update SystemCoreClock.
     *   All steps need GD32E23x RM verification on real Cortex-M23 silicon.
     */
}
