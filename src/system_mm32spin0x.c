/*
 * system_mm32spin0x.c — Minimal SystemInit for MM32SPIN0x (Cortex-M0).
 *
 * Motor-disabled smoke only — clock tree and peripheral init must be verified
 * on hardware before use.  No GPL vendor library required.
 *
 * SystemInit(): configures the MM32SPIN0x to run from the internal 8MHz HSI
 * oscillator.  No PLL configuration.  Sufficient for CAN init and the
 * board_select/override machinery to run.
 *
 * MM32SPIN0x register map (from MM32SPIN0x Reference Manual):
 *   RCC_CR   = 0x40021000  — clock control (HSION bit 0, HSIRDY bit 1)
 *   RCC_CFGR = 0x40021004  — SW bits [1:0] = 00 → HSI as system clock
 *   RCC_CIR  = 0x40021008  — interrupt clear
 *
 * NOTE: MM32SPIN0x RCC register layout is STM32F0xx-compatible (same base
 * address, same bit positions for HSI/PLL control).  Verify against the
 * MM32SPIN0x RM before trusting this for production code.
 *
 * The MM32SPIN0x is Cortex-M0 (ARMv6-M); compiled with -mcpu=cortex-m0.
 *
 * Motor-disabled smoke only — clock tree and peripheral init must be verified
 * on hardware before use.
 */

#include <stdint.h>

/* RCC register base (MM32SPIN0x — same address as STM32F0xx) */
#define RCC_BASE    0x40021000u
#define RCC_CR      (*(volatile uint32_t *)(RCC_BASE + 0x00u))
#define RCC_CFGR    (*(volatile uint32_t *)(RCC_BASE + 0x04u))
#define RCC_CIR     (*(volatile uint32_t *)(RCC_BASE + 0x08u))

#define RCC_CR_HSION  (1u << 0)
#define RCC_CR_HSIRDY (1u << 1)

uint32_t SystemCoreClock = 8000000u;

void SystemInit(void)
{
    /* Enable HSI and wait for ready */
    RCC_CR |= RCC_CR_HSION;
    uint32_t timeout = 5000u;
    while (!(RCC_CR & RCC_CR_HSIRDY) && timeout--) {}

    /* Select HSI as system clock (SW[1:0] = 00) */
    RCC_CFGR &= ~0x3u;

    /* Clear RCC interrupt flags */
    RCC_CIR = 0x009F0000u;

    /*
     * TODO (MM32SPIN0X full clock init):
     *   1. Verify RCC register layout against MM32SPIN0x RM (may differ
     *      from the STM32-compatible addresses assumed here).
     *   2. Configure PLL from HSI/2 or HSE for the target frequency.
     *   3. Set flash wait states before switching to PLL.
     *   4. Switch to PLL and update SystemCoreClock.
     *   5. Bench-validate on MM32SPIN0x silicon before claiming any
     *      specific CPU frequency.
     *
     * WARNING: MM32SPIN0x is the highest-risk family in this firmware;
     * the register-level details have NOT been verified against actual
     * MM32SPIN0x silicon.  Treat this SystemInit as a placeholder only.
     */
}
