/*
 * system_gd32f1.c — Minimal SystemInit for GD32F130 (GD32F1 family, Cortex-M3).
 *
 * Motor-disabled smoke only — clock tree and peripheral init must be verified
 * on hardware before use.  No GPL vendor library required.
 *
 * SystemInit(): configures the GD32F130 to run from the internal 8MHz HSI
 * oscillator at reset-default speed.  This is sufficient for CAN init to start
 * and for the board_select/override machinery to run.  It does NOT:
 *   - Enable PLL (left for a full clock-tree implementation).
 *   - Configure flash wait states beyond the reset default (0 WS, sufficient
 *     at 8 MHz but MUST be set before running above ~24 MHz).
 *   - Enable any peripheral clocks (each peripheral init does its own CLK_EN).
 *
 * NOTE: GD32F130 HSI is 8 MHz; after SystemInit() the core runs at 8 MHz.
 * The full clock configuration (PLL to 72 MHz, flash WS=2) must be implemented
 * and validated on silicon before claiming any specific CPU frequency.
 *
 * GD32F130 register map (from GD32F1x0 User Manual):
 *   RCU_CTL  = 0x40021000  (bit 0: HSIEN, bit 1: HSISTB)
 *   RCU_CFG0 = 0x40021004  (SWS bits 2-3 = 00 for HSI)
 *   RCU_INT  = 0x40021008  (clear all pending interrupt flags)
 */

#include <stdint.h>

/* RCU register base (GD32F1x0 AHB1) */
#define RCU_BASE    0x40021000u
#define RCU_CTL     (*(volatile uint32_t *)(RCU_BASE + 0x00u))
#define RCU_CFG0    (*(volatile uint32_t *)(RCU_BASE + 0x04u))
#define RCU_INT     (*(volatile uint32_t *)(RCU_BASE + 0x08u))

/* RCU_CTL bits */
#define RCU_CTL_HSIEN  (1u << 0)   /* HSI oscillator enable */
#define RCU_CTL_HSISTB (1u << 1)   /* HSI oscillator stable */

/* SystemCoreClock: reflect 8 MHz HSI until PLL is configured. */
uint32_t SystemCoreClock = 8000000u;

void SystemInit(void)
{
    /*
     * Step 1: Ensure HSI is enabled and stable.
     * At reset, HSI is the default clock source and is already enabled on
     * GD32F130.  This is a belt-and-suspenders re-enable.
     */
    RCU_CTL |= RCU_CTL_HSIEN;
    /* Wait for HSI stable (HSISTB set) — bounded loop to avoid infinite hang
     * if silicon is absent or the oscillator fails to start. */
    uint32_t timeout = 5000u;
    while (!(RCU_CTL & RCU_CTL_HSISTB) && timeout--) {
        /* spin */
    }

    /*
     * Step 2: Select HSI as the system clock (SWS = 00).
     * Clear SW bits [1:0] in RCU_CFG0.  At reset these are already 00
     * (HSI selected), so this is a no-op on a fresh reset.
     */
    RCU_CFG0 &= ~0x3u;

    /*
     * Step 3: Clear any pending RCU interrupt flags.
     * Write 1 to the clear bits in RCU_INT to ensure clean state.
     * Bits [23:16] are the clear bits for the corresponding interrupt flags.
     */
    RCU_INT = 0x009F0000u;

    /*
     * SystemCoreClock remains 8 MHz.
     *
     * TODO (GD32F1 full clock init):
     *   1. Configure HSE (if external crystal present) and wait for HSESTB.
     *   2. Configure PLL: source=HSI/2 or HSE, multiplier for target freq.
     *   3. Set flash wait states BEFORE switching to PLL (GD32F130 FMC_WS).
     *   4. Switch system clock to PLL (SW = 10), wait for SWS = 10.
     *   5. Update SystemCoreClock to the actual PLL output frequency.
     *   6. Configure AHB/APB prescalers as needed.
     * All steps require verification on GD32F130 silicon.
     */
}
