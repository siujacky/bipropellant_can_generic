/* bootloader_request.h — API for the running application to trigger a
 * programmatic jump back to the CAN+UART bootloader.
 *
 * Usage from anywhere in the firmware:
 *
 *   #include "bootloader_request.h"
 *   bootloader_request_reset();   // does not return
 *
 * What it does:
 *   1. Writes BKP_DR1 = 0xB001 ("boot-one") to the STM32 backup register.
 *      Backup registers survive NVIC_SystemReset() (they are in the RTC/backup
 *      power domain, kept alive by VBAT or VDD standby).
 *   2. Calls NVIC_SystemReset().
 *
 * After the reset the bootloader reads the flag, clears it, and waits 30 seconds
 * for a CAN or UART upload tool to connect — instead of the normal 500 ms — so
 * there is no race condition between the reset and the upload tool starting.
 *
 * If the upload tool does not connect within 30 s, the bootloader jumps to the
 * application as normal.
 */

#pragma once
#include "stm32f1xx_hal.h"

/* Write the BKP magic and reset into the bootloader.  Never returns. */
static inline __attribute__((noreturn)) void bootloader_request_reset(void)
{
    /* Enable the PWR and BKP peripheral clocks on APB1 */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();

    /* Disable backup domain write protection (required to write BKP registers) */
    HAL_PWR_EnableBkUpAccess();

    /* Write the "enter bootloader" magic — bootloader reads this on startup */
    BKP->DR1 = 0xB001U;

    /* Data Synchronisation Barrier: ensure the write completes before reset */
    __DSB();

    /* Reset — BKP register survives, bootloader sees the flag */
    NVIC_SystemReset();

    /* Unreachable — suppress compiler warning */
    for (;;) {}
}
